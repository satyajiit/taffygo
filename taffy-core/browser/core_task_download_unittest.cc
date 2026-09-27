// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect_action.h"
#include "taffy/browser/core_task_policy_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

void CanonicalField(std::vector<uint8_t>* out,
                    uint8_t tag,
                    base::span<const uint8_t> value) {
  out->push_back(tag);
  const uint64_t length = value.size();
  for (size_t index = 0; index < sizeof(length); ++index) {
    out->push_back(static_cast<uint8_t>(length >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

void CanonicalTextField(std::vector<uint8_t>* out,
                        uint8_t tag,
                        std::string_view value) {
  CanonicalField(out, tag, base::as_byte_span(value));
}

std::vector<uint8_t> DownloadIntent(bool starts,
                                    std::string_view browser_session_id) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {
      static_cast<uint8_t>(starts ? 15u : 16u)};
  CanonicalField(&intent, 0u, operation);
  CanonicalTextField(&intent, 1u, "tab-1");
  if (starts) {
    CanonicalTextField(&intent, 2u, "https://files.example.test/report.pdf");
    CanonicalTextField(&intent, 3u, browser_session_id);
  } else {
    CanonicalTextField(&intent, 2u, browser_session_id);
  }
  return intent;
}

std::vector<uint8_t> DownloadCancelIntent(std::string_view download_id) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  constexpr std::array<uint8_t, 1> kOperation = {27u};
  CanonicalField(&intent, 0u, kOperation);
  CanonicalTextField(&intent, 1u, "tab-1");
  CanonicalTextField(&intent, 2u, "browser-session-1");
  CanonicalTextField(&intent, 3u, download_id);
  return intent;
}

std::vector<uint8_t> DownloadLinkIntent() {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  CanonicalField(&intent, 0u, std::array<uint8_t, 1>{30u});
  CanonicalTextField(&intent, 1u, "tab-1");
  CanonicalTextField(&intent, 2u, "frame-1");
  CanonicalTextField(&intent, 3u, "epoch-1");
  CanonicalField(&intent, 4u,
                 std::array<uint8_t, 8>{7u, 0u, 0u, 0u, 0u, 0u, 0u, 0u});
  CanonicalTextField(&intent, 5u, "node-download");
  CanonicalField(&intent, 6u, std::array<uint8_t, 1>{0u});
  CanonicalTextField(&intent, 7u, "https://example.test");
  CanonicalTextField(&intent, 8u, "browser-session-1");
  return intent;
}

mojom::TaskEffectBindingPtr DownloadBinding(bool starts) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "operation-1", 1u, 7u, 10'000u, "operation-key-1");
  binding->effect_id = "effect-1";
  binding->task_id = "task-1";
  binding->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  binding->action = mojom::TaskActionEffect::New();
  auto& action = binding->action;
  action->action_id = "action-1";
  action->proposal_digest = std::string(64u, 'a');
  action->idempotency_key = binding->operation->idempotency_key;
  action->capability_id = "capability-1";
  action->dispatch_id = "dispatch-1";
  action->document = mojom::TaskFrozenDocument::New(
      "frame-1", "epoch-1", 7u, "https://example.test", std::nullopt);
  action->executable = mojom::TaskExecutableAction::New();
  auto& executable = action->executable;
  executable->action_class = starts ? mojom::PolicyActionClass::kStartDownload
                                    : mojom::PolicyActionClass::kObservePage;
  executable->operation_kind =
      starts ? mojom::TaskActionOperationKind::kDownloadStart
             : mojom::TaskActionOperationKind::kDownloadList;
  executable->tool_name =
      starts ? "browser.download.start" : "browser.download.list";
  executable->canonical_intent = DownloadIntent(starts, "browser-session-1");
  executable->input = mojom::TaskActionInput::New();
  executable->input->kind = mojom::TaskActionInputKind::kNone;
  executable->tab_id = "tab-1";
  executable->task_download = mojom::TaskDownloadActionBinding::New();
  executable->task_download->browser_session_id = "browser-session-1";
  action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
  };
  if (starts) {
    executable->destination_origin = "https://files.example.test";
    executable->destination_address = "https://files.example.test/report.pdf";
    action->preconditions.push_back(
        mojom::TaskActionPrecondition::kDestinationUnchanged);
    action->postcondition = mojom::TaskActionPostcondition::kDownloadStarted;
  } else {
    action->postcondition =
        mojom::TaskActionPostcondition::kObservationCaptured;
  }
  return binding;
}

mojom::TaskPolicyEffectPtr LinkDownloadPolicy(bool approved) {
  auto effect = core_task_policy_test::Effect();
  effect->action_class = mojom::PolicyActionClass::kStartDownload;
  effect->operation_kind = mojom::TaskActionOperationKind::kDownloadStart;
  effect->tool_name = "browser.download.from_link";
  effect->canonical_intent = DownloadLinkIntent();
  effect->node_id = "node-download";
  effect->context_risk = mojom::PolicyRiskClass::kSensitiveDisclosure;
  effect->control_mode = mojom::TaskControlMode::kAssistant;
  if (approved) {
    effect->approval = mojom::PolicyApprovalFact::New();
    effect->approval->receipt_reference = "receipt-1";
    effect->approval->proposal_digest = effect->proposal_digest;
    effect->approval->service_generation = core_task_policy_test::kGeneration;
    effect->approval->expires_at_monotonic_ms =
        core_task_policy_test::kNow + 10'000u;
    effect->approval->expires_at_utc_ms =
        core_task_policy_test::kNowUtc + 10'000u;
    effect->approval->browser_session_id =
        core_task_policy_test::kBrowserSessionId;
  }
  return effect;
}

TEST(CoreTaskDownloadTest, LinkPolicyBindsTheCanonicalBrowserSession) {
  using namespace core_task_policy_test;
  auto effect = LinkDownloadPolicy(true);
  ASSERT_TRUE(TaskDownloadCanonicalMatchesEffect(*effect));
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(*effect, kGeneration, kRevision,
                                            kNow));
  EXPECT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  // Keep the receipt internally valid for the other session: the refusal
  // must come from the session frozen into the observed link itself.
  effect->approval->browser_session_id = "browser-session-other";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, "browser-session-other", kNow, kNowUtc));
}

TEST(CoreTaskDownloadTest, UnapprovedLinkReachesPolicyWithoutInventingAReceipt) {
  using namespace core_task_policy_test;
  auto effect = LinkDownloadPolicy(false);
  ASSERT_TRUE(TaskDownloadCanonicalMatchesEffect(*effect));
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(*effect, kGeneration, kRevision,
                                            kNow));
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  auto document = Document();
  document.graph_revision = 7u;
  document.destination_address = "https://example.test/report.pdf";
  document.destination_origin = "https://example.test";
  auto request = BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  EXPECT_FALSE(request->approval);
  ASSERT_TRUE(request->scope);
  EXPECT_EQ(request->scope->node_id, effect->node_id);
  EXPECT_EQ(request->scope->destination_address, document.destination_address);
  EXPECT_EQ(request->context_risk,
            mojom::PolicyRiskClass::kSensitiveDisclosure);
  EXPECT_EQ(request->action_class, mojom::PolicyActionClass::kStartDownload);
}

TEST(CoreTaskDownloadTest, LinkPolicyRefusesSubstitutedReceiptAndLinkFacts) {
  using namespace core_task_policy_test;
  auto valid = LinkDownloadPolicy(true);
  for (int substitution = 0; substitution < 5; ++substitution) {
    SCOPED_TRACE(substitution);
    auto effect = valid.Clone();
    switch (substitution) {
      case 0:
        effect->approval->proposal_digest = std::string(64u, 'b');
        break;
      case 1:
        effect->approval->service_generation = kGeneration + 1u;
        break;
      case 2:
        effect->approval->expires_at_utc_ms = kNowUtc;
        break;
      case 3:
        effect->node_id = "node-other";
        break;
      case 4:
        effect->destination_address = "https://example.test/substituted.pdf";
        break;
    }
    EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
        *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  }
}

TEST(CoreTaskDownloadTest, StartBindsExactHttpsAddressSessionAndDocument) {
  mojom::TaskEffectBindingPtr binding = DownloadBinding(true);
  EXPECT_TRUE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));

  binding->action->executable->task_download->browser_session_id =
      "browser-session-other";
  EXPECT_FALSE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));
  binding->action->executable->task_download->browser_session_id =
      "browser-session-1";

  binding->action->executable->destination_address =
      "https://person:secret@files.example.test/report.pdf";
  EXPECT_FALSE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));
  binding->action->executable->destination_address =
      "https://files.example.test/report.pdf";

  binding->action->executable->destination_origin = "https://example.test";
  EXPECT_FALSE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));
}

TEST(CoreTaskDownloadTest, LinkDownloadBindsFreshControlSessionAndDocument) {
  mojom::TaskEffectBindingPtr binding = DownloadBinding(true);
  auto& action = *binding->action;
  action.executable->tool_name = "browser.download.from_link";
  action.executable->node_id = "node-download";
  action.executable->canonical_intent = DownloadLinkIntent();
  action.preconditions.insert(action.preconditions.begin() + 2,
                              mojom::TaskActionPrecondition::kNodePresent);
  ASSERT_TRUE(IsValidTaskActionEffect(action, *binding, 1'000u));

  action.executable->node_id = "node-other";
  EXPECT_FALSE(IsValidTaskActionEffect(action, *binding, 1'000u));
  action.executable->node_id = "node-download";
  action.document->page_epoch = "epoch-other";
  EXPECT_FALSE(IsValidTaskActionEffect(action, *binding, 1'000u));
  action.document->page_epoch = "epoch-1";
  action.executable->task_download->browser_session_id =
      "browser-session-other";
  EXPECT_FALSE(IsValidTaskActionEffect(action, *binding, 1'000u));
  action.executable->task_download->browser_session_id = "browser-session-1";
  action.executable->tool_name = "browser.download.start";
  EXPECT_FALSE(IsValidTaskActionEffect(action, *binding, 1'000u));
  action.executable->tool_name = "browser.download.from_link";
  action.executable->node_id.reset();
  EXPECT_FALSE(IsValidTaskActionEffect(action, *binding, 1'000u));
}

TEST(CoreTaskDownloadTest,
     LinkIntentHasNoStoredAddressAndRejectsTrailingField) {
  auto intent = DownloadLinkIntent();
  const auto parsed = ReadCanonicalDownloadLinkIntent(intent);
  ASSERT_TRUE(parsed);
  EXPECT_EQ("node-download", parsed->target.node_id);
  EXPECT_EQ("browser-session-1", parsed->browser_session_id);
  CanonicalTextField(&intent, 9u, "https://example.test/private-file.pdf");
  EXPECT_FALSE(ReadCanonicalDownloadLinkIntent(intent));
  EXPECT_FALSE(ReadCanonicalDownloadLinkIntent(
      DownloadIntent(true, "browser-session-1")));
}

TEST(CoreTaskDownloadTest,
     ListCannotAcquireDestinationNodeOrOtherTypedBinding) {
  mojom::TaskEffectBindingPtr binding = DownloadBinding(false);
  EXPECT_TRUE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));

  binding->action->executable->destination_address =
      "https://files.example.test/report.pdf";
  EXPECT_FALSE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));
  binding->action->executable->destination_address.reset();

  binding->action->executable->node_id = "node-1";
  EXPECT_FALSE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));
  binding->action->executable->node_id.reset();

  binding->action->executable->task_tab = mojom::TaskTabActionBinding::New();
  EXPECT_FALSE(IsValidTaskActionEffect(*binding->action, *binding, 1'000u));
}

TEST(CoreTaskDownloadTest, CanonicalIntentRejectsCrossOperationAndExtraFields) {
  EXPECT_TRUE(CanonicalDownloadStartIntentMatches(
      DownloadIntent(true, "browser-session-1"), "tab-1",
      "https://files.example.test/report.pdf", "browser-session-1"));
  EXPECT_TRUE(CanonicalDownloadListIntentMatches(
      DownloadIntent(false, "browser-session-1"), "tab-1",
      "browser-session-1"));
  EXPECT_FALSE(CanonicalDownloadListIntentMatches(
      DownloadIntent(true, "browser-session-1"), "tab-1", "browser-session-1"));

  std::vector<uint8_t> trailing = DownloadIntent(false, "browser-session-1");
  CanonicalTextField(&trailing, 3u, "unexpected");
  EXPECT_FALSE(CanonicalDownloadListIntentMatches(trailing, "tab-1",
                                                  "browser-session-1"));
}

TEST(CoreTaskDownloadTest, CancelBindsOneExactOpaqueTaskDownloadIdentity) {
  mojom::TaskEffectBindingPtr binding = DownloadBinding(false);
  auto& action = binding->action;
  action->executable->action_class = mojom::PolicyActionClass::kStartDownload;
  action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDownloadCancel;
  action->executable->tool_name = "browser.download.cancel";
  action->executable->task_download->download_id = "opaque-guid-1";
  action->executable->canonical_intent = DownloadCancelIntent("opaque-guid-1");
  action->postcondition = mojom::TaskActionPostcondition::kDownloadCancelled;

  EXPECT_TRUE(IsValidTaskActionEffect(*action, *binding, 1'000u));
  action->executable->task_download->download_id = "opaque-guid-other";
  EXPECT_FALSE(IsValidTaskActionEffect(*action, *binding, 1'000u));
  action->executable->task_download->download_id = "opaque-guid-1";
  action->executable->destination_address =
      "https://files.example.test/report.pdf";
  EXPECT_FALSE(IsValidTaskActionEffect(*action, *binding, 1'000u));
}

}  // namespace
}  // namespace taffy
