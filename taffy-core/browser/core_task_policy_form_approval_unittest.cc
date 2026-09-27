// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_policy.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "taffy/browser/core_task_policy_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using namespace core_task_policy_test;

mojom::TaskPolicyEffectPtr FormEffect() {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kFillField;
  effect->operation_kind = mojom::TaskActionOperationKind::kFormFill;
  effect->tool_name = "browser.form.fill";
  effect->node_id = "field-1";
  effect->canonical_intent = {0x10u, 0x20u, 0x30u};
  effect->input = mojom::TaskActionInput::New();
  effect->input->kind = mojom::TaskActionInputKind::kSuppliedValue;
  effect->input->supplied_value = mojom::TaskSuppliedValuePosition::New();
  effect->input->supplied_value->request_id = "values-1";
  effect->input->supplied_value->index = 2u;
  effect->approval = mojom::PolicyApprovalFact::New();
  effect->approval->receipt_reference = "approval-1";
  effect->approval->proposal_digest = effect->proposal_digest;
  effect->approval->service_generation = kGeneration;
  effect->approval->expires_at_monotonic_ms = kNow + 500u;
  effect->approval->expires_at_utc_ms = kNowUtc + 500u;
  effect->approval->browser_session_id = kBrowserSessionId;
  return effect;
}

BrowserFormActionPreapproval Preapproval(
    const mojom::TaskPolicyEffect& effect) {
  return BrowserFormActionPreapproval{
      .task_id = effect.task_id,
      .action_id = effect.action_id,
      .proposal_digest = effect.proposal_digest,
      .tool_name = effect.tool_name,
      .tab_id = effect.tab_id,
      .node_id = *effect.node_id,
      .request_id = effect.input->supplied_value->request_id,
      .canonical_intent = effect.canonical_intent,
      .supplied_value_index = effect.input->supplied_value->index,
      .normalized_origin = "https://example.test",
      .frame_id = "frame-1",
      .page_epoch = "epoch-1",
      .graph_revision = 12u,
      .expires_at_monotonic_ms = effect.approval->expires_at_monotonic_ms,
      .expires_at_utc_ms = effect.approval->expires_at_utc_ms,
  };
}

bool Matches(const mojom::TaskPolicyEffect& effect,
             const TaskPolicyDocumentBinding& document,
             const BrowserFormActionPreapproval& approved,
             uint64_t now = kNow,
             uint64_t now_utc = kNowUtc) {
  return TaskPolicyEffectMatchesFormPreapproval(
      effect, document, approved, kBrowserSessionId, now, now_utc);
}

TEST(CoreTaskPolicyFormApprovalTest, ExactExecutableTupleMatchesOnce) {
  const auto effect = FormEffect();
  EXPECT_TRUE(Matches(*effect, Document(), Preapproval(*effect)));
}

TEST(CoreTaskPolicyFormApprovalTest, TaskActionAndProposalSubstitutionFail) {
  auto effect = FormEffect();
  const BrowserFormActionPreapproval approved = Preapproval(*effect);
  effect->task_id = "task-2";
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->action_id = "action-2";
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->proposal_digest = std::string(64u, 'b');
  EXPECT_FALSE(Matches(*effect, Document(), approved));
}

TEST(CoreTaskPolicyFormApprovalTest, ToolTargetAndCanonicalSubstitutionFail) {
  auto effect = FormEffect();
  const BrowserFormActionPreapproval approved = Preapproval(*effect);
  effect->tool_name = "browser.form.select";
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->tab_id = "tab-2";
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->node_id = "field-2";
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->canonical_intent.back() ^= 0xffu;
  EXPECT_FALSE(Matches(*effect, Document(), approved));
}

TEST(CoreTaskPolicyFormApprovalTest, SuppliedReferenceSubstitutionFails) {
  auto effect = FormEffect();
  const BrowserFormActionPreapproval approved = Preapproval(*effect);
  effect->input->supplied_value->request_id = "values-2";
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->input->supplied_value->index = 1u;
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->input->kind = mojom::TaskActionInputKind::kNone;
  EXPECT_FALSE(Matches(*effect, Document(), approved));
}

TEST(CoreTaskPolicyFormApprovalTest, DocumentSubstitutionFails) {
  const auto effect = FormEffect();
  const BrowserFormActionPreapproval approved = Preapproval(*effect);
  auto document = Document();
  document.origin = "https://other.test";
  EXPECT_FALSE(Matches(*effect, document, approved));

  document = Document();
  document.frame_id = "frame-2";
  EXPECT_FALSE(Matches(*effect, document, approved));

  document = Document();
  document.page_epoch = "epoch-2";
  EXPECT_FALSE(Matches(*effect, document, approved));

  document = Document();
  document.graph_revision = 13u;
  EXPECT_FALSE(Matches(*effect, document, approved));
}

TEST(CoreTaskPolicyFormApprovalTest, ClassAndOperationCannotBeWidened) {
  auto effect = FormEffect();
  const BrowserFormActionPreapproval approved = Preapproval(*effect);
  effect->action_class = mojom::PolicyActionClass::kSelectOption;
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->operation_kind = mojom::TaskActionOperationKind::kFormSelect;
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->destination_address = "https://example.test/submit";
  EXPECT_FALSE(Matches(*effect, Document(), approved));
}

TEST(CoreTaskPolicyFormApprovalTest, ExpiryMustBeExactAndStillLive) {
  auto effect = FormEffect();
  const BrowserFormActionPreapproval approved = Preapproval(*effect);
  ++effect->approval->expires_at_monotonic_ms;
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  ++effect->approval->expires_at_utc_ms;
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  effect = FormEffect();
  effect->approval->browser_session_id = "other-session";
  EXPECT_FALSE(Matches(*effect, Document(), approved));

  EXPECT_FALSE(Matches(*FormEffect(), Document(), approved,
                       approved.expires_at_monotonic_ms, kNowUtc));
  EXPECT_FALSE(Matches(*FormEffect(), Document(), approved, kNow,
                       approved.expires_at_utc_ms));
}

void Field(std::vector<uint8_t>* out,
           uint8_t tag,
           base::span<const uint8_t> value) {
  out->push_back(tag);
  const uint64_t length = value.size();
  for (size_t index = 0; index < sizeof(length); ++index) {
    out->push_back(static_cast<uint8_t>(length >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

void TextField(std::vector<uint8_t>* out, uint8_t tag, std::string_view text) {
  Field(out, tag, base::as_byte_span(text));
}

// The canonical intent of `browser.form.fill` into field-1 of tab-1 with held
// value `position` of request values-1.
std::vector<uint8_t> FillIntent(uint32_t position) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {13u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "field-1");
  TextField(&intent, 3u, "values-1");
  std::array<uint8_t, sizeof(position)> bytes = {};
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<uint8_t>(position >> (index * 8u));
  }
  Field(&intent, 4u, bytes);
  return intent;
}

// A fill as the task first asks about it: an exact executable tuple, and no
// approval yet, because nobody has been asked.
mojom::TaskPolicyEffectPtr FirstFillAsk() {
  auto effect = FormEffect();
  effect->canonical_intent = FillIntent(effect->input->supplied_value->index);
  effect->context_risk = mojom::PolicyRiskClass::kSensitiveDisclosure;
  effect->approval.reset();
  return effect;
}

bool IsWellFormed(const mojom::TaskPolicyEffect& effect) {
  return IsValidReadOnlyTaskPolicyEffect(effect, kGeneration, kRevision,
                                         kBrowserSessionId, kNow, kNowUtc);
}

// Decision 0239. Refusing this ask as malformed was why no fill reached a
// page on a phone: the policy engine is what answers it, with a question.
TEST(CoreTaskPolicyFormApprovalTest, AFillsFirstAskIsWellFormedAndAsksFirst) {
  const auto effect = FirstFillAsk();
  EXPECT_TRUE(IsWellFormed(*effect));
  EXPECT_TRUE(TaskPolicyAskIsAnsweredByAsking(*effect));
}

TEST(CoreTaskPolicyFormApprovalTest, AnApprovedFillOrAReadDoesNotAskFirst) {
  auto approved = FirstFillAsk();
  approved->approval = FormEffect()->approval.Clone();
  EXPECT_TRUE(IsWellFormed(*approved));
  EXPECT_FALSE(TaskPolicyAskIsAnsweredByAsking(*approved));

  // A read never waits on a person, with or without a receipt.
  EXPECT_FALSE(TaskPolicyAskIsAnsweredByAsking(*Effect()));
}

TEST(CoreTaskPolicyFormApprovalTest, TheFirstAskIsStillAnExactTuple) {
  // Relaxing the receipt relaxed nothing else: the intent must still name the
  // field and the held value the input does.
  auto effect = FirstFillAsk();
  effect->canonical_intent =
      FillIntent(effect->input->supplied_value->index + 1u);
  EXPECT_FALSE(IsWellFormed(*effect));

  effect = FirstFillAsk();
  effect->node_id = "field-2";
  EXPECT_FALSE(IsWellFormed(*effect));
}

}  // namespace
}  // namespace taffy
