// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "taffy/browser/core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

std::vector<uint8_t> CanonicalNavigate(std::string_view address, bool new_tab) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> out(kPrefix.begin(), kPrefix.end());
  auto field = [&](uint8_t tag, base::span<const uint8_t> value) {
    out.push_back(tag);
    for (size_t index = 0u; index < 8u; ++index) {
      out.push_back(static_cast<uint8_t>(value.size() >> (index * 8u)));
    }
    out.insert(out.end(), value.begin(), value.end());
  };
  const std::array<uint8_t, 1> operation = {0u};
  const std::array<uint8_t, 1> flag = {static_cast<uint8_t>(new_tab)};
  field(0u, operation);
  field(1u, base::as_byte_span(std::string_view("tab-1")));
  field(2u, base::as_byte_span(address));
  field(3u, flag);
  return out;
}

mojom::TaskEffectBindingPtr DiscoveryNavigateBinding() {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "effect-operation", 7u, 11u, 10'000u, "action-key");
  binding->effect_id = "effect-1";
  binding->task_id = "task-1";
  binding->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  binding->action = mojom::TaskActionEffect::New();
  auto& action = *binding->action;
  action.action_id = "action-1";
  action.proposal_digest = std::string(64u, 'a');
  action.idempotency_key = "action-key";
  action.capability_id = "capability-1";
  action.dispatch_id = "dispatch-1";
  action.document = mojom::TaskFrozenDocument::New("frame-1", "epoch-blank", 0u,
                                                   "", "opaque-blank");
  action.executable = mojom::TaskExecutableAction::New();
  action.executable->action_class = mojom::PolicyActionClass::kOpenLink;
  action.executable->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  action.executable->tool_name = "browser.navigate";
  action.executable->tab_id = "tab-1";
  action.executable->destination_address = "https://official.test/document";
  action.executable->destination_origin = "https://official.test";
  action.executable->canonical_intent =
      CanonicalNavigate(*action.executable->destination_address, false);
  action.executable->input = mojom::TaskActionInput::New();
  action.executable->input->kind = mojom::TaskActionInputKind::kNone;
  action.preconditions = {mojom::TaskActionPrecondition::kDocumentUnchanged,
                          mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
                          mojom::TaskActionPrecondition::kDestinationUnchanged};
  action.postcondition = mojom::TaskActionPostcondition::kDocumentNavigated;
  return binding;
}

TEST(CoreTaskEffectDiscoveryTest, ExactOpaqueNavigateDispatchIsAdmitted) {
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*DiscoveryNavigateBinding(),
                                                   7u, 11u, 1'000u));
}

TEST(CoreTaskEffectDiscoveryTest, OpaqueNavigateCannotWidenItsFrozenIntent) {
  for (int mutation = 0; mutation < 7; ++mutation) {
    SCOPED_TRACE(mutation);
    auto binding = DiscoveryNavigateBinding();
    auto& action = *binding->action;
    switch (mutation) {
      case 0:
        action.executable->canonical_intent =
            CanonicalNavigate(*action.executable->destination_address, true);
        break;
      case 1:
        action.executable->destination_address =
            "https://official.test/different";
        break;
      case 2:
        action.executable->tab_id = "different-tab";
        break;
      case 3:
        action.document->graph_revision = 1u;
        break;
      case 4:
        action.executable->node_id = "node-1";
        break;
      case 5:
        action.executable->operation_kind =
            mojom::TaskActionOperationKind::kTabsOpen;
        break;
      case 6:
        action.executable->destination_address =
            "http://official.test/document";
        action.executable->destination_origin = "http://official.test";
        action.executable->canonical_intent =
            CanonicalNavigate(*action.executable->destination_address, false);
        break;
    }
    EXPECT_FALSE(
        IsStructurallyValidTaskEffectBinding(*binding, 7u, 11u, 1'000u));
  }
}

}  // namespace
}  // namespace taffy
