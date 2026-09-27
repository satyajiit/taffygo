// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_task_policy.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kRevision = 11u;
constexpr uint64_t kNow = 1'000u;
constexpr uint64_t kNowUtc = 1'800'000'000'000u;
constexpr char kBrowserSessionId[] = "browser-session-1";

void CanonicalField(std::vector<uint8_t>* out,
                    uint8_t tag,
                    base::span<const uint8_t> value) {
  out->push_back(tag);
  for (size_t index = 0; index < 8u; ++index) {
    out->push_back(static_cast<uint8_t>(value.size() >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

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

std::vector<uint8_t> TabsOpenIntent(std::string_view address) {
  std::vector<uint8_t> operand = {1u};
  operand.insert(operand.end(), address.begin(), address.end());
  return BrowserIntent(4u, operand);
}

std::vector<uint8_t> NavigateIntent(std::string_view address,
                                    bool new_tab = false) {
  auto intent = BrowserIntent(0u, base::as_byte_span(address));
  const std::array<uint8_t, 1> flag = {static_cast<uint8_t>(new_tab)};
  CanonicalField(&intent, 3u, flag);
  return intent;
}

mojom::TaskPolicyEffectPtr Effect() {
  auto effect = mojom::TaskPolicyEffect::New();
  effect->operation = mojom::OperationEnvelope::New(
      "policy-effect-1", kGeneration, kRevision, 20'000u, "idempotency-1");
  effect->effect_id = effect->operation->operation_id;
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

mojom::TaskPolicyEffectPtr DiscoveryNavigate() {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  effect->tool_name = "browser.navigate";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->control_mode = mojom::TaskControlMode::kAssistant;
  effect->destination_address = "https://official.test/download-document";
  effect->canonical_intent = NavigateIntent(*effect->destination_address);
  effect->discovery =
      mojom::TaskDiscoveryAuthorityFact::New("tab-1", kBrowserSessionId, 3u);
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

TEST(CoreTaskPolicyDiscoveryTest,
     SearchAndTaskTabOpenBindTheirExactBrowserOperands) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kSearch;
  effect->tool_name = "browser.search";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->transient_search_query = "tea & cake";
  effect->canonical_intent = SearchIntent(*effect->transient_search_query);
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  auto document = Document();
  document.destination_address = "https://search.test/?q=tea+%26+cake";
  document.destination_origin = "https://search.test";
  auto request = BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  ASSERT_TRUE(request->scope->destination_address);
  EXPECT_EQ(*document.destination_address,
            *request->scope->destination_address);
  effect->transient_search_query = "different";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kCreateTaskTab;
  effect->operation_kind = mojom::TaskActionOperationKind::kTabsOpen;
  effect->tool_name = "browser.tabs.open";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->destination_address = "https://destination.test/research";
  effect->canonical_intent = TabsOpenIntent(*effect->destination_address);
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  document = Document();
  document.destination_address = effect->destination_address;
  document.destination_origin = "https://destination.test";
  request = BindReadOnlyTaskPolicyRequest(*effect, document, "profile-1",
                                          Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  ASSERT_TRUE(request->scope->destination_address);
  EXPECT_EQ(*effect->destination_address, *request->scope->destination_address);
  effect->destination_address = "https://destination.test/changed";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyDiscoveryTest,
     DiscoverySearchBindsOnlyExactOpaqueSourceAndTupleDestination) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kSearch;
  effect->tool_name = "browser.search";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->control_mode = mojom::TaskControlMode::kAssistant;
  effect->transient_search_query = "tea & cake";
  effect->canonical_intent = SearchIntent(*effect->transient_search_query);
  effect->discovery =
      mojom::TaskDiscoveryAuthorityFact::New("tab-1", kBrowserSessionId, 3u);
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  TaskPolicyDocumentBinding document{
      .tab_id = "tab-1",
      .frame_id = "frame-blank",
      .page_epoch = "epoch-blank",
      .origin = "",
      .opaque_origin_id = "epoch-blank",
      .graph_revision = 0u,
      .destination_address = "https://search.test/?q=tea+%26+cake",
      .destination_origin = "https://search.test",
  };
  auto request = BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  EXPECT_EQ(request->context, mojom::PolicyEvaluationContext::kTaskDiscovery);
  ASSERT_TRUE(request->discovery);
  EXPECT_EQ(request->discovery->browser_session_id, kBrowserSessionId);
  ASSERT_TRUE(request->scope->origin);
  EXPECT_EQ(request->scope->origin->kind, mojom::PolicyOriginKind::kOpaque);
  EXPECT_FALSE(request->scope->origin->serialization);
  EXPECT_EQ(request->scope->origin->opaque_id,
            std::optional<std::string>("epoch-blank"));
  ASSERT_TRUE(request->scope->destination_scope);
  EXPECT_EQ(request->scope->destination_scope->kind,
            mojom::PolicyOriginKind::kTuple);

  effect->discovery->browser_session_id = "stale-session";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  effect->discovery->browser_session_id = kBrowserSessionId;
  auto tuple_document = document;
  tuple_document.origin = "https://example.test";
  EXPECT_FALSE(BindReadOnlyTaskPolicyRequest(*effect, tuple_document,
                                             "profile-1", Lease(), kGeneration,
                                             kNow, kNowUtc));

  // Decision 0176: which origin shape a scope carries is a fact about the
  // document, not about the kind of tab it sits in. A blank discovery document
  // and the error document Chromium writes when an address does not answer are
  // both documents with no site of their own, so an ordinary task effect binds
  // an opaque scope against one. What a discovery effect may not do is claim
  // that shape on a document that does have a site, which is the
  // `tuple_document` refusal just above.
  effect->discovery.reset();
  auto departure = BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(departure);
  EXPECT_EQ(departure->context, mojom::PolicyEvaluationContext::kTask);
  EXPECT_FALSE(departure->discovery);
  ASSERT_TRUE(departure->scope->origin);
  EXPECT_EQ(departure->scope->origin->kind, mojom::PolicyOriginKind::kOpaque);
  EXPECT_EQ(departure->scope->origin->opaque_id,
            std::optional<std::string>("epoch-blank"));
}

TEST(CoreTaskPolicyDiscoveryTest,
     KnownHttpsAddressBindsOwnedBlankTabAndExactDestination) {
  auto effect = DiscoveryNavigate();
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  TaskPolicyDocumentBinding document{
      .tab_id = "tab-1",
      .frame_id = "frame-blank",
      .page_epoch = "epoch-blank",
      .origin = "",
      .opaque_origin_id = "epoch-blank",
      .graph_revision = 0u,
      .destination_address = effect->destination_address,
      .destination_origin = "https://official.test",
  };
  auto request = BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  EXPECT_EQ(request->context, mojom::PolicyEvaluationContext::kTaskDiscovery);
  ASSERT_TRUE(request->discovery);
  EXPECT_EQ(request->discovery->remaining_new_source_cap, 3u);
  EXPECT_EQ(request->scope->destination_address, effect->destination_address);
  EXPECT_EQ(request->scope->origin->kind, mojom::PolicyOriginKind::kOpaque);
  ASSERT_TRUE(request->scope->destination_scope);
  EXPECT_EQ(request->scope->destination_scope->serialization,
            std::optional<std::string>("https://official.test"));
  document.destination_origin = "https://different.test";
  EXPECT_FALSE(BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc));
  document.destination_origin = "https://official.test";
  document.origin = "https://official.test";
  EXPECT_FALSE(BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc));
}

TEST(CoreTaskPolicyDiscoveryTest,
     KnownAddressRejectsAlteredCanonicalOperandsAndNonHttps) {
  for (int mutation = 0; mutation < 7; ++mutation) {
    SCOPED_TRACE(mutation);
    auto effect = DiscoveryNavigate();
    switch (mutation) {
      case 0:
        effect->destination_address = "https://official.test/different";
        break;
      case 1:
        effect->tab_id = "other-tab";
        effect->discovery->discovery_tab_id = effect->tab_id;
        break;
      case 2:
        effect->canonical_intent =
            NavigateIntent(*effect->destination_address, true);
        break;
      case 3:
        effect->canonical_intent.pop_back();
        break;
      case 4:
        CanonicalField(&effect->canonical_intent, 4u, {});
        break;
      case 5:
        effect->destination_address = "http://official.test/download-document";
        effect->canonical_intent = NavigateIntent(*effect->destination_address);
        break;
      case 6:
        effect->canonical_intent = TabsOpenIntent(*effect->destination_address);
        break;
    }
    EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
        *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  }
}

TEST(CoreTaskPolicyDiscoveryTest,
     KnownAddressPreservesDiscoveryAuthorityBounds) {
  for (int mutation = 0; mutation < 8; ++mutation) {
    SCOPED_TRACE(mutation);
    auto effect = DiscoveryNavigate();
    switch (mutation) {
      case 0:
        effect->discovery->browser_session_id = "previous-session";
        break;
      case 1:
        effect->discovery->remaining_new_source_cap = 0u;
        break;
      case 2:
        effect->discovery->remaining_new_source_cap =
            mojom::kMaxNewSourceCap + 1u;
        break;
      case 3:
        effect->discovery->discovery_tab_id = "other-tab";
        break;
      case 4:
        effect->principal = mojom::PolicyPrincipal::New(
            mojom::PolicyPrincipalKind::kSkill, "skill-version-1");
        break;
      case 5:
        effect->control_mode = mojom::TaskControlMode::kShared;
        break;
      case 6:
        effect->node_id = "node-1";
        break;
      case 7:
        effect->data_classes = {mojom::BipSensitivity::kCredential};
        break;
    }
    EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
        *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  }
}

}  // namespace
}  // namespace taffy
