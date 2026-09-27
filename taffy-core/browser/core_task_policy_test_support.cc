// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_policy_test_support.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "crypto/sha2.h"

namespace taffy::core_task_policy_test {

namespace mojom = core_service::mojom;

namespace {

void CanonicalField(std::vector<uint8_t>* out,
                    uint8_t tag,
                    base::span<const uint8_t> value) {
  out->push_back(tag);
  for (size_t index = 0; index < 8u; ++index) {
    out->push_back(static_cast<uint8_t>(value.size() >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

}  // namespace

std::vector<uint8_t> BrowserIntent(uint8_t operation,
                                   base::span<const uint8_t> operand) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation_bytes = {operation};
  CanonicalField(&intent, 0u, operation_bytes);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  CanonicalField(&intent, 2u, operand);
  return intent;
}

std::vector<uint8_t> FormInspectIntent(std::string_view form_node_id) {
  return BrowserIntent(12u, base::as_byte_span(form_node_id));
}

std::vector<uint8_t> SearchIntent(std::string_view query) {
  std::vector<uint8_t> operand;
  CanonicalField(&operand, 0u,
                 base::as_byte_span(std::string_view("search-operand")));
  const std::array<uint8_t, 1> kind = {0u};
  CanonicalField(&operand, 1u, kind);
  const std::array<uint8_t, 32> digest =
      crypto::SHA256Hash(base::as_byte_span(query));
  CanonicalField(&operand, 2u, digest);
  return BrowserIntent(1u, operand);
}

std::vector<uint8_t> ObservedNodeIntent(uint8_t operation_tag,
                                        std::string_view node_id,
                                        std::optional<bool> expected_expanded,
                                        uint8_t graph_revision = 12u) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {operation_tag};
  CanonicalField(&intent, 0u, operation);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  CanonicalField(&intent, 2u, base::as_byte_span(std::string_view("frame-1")));
  CanonicalField(&intent, 3u, base::as_byte_span(std::string_view("epoch-1")));
  const std::array<uint8_t, 8> revision = {graph_revision};
  CanonicalField(&intent, 4u, revision);
  CanonicalField(&intent, 5u, base::as_byte_span(node_id));
  const std::array<uint8_t, 1> tuple_origin = {0u};
  CanonicalField(&intent, 6u, tuple_origin);
  CanonicalField(&intent, 7u,
                 base::as_byte_span(std::string_view("https://example.test")));
  if (operation_tag == 10u) {
    // Field 8 is always present on an activation. 0 expanded, 1 collapsed,
    // 2 no disclosure claim at all, which is an ordinary press.
    const std::array<uint8_t, 1> state = {
        expected_expanded ? static_cast<uint8_t>(*expected_expanded ? 0u : 1u)
                          : uint8_t{2u}};
    CanonicalField(&intent, 8u, state);
  }
  return intent;
}

std::vector<uint8_t> DomActivationIntent(
    std::string_view node_id,
    std::optional<bool> expected_expanded) {
  return ObservedNodeIntent(10u, node_id, expected_expanded);
}

std::vector<uint8_t> DomFocusIntent(std::string_view node_id) {
  return ObservedNodeIntent(25u, node_id, std::nullopt);
}

std::vector<uint8_t> LinkOpenIntent(std::string_view node_id,
                                    uint8_t graph_revision) {
  return ObservedNodeIntent(22u, node_id, std::nullopt, graph_revision);
}

std::vector<uint8_t> SelectionReadIntent() {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {17u};
  CanonicalField(&intent, 0u, operation);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  return intent;
}

std::vector<uint8_t> TabControlIntent(uint8_t operation) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation_bytes = {operation};
  CanonicalField(&intent, 0u, operation_bytes);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  return intent;
}

std::vector<uint8_t> TaskTabIntent(uint8_t operation,
                                   std::string_view context_tab_id,
                                   std::string_view browser_session_id,
                                   uint64_t target_revision) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation_bytes = {operation};
  CanonicalField(&intent, 0u, operation_bytes);
  CanonicalField(&intent, 1u, base::as_byte_span(context_tab_id));
  CanonicalField(&intent, 2u, base::as_byte_span(browser_session_id));
  if (operation == 6u || operation == 7u) {
    CanonicalField(&intent, 3u,
                   base::as_byte_span(std::string_view("tab-owned")));
    CanonicalField(&intent, 4u,
                   base::as_byte_span(std::string_view("frame-owned")));
    CanonicalField(&intent, 5u,
                   base::as_byte_span(std::string_view("epoch-owned")));
    std::array<uint8_t, sizeof(target_revision)> revision_bytes = {};
    for (size_t index = 0; index < revision_bytes.size(); ++index) {
      revision_bytes[index] =
          static_cast<uint8_t>(target_revision >> (index * 8u));
    }
    CanonicalField(&intent, 6u, revision_bytes);
  }
  return intent;
}

std::vector<uint8_t> DomQueryIntent(std::optional<std::string_view> within) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {8u};
  CanonicalField(&intent, 0u, operation);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  std::vector<uint8_t> optional_within = {static_cast<uint8_t>(within ? 1 : 0)};
  if (within) {
    optional_within.insert(optional_within.end(), within->begin(),
                           within->end());
  }
  CanonicalField(&intent, 2u, optional_within);
  const std::array<uint8_t, 1> absent = {0u};
  CanonicalField(&intent, 3u, absent);
  CanonicalField(&intent, 4u, absent);
  CanonicalField(&intent, 5u, absent);
  return intent;
}

mojom::TaskPolicyEffectPtr Effect() {
  auto effect = mojom::TaskPolicyEffect::New();
  effect->operation = mojom::OperationEnvelope::New(
      "policy-effect-1", kGeneration, kRevision, 20'000u, "idempotency-1");
  effect->effect_id = "task-effect-1";
  effect->task_id = "task-1";
  effect->action_id = "action-1";
  effect->action_class = mojom::PolicyActionClass::kObservePage;
  effect->operation_kind = mojom::TaskActionOperationKind::kDomRead;
  effect->tool_name = "browser.dom.read";
  effect->canonical_intent = {0x01u, 0x02u, 0x03u};
  effect->input = mojom::TaskActionInput::New();
  effect->input->kind = mojom::TaskActionInputKind::kNone;
  effect->proposal_digest = std::string(64u, 'a');
  effect->idempotency_key = effect->operation->idempotency_key;
  effect->tab_id = "tab-1";
  effect->principal = mojom::PolicyPrincipal::New(
      mojom::PolicyPrincipalKind::kAssistant, std::nullopt);
  effect->data_classes = {mojom::BipSensitivity::kNotSensitive};
  effect->context_risk = mojom::PolicyRiskClass::kLocalRead;
  effect->control_mode = mojom::TaskControlMode::kShared;
  effect->policy_version = 3u;
  return effect;
}

ActorLeaseResult Lease() {
  return ActorLeaseResult{
      .code = ActorLeaseResultCode::kIssued,
      .lease_id = ActorLeaseId{"lease-1"},
      .expires_at_monotonic_ms = 10'000u,
  };
}

TaskPolicyDocumentBinding Document() {
  return TaskPolicyDocumentBinding{
      .tab_id = "tab-1",
      .frame_id = "frame-1",
      .page_epoch = "epoch-1",
      .origin = "https://example.test",
      .graph_revision = 12u,
  };
}

}  // namespace taffy::core_task_policy_test
