// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_policy.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "crypto/sha2.h"
#include "taffy/browser/core_task_policy_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using namespace core_task_policy_test;

TEST(CoreTaskPolicyTest, BindsExactReducerAndLiveDocumentFacts) {
  const auto effect = Effect();
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  EXPECT_NE(effect->effect_id, effect->operation->operation_id);

  const auto request = BindReadOnlyTaskPolicyRequest(
      *effect, Document(), "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  ASSERT_TRUE(request->operation);
  EXPECT_EQ(effect->operation, request->operation);
  EXPECT_EQ(effect->task_id, request->task_id);
  EXPECT_EQ(effect->action_id, request->action_id);
  EXPECT_EQ(effect->proposal_digest, request->proposal_digest);
  EXPECT_EQ(effect->operation_kind, request->operation_kind);
  const auto canonical_digest = crypto::SHA256Hash(effect->canonical_intent);
  EXPECT_TRUE(std::equal(canonical_digest.begin(), canonical_digest.end(),
                         request->canonical_intent_digest.begin(),
                         request->canonical_intent_digest.end()));
  EXPECT_EQ(effect->principal, request->principal);
  EXPECT_EQ(effect->data_classes, request->data_classes);
  EXPECT_EQ(effect->context_risk, request->context_risk);
  EXPECT_EQ(effect->policy_version, request->policy_version);
  EXPECT_EQ(kNowUtc, request->now_utc_ms);
  ASSERT_TRUE(request->scope);
  ASSERT_TRUE(request->scope->origin);
  EXPECT_EQ("profile-1", request->scope->profile_id);
  EXPECT_EQ("tab-1", request->scope->tab_id);
  EXPECT_EQ("frame-1", request->scope->frame_id);
  EXPECT_EQ("epoch-1", request->scope->page_epoch);
  EXPECT_EQ("https://example.test", request->scope->origin->serialization);
  EXPECT_EQ(12u, request->scope->required_graph_revision);
  EXPECT_FALSE(request->scope->node_id.has_value());
  ASSERT_TRUE(request->actor_lease);
  ASSERT_TRUE(request->actor_lease->authority_subject);
  EXPECT_EQ("lease-1", request->actor_lease->lease_id);
  EXPECT_EQ(effect->task_id,
            request->actor_lease->authority_subject->authority_subject_id);
  EXPECT_EQ(mojom::PolicyEvaluationContext::kTask, request->context);
}

TEST(CoreTaskPolicyTest, RejectsStaleOrReidentifiedProposal) {
  auto effect = Effect();
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration + 1u, kRevision, kBrowserSessionId, kNow, kNowUtc));
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision + 1u, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->effect_id.clear();
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->idempotency_key = "different-key";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->proposal_digest[0] = 'A';
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, AdmitsTypedPageActionsAndRefusesWidenedObservation) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kSyntheticClick;
  effect->operation_kind = mojom::TaskActionOperationKind::kDomClick;
  effect->tool_name = "browser.dom.click";
  effect->canonical_intent = DomActivationIntent("node-1", true);
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->node_id = "node-1";
  EXPECT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kMoveFocus;
  effect->operation_kind = mojom::TaskActionOperationKind::kDomFocus;
  effect->tool_name = "browser.dom.focus";
  effect->canonical_intent = DomFocusIntent("node-1");
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->node_id = "node-1";
  EXPECT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kSyntheticClick;
  effect->operation_kind = mojom::TaskActionOperationKind::kDomClick;
  effect->tool_name = "browser.dom.click";
  effect->canonical_intent = DomActivationIntent("node-1", false);
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kScrollIntoView;
  effect->operation_kind = mojom::TaskActionOperationKind::kDomScroll;
  effect->tool_name = "browser.dom.scroll";
  effect->node_id = "node-1";
  EXPECT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->node_id = "node-1";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, CopiesANodeIdOntoTheBoundPolicyScope) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kScrollIntoView;
  effect->operation_kind = mojom::TaskActionOperationKind::kDomScroll;
  effect->tool_name = "browser.dom.scroll";
  effect->node_id = "node-1";
  const auto request = BindReadOnlyTaskPolicyRequest(
      *effect, Document(), "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  ASSERT_TRUE(request->scope);
  ASSERT_TRUE(request->scope->node_id);
  EXPECT_EQ("node-1", *request->scope->node_id);
  EXPECT_FALSE(request->scope->destination_scope);
}

TEST(CoreTaskPolicyTest, ExactReadToolsKeepTheirDistinctTargetShape) {
  auto form = Effect();
  form->operation_kind = mojom::TaskActionOperationKind::kFormInspect;
  form->tool_name = "browser.form.inspect";
  form->node_id = "form-1";
  form->canonical_intent = FormInspectIntent(*form->node_id);
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *form, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  auto form_request = BindReadOnlyTaskPolicyRequest(
      *form, Document(), "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(form_request);
  ASSERT_TRUE(form_request->scope->node_id);
  EXPECT_EQ("form-1", *form_request->scope->node_id);

  form->node_id = "form-2";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *form, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  auto selection = Effect();
  selection->operation_kind = mojom::TaskActionOperationKind::kSelectionRead;
  selection->tool_name = "browser.selection.read";
  selection->canonical_intent = SelectionReadIntent();
  EXPECT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *selection, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  selection->node_id = "invented-node";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *selection, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, TaskTabToolsBindTheExactCurrentBrowserSession) {
  auto list = Effect();
  list->operation_kind = mojom::TaskActionOperationKind::kTabsList;
  list->tool_name = "browser.tabs.list";
  list->canonical_intent = TaskTabIntent(5u);
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *list, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  auto activate = Effect();
  activate->action_class = mojom::PolicyActionClass::kMoveFocus;
  activate->operation_kind = mojom::TaskActionOperationKind::kTabsActivate;
  activate->tool_name = "browser.tabs.activate";
  activate->canonical_intent = TaskTabIntent(6u);
  activate->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *activate, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  auto close = Effect();
  close->action_class = mojom::PolicyActionClass::kCreateTaskTab;
  close->operation_kind = mojom::TaskActionOperationKind::kTabsClose;
  close->tool_name = "browser.tabs.close";
  close->canonical_intent = TaskTabIntent(7u);
  close->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *close, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  activate->canonical_intent = TaskTabIntent(6u, "tab-1", "old-session");
  EXPECT_TRUE(
      IsValidReadOnlyTaskPolicyEffect(*activate, kGeneration, kRevision, kNow));
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *activate, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  close->canonical_intent = TaskTabIntent(7u, "other-context");
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *close, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  close->canonical_intent = TaskTabIntent(7u, "tab-1", kBrowserSessionId, 0u);
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *close, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest,
     DomQueryWithinRemainsCanonicalButTheGrantIsDocumentScoped) {
  auto query = Effect();
  query->operation_kind = mojom::TaskActionOperationKind::kDomQuery;
  query->tool_name = "browser.dom.query";
  query->canonical_intent = DomQueryIntent(std::string_view("node-1"));
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *query, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  const auto request = BindReadOnlyTaskPolicyRequest(
      *query, Document(), "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  ASSERT_TRUE(request->scope);
  EXPECT_FALSE(request->scope->node_id);

  query->node_id = "node-1";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *query, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, ApprovalIsCopiedExactlyAndBoundsGrantLifetime) {
  auto effect = Effect();
  effect->approval = mojom::PolicyApprovalFact::New();
  effect->approval->receipt_reference = "receipt-1";
  effect->approval->proposal_digest = effect->proposal_digest;
  effect->approval->service_generation = kGeneration;
  effect->approval->expires_at_monotonic_ms = 5'000u;
  effect->approval->expires_at_utc_ms = kNowUtc + 4'000u;
  effect->approval->browser_session_id = kBrowserSessionId;
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  const auto request = BindReadOnlyTaskPolicyRequest(
      *effect, Document(), "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  EXPECT_EQ(effect->approval, request->approval);
  EXPECT_EQ(5'000u, request->expires_at_monotonic_ms);

  effect->approval->proposal_digest = std::string(64u, 'b');
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->approval = mojom::PolicyApprovalFact::New();
  effect->approval->receipt_reference = "receipt-1";
  effect->approval->proposal_digest = effect->proposal_digest;
  effect->approval->service_generation = kGeneration;
  effect->approval->expires_at_monotonic_ms = 5'000u;
  effect->approval->expires_at_utc_ms = kNowUtc;
  effect->approval->browser_session_id = kBrowserSessionId;
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect->approval->expires_at_utc_ms = kNowUtc + 4'000u;
  effect->approval->browser_session_id = "old-browser-session";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, AdmitsSameOriginInTabNavigationWithoutANode) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  effect->tool_name = "browser.navigate";
  effect->destination_address = "https://example.test/next";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  EXPECT_TRUE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect->node_id = "node-1";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, BindsDestinationScopeToTheLiveOriginForNavigate) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  effect->tool_name = "browser.navigate";
  effect->destination_address = "https://example.test/next";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  const auto request = BindReadOnlyTaskPolicyRequest(
      *effect, Document(), "profile-1", Lease(), kGeneration, kNow, kNowUtc);
  ASSERT_TRUE(request);
  ASSERT_TRUE(request->scope);
  EXPECT_FALSE(request->scope->node_id.has_value());
  ASSERT_TRUE(request->scope->destination_scope);
  EXPECT_EQ(mojom::PolicyOriginKind::kTuple,
            request->scope->destination_scope->kind);
  EXPECT_EQ("https://example.test",
            request->scope->destination_scope->serialization);
  ASSERT_TRUE(request->scope->destination_address);
  EXPECT_EQ("https://example.test/next", *request->scope->destination_address);
}

TEST(CoreTaskPolicyTest, ACrossOriginNavigateIsRefusedAsEgressNotAuthorized) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  effect->tool_name = "browser.navigate";
  effect->destination_address = "https://elsewhere.test/start";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  std::optional<mojom::TaskActionResultCode> denial;
  const auto request =
      BindReadOnlyTaskPolicyRequest(*effect, Document(), "profile-1", Lease(),
                                    kGeneration, kNow, kNowUtc, &denial);
  // Understood and refused, not malformed: the binder names the code the
  // action is settled with, so the model is told rather than the core ended.
  EXPECT_FALSE(request);
  EXPECT_EQ(mojom::TaskActionResultCode::kEgressNotAuthorized, denial);

  // Given the destination the browser resolved from the typed address — its
  // own origin — the same navigate binds, and the grant's destination scope
  // is the site it leads to rather than the one the tab is on.
  denial.reset();
  auto resolved = Document();
  resolved.destination_address = effect->destination_address;
  resolved.destination_origin = "https://elsewhere.test";
  const auto crossing =
      BindReadOnlyTaskPolicyRequest(*effect, resolved, "profile-1", Lease(),
                                    kGeneration, kNow, kNowUtc, &denial);
  ASSERT_TRUE(crossing);
  EXPECT_FALSE(denial.has_value());
  ASSERT_TRUE(crossing->scope->destination_scope);
  EXPECT_EQ("https://elsewhere.test",
            crossing->scope->destination_scope->serialization);

  // A binding with no destination at all is a malformed effect and names no
  // code.
  denial.reset();
  effect->destination_address.reset();
  EXPECT_FALSE(BindReadOnlyTaskPolicyRequest(*effect, Document(), "profile-1",
                                             Lease(), kGeneration, kNow,
                                             kNowUtc, &denial));
  EXPECT_FALSE(denial.has_value());
}

TEST(CoreTaskPolicyTest, RefusesOperationToolAndCanonicalIntentSubstitution) {
  auto effect = Effect();
  effect->tool_name = "browser.dom.query";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->operation_kind = mojom::TaskActionOperationKind::kDomQuery;
  effect->tool_name = "browser.dom.query";
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->operation_kind = mojom::TaskActionOperationKind::kDomClick;
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

  effect = Effect();
  effect->canonical_intent.clear();
  EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
      *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, TabControlsCarryNoReplayableDestination) {
  struct ControlCase {
    mojom::TaskActionOperationKind operation;
    std::string_view tool_name;
    uint8_t canonical_tag;
  };
  constexpr ControlCase kCases[] = {
      {mojom::TaskActionOperationKind::kHistoryBack, "browser.back", 2u},
      {mojom::TaskActionOperationKind::kHistoryForward, "browser.forward", 3u},
      {mojom::TaskActionOperationKind::kReload, "browser.reload", 28u},
      {mojom::TaskActionOperationKind::kStopLoading, "browser.stop_loading",
       29u},
  };
  for (const ControlCase& test_case : kCases) {
    auto effect = Effect();
    effect->action_class = mojom::PolicyActionClass::kControlTab;
    effect->operation_kind = test_case.operation;
    effect->tool_name = test_case.tool_name;
    effect->canonical_intent = TabControlIntent(test_case.canonical_tag);
    effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
    ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(
        *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));

    const auto request = BindReadOnlyTaskPolicyRequest(
        *effect, Document(), "profile-1", Lease(), kGeneration, kNow, kNowUtc);
    ASSERT_TRUE(request);
    ASSERT_TRUE(request->scope);
    EXPECT_FALSE(request->scope->destination_scope);
    EXPECT_FALSE(request->scope->destination_address);

    effect->destination_address = "https://example.test/invented";
    EXPECT_FALSE(IsValidReadOnlyTaskPolicyEffect(
        *effect, kGeneration, kRevision, kBrowserSessionId, kNow, kNowUtc));
  }
}

TEST(CoreTaskPolicyTest, RefusesBrowserDocumentFromAnotherTab) {
  auto document = Document();
  document.tab_id = "tab-other";
  EXPECT_FALSE(BindReadOnlyTaskPolicyRequest(
      *Effect(), document, "profile-1", Lease(), kGeneration, kNow, kNowUtc));
}

// The two halves of the revision-floor rule (decision 0051 section 6). They are
// written as a pair on purpose: each one alone reads as an arbitrary choice,
// and only together do they say what a revision of zero means.

TEST(CoreTaskPolicyTest, RefusesAnUnversionedNodeTargetedBrowserDocument) {
  auto effect = Effect();
  effect->node_id = "node-1";
  auto document = Document();
  document.graph_revision = 0u;
  EXPECT_FALSE(BindReadOnlyTaskPolicyRequest(
      *effect, document, "profile-1", Lease(), kGeneration, kNow, kNowUtc));
}

TEST(CoreTaskPolicyTest, BindsAnUnversionedDocumentWhenNoNodeIsTargeted) {
  // Zero is what the first request against a document has to say, because the
  // renderer owns the revision and has not reported one. Refusing it here is
  // the deadlock that removing the browser's own counter exposed: a task's
  // floor comes from an earlier observation, a task's first observation has no
  // earlier one, so a task could never read a page at all.
  auto document = Document();
  document.graph_revision = 0u;
  EXPECT_TRUE(BindReadOnlyTaskPolicyRequest(
      *Effect(), document, "profile-1", Lease(), kGeneration, kNow, kNowUtc));
}

}  // namespace
}  // namespace taffy
