// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/web_contents_tester.h"
#include "net/base/net_errors.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/accepted_approval_ledger_test_support.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry_actions_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kGeneration = 9u;
constexpr uint64_t kAcceptedRevision = 4u;
constexpr char kTaskId[] = "task-errand-source";
constexpr char kProfileId[] = "profile-errand-source";
constexpr char kSessionId[] = "session-errand-source";

api::TaskConsentPreviewPtr Intent(bool errand, bool saved_replay = false) {
  auto intent = api::TaskConsentPreview::New();
  intent->source_hosts = {"user.example"};
  intent->provider_route = errand && !saved_replay
                               ? api::TaskProviderRoute::kDirectUserKey
                               : api::TaskProviderRoute::kNoModelRequired;
  intent->source_discovery_enabled = errand && !saved_replay;
  intent->new_source_cap = intent->source_discovery_enabled ? 8u : 0u;
  return intent;
}

class TaskSourceSelectionRegistryErrandTest
    : public task_source_selection_test::
          TaskSourceSelectionRegistryActionsTest {
 protected:
  TaskSourceSelectionRegistry::WindowToken RegisterSelected(
      TaskSourceSelectionRegistry* registry) {
    const auto window = RegisterWindow(registry);
    EXPECT_TRUE(registry->RegisterProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->SelectProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->ActivateProductWindow(window));
    return window;
  }

  // Follow the shipping storage acknowledgment and publication path. The
  // browser must not lose the standing receipt when the next publication
  // observes an allowed same-origin navigation or a person's hand-back.
  void Accept(const service::TaskConsentPreview& preview, bool errand) {
    auto empty = service::CoreStateBrowserBindings::New();
    empty->service_generation = kGeneration;
    empty->state_sequence = 1u;
    ASSERT_EQ(bindings_.Replace(std::move(empty)),
              service::PendingApprovalRegistrationStatus::kRegistered);
    auto command = service::CoreServiceCommand::New();
    command->operation = service::OperationEnvelope::New(
        "start-errand", kGeneration, 0u, 1'000u, "start-errand-key");
    command->kind = service::CoreServiceCommandKind::kStartTask;
    command->start_task = service::StartTaskCommand::New();
    auto& start = *command->start_task;
    start.task_id = kTaskId;
    start.browser_profile_id = kProfileId;
    start.browser_session_id = kSessionId;
    start.template_id = errand ? service::TaskTemplateId::kWebErrand
                               : service::TaskTemplateId::kBuildSourceTable;
    start.provider_route_id = errand ? "direct_user_key" : "no_model_required";
    start.tool_allowlist = {"browser.dom.read"};
    start.initial_consent_receipt_id = "receipt-errand";
    start.consent_preview = preview.Clone();
    ASSERT_EQ(accepted_.StageSubmittedCommand(*command, kProfileId, kSessionId,
                                              bindings_, 1u, 1u),
              AuthoritySubmissionStage::kStaged);
    ASSERT_TRUE(CompleteStagedStorageCommitForTesting(&accepted_, *command,
                                                      kAcceptedRevision, 1u));
  }

  void Publish(TaskSourceSelectionRegistry* registry,
               const service::TaskConsentPreview& preview,
               uint64_t revision,
               uint64_t sequence) {
    auto state = service::CoreStateBrowserBindings::New();
    state->service_generation = kGeneration;
    state->state_sequence = sequence;
    state->task_revisions.push_back(service::TaskRevisionBinding::New(
        kTaskId, kGeneration, revision,
        std::vector<service::TaskControlKind>()));
    auto consent = service::AcceptedTaskConsentBinding::New();
    consent->task_id = kTaskId;
    consent->service_generation = kGeneration;
    consent->current_task_revision = revision;
    consent->accepted_revision = kAcceptedRevision;
    consent->browser_session_id = kSessionId;
    consent->receipt_id = "receipt-errand";
    consent->consent_preview = preview.Clone();
    state->accepted_task_consents.push_back(std::move(consent));
    ASSERT_EQ(bindings_.Replace(state.Clone()),
              service::PendingApprovalRegistrationStatus::kRegistered);
    accepted_.Reconcile(bindings_, kGeneration, 1u);
    accepted_.RehydrateDurableAuthority(
        *state, bindings_, kGeneration, kSessionId, 1u, 1u,
        base::BindRepeating(&TaskSourceSelectionRegistry::IssuedSourceLivenessOf,
                            base::Unretained(registry)));
  }

  bool IsAuthorized(const service::TaskConsentSource& source,
                    uint64_t revision) {
    auto operation = service::OperationEnvelope::New(
        "fresh-read", kGeneration, revision, 1'000u, "fresh-read-key");
    return accepted_.IsTaskSourceAuthorized(kTaskId, source.tab_id, *operation,
                                            source.normalized_origin,
                                            kGeneration);
  }

  CoreStateBindingRegistry bindings_;
  AcceptedApprovalLedger accepted_;
};

TEST_F(TaskSourceSelectionRegistryErrandTest,
       ErrandConsentSurvivesSameOriginNavigationAndPublication) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(
      api::TaskTemplateId::kWebErrand, *Intent(true));
  ASSERT_TRUE(resolved);
  const auto& preview = **resolved;
  ASSERT_EQ(preview.sources.size(), 1u);
  const auto& source = *preview.sources.front();
  EXPECT_FALSE(source.canonical_locator);
  Accept(preview, true);
  Publish(&registry, preview, kAcceptedRevision, 2u);
  ASSERT_TRUE(IsAuthorized(source, kAcceptedRevision));

  NavigateAndCommit(GURL("https://user.example/download-document"));
  EXPECT_TRUE(registry.IsLiveIssuedSource(source));
  Publish(&registry, preview, kAcceptedRevision + 1u, 3u);
  EXPECT_TRUE(IsAuthorized(source, kAcceptedRevision + 1u));

  NavigateAndCommit(GURL("https://user.example/verification?session=fixture"));
  auto repeated = registry.ResolveConsentPreview(
      api::TaskTemplateId::kWebErrand, *Intent(true));
  ASSERT_TRUE(repeated);
  EXPECT_EQ((*repeated)->sources.front()->source_id, source.source_id);
  Publish(&registry, preview, kAcceptedRevision + 2u, 4u);
  EXPECT_TRUE(IsAuthorized(source, kAcceptedRevision + 2u));
}

TEST_F(TaskSourceSelectionRegistryErrandTest,
       SavedReplayConsentSurvivesSameOriginNavigation) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved =
      registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand,
                                     *Intent(true, true), "opaque-page-offer");
  ASSERT_TRUE(resolved);
  const auto& source = *(*resolved)->sources.front();
  EXPECT_FALSE(source.canonical_locator);
  NavigateAndCommit(GURL("https://user.example/download-document"));
  EXPECT_TRUE(registry.IsLiveIssuedSource(source));
}

TEST_F(TaskSourceSelectionRegistryErrandTest,
       ErrandConsentRefusesManualCrossOriginNavigation) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(
      api::TaskTemplateId::kWebErrand, *Intent(true));
  ASSERT_TRUE(resolved);
  Accept(**resolved, true);
  Publish(&registry, **resolved, kAcceptedRevision, 2u);
  const auto& source = *(*resolved)->sources.front();
  ASSERT_TRUE(IsAuthorized(source, kAcceptedRevision));
  NavigateAndCommit(GURL("https://other.example/download-document"));
  EXPECT_FALSE(registry.IsLiveIssuedSource(source));
  Publish(&registry, **resolved, kAcceptedRevision + 1u, 3u);
  EXPECT_FALSE(IsAuthorized(source, kAcceptedRevision + 1u));
}

TEST_F(TaskSourceSelectionRegistryErrandTest,
       ErrandConsentRefusesReplacedProductTab) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto window = RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(
      api::TaskTemplateId::kWebErrand, *Intent(true));
  ASSERT_TRUE(resolved);
  auto replacement = CreateTestWebContents();
  content::WebContentsTester::For(replacement.get())
      ->NavigateAndCommit(GURL("https://user.example/"));
  AttachHost(replacement.get());
  ASSERT_TRUE(registry.RegisterProductTab(window, 17, replacement.get()));
  ASSERT_TRUE(registry.SelectProductTab(window, 17, replacement.get()));
  EXPECT_FALSE(registry.IsLiveIssuedSource(*(*resolved)->sources.front()));
}

// Decision 0183, and the test that says the rule fires against a real document
// rather than against a fake callback. A host that does not resolve leaves
// Chromium's own error page committed in the tab: the tab is there, it is
// still the task's, and its origin is opaque, so there is nothing to compare
// the source against. That is not the same fact as the tab having moved, and
// the two tests either side of this one are the ones that say so.
//
// The last two lines are the whole defect. Rehydration used to refuse on this
// document and destroy the record, and because the candidate the next pass
// looks for lives in the map that pass rebuilds, the task never got it back —
// a phone logged one `source-not-live` and then dozens of `candidate=0`, with
// every later proposal refused including every search.
TEST_F(TaskSourceSelectionRegistryErrandTest,
       ErrandConsentSurvivesAnAddressThatDoesNotResolve) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(
      api::TaskTemplateId::kWebErrand, *Intent(true));
  ASSERT_TRUE(resolved);
  Accept(**resolved, true);
  Publish(&registry, **resolved, kAcceptedRevision, 2u);
  const auto& source = *(*resolved)->sources.front();
  ASSERT_TRUE(IsAuthorized(source, kAcceptedRevision));

  // `WebContentsTester::NavigateAndFail` calls `Fail` and stops there, which
  // leaves the tab on the document it was already showing — the first version
  // of this test asserted against `user.example` and learned nothing. The
  // error document has to be committed explicitly.
  auto navigation = content::NavigationSimulator::CreateBrowserInitiated(
      GURL("https://does-not-resolve.test/eaadhaar"), web_contents());
  ASSERT_TRUE(navigation);
  navigation->Fail(net::ERR_NAME_NOT_RESOLVED);
  navigation->CommitErrorPage();

  content::RenderFrameHost* frame = web_contents()->GetPrimaryMainFrame();
  ASSERT_TRUE(frame);
  ASSERT_TRUE(frame->GetLastCommittedOrigin().opaque())
      << "the harness did not commit an error document; committed="
      << frame->GetLastCommittedURL();

  // Not live, and not gone either — and the middle answer is only reachable if
  // the error document still carries an actionable observation endpoint, which
  // is what `ResolveTaskDepartureDocument` requires. Asserted rather than
  // assumed: a retired endpoint would answer `kGone` here and silently turn
  // the whole rule back off. The facts that tell those apart are printed from
  // the assertion, because reading them off a device otherwise costs a build.
  EXPECT_FALSE(registry.IsLiveIssuedSource(source));
  EXPECT_EQ(registry.IssuedSourceLivenessOf(source),
            IssuedSourceLiveness::kTabHasNoDocumentOfItsOwn)
      << "committed=" << frame->GetLastCommittedURL()
      << " opaque=" << frame->GetLastCommittedOrigin().opaque()
      << " frame_live=" << frame->IsRenderFrameLive() << " departure="
      << ResolveTaskDepartureDocument(browser_context(), source.tab_id)
             .has_value();

  Publish(&registry, **resolved, kAcceptedRevision + 1u, 3u);
  EXPECT_TRUE(IsAuthorized(source, kAcceptedRevision + 1u));
  Publish(&registry, **resolved, kAcceptedRevision + 2u, 4u);
  EXPECT_TRUE(IsAuthorized(source, kAcceptedRevision + 2u));
}

// The bound, against the same live registry rather than a fake: a tab a person
// carried to another site answers `kGone`, so it takes the clause that still
// destroys the record. Without this beside the test above, "not live" and "no
// site of its own" are indistinguishable again.
TEST_F(TaskSourceSelectionRegistryErrandTest,
       ATabTakenToAnotherSiteIsGoneAndNotMerelyUnobservable) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(
      api::TaskTemplateId::kWebErrand, *Intent(true));
  ASSERT_TRUE(resolved);
  Accept(**resolved, true);
  Publish(&registry, **resolved, kAcceptedRevision, 2u);
  const auto& source = *(*resolved)->sources.front();
  ASSERT_TRUE(IsAuthorized(source, kAcceptedRevision));

  NavigateAndCommit(GURL("https://other.example/download-document"));
  EXPECT_EQ(registry.IssuedSourceLivenessOf(source),
            IssuedSourceLiveness::kGone);
}

TEST_F(TaskSourceSelectionRegistryErrandTest,
       ResearchConsentRemainsBoundToReviewedPage) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(
      api::TaskTemplateId::kBuildSourceTable, *Intent(false));
  ASSERT_TRUE(resolved);
  const auto& source = *(*resolved)->sources.front();
  EXPECT_EQ(source.canonical_locator, "https://user.example/");
  Accept(**resolved, false);
  Publish(&registry, **resolved, kAcceptedRevision, 2u);
  ASSERT_TRUE(IsAuthorized(source, kAcceptedRevision));
  NavigateAndCommit(GURL("https://user.example/other-product"));
  EXPECT_FALSE(registry.IsLiveIssuedSource(source));
  Publish(&registry, **resolved, kAcceptedRevision + 1u, 3u);
  EXPECT_FALSE(IsAuthorized(source, kAcceptedRevision + 1u));
}

TEST_F(TaskSourceSelectionRegistryErrandTest,
       OmittedPrivateLocatorCannotAliasResearchAndErrandSources) {
  NavigateAndCommit(GURL("https://user.example/private?session=fixture"));
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto research = registry.ResolveConsentPreview(
      api::TaskTemplateId::kBuildSourceTable, *Intent(false));
  auto errand = registry.ResolveConsentPreview(api::TaskTemplateId::kWebErrand,
                                               *Intent(true));
  ASSERT_TRUE(research);
  ASSERT_TRUE(errand);
  EXPECT_FALSE((*research)->sources.front()->canonical_locator);
  EXPECT_FALSE((*errand)->sources.front()->canonical_locator);
  EXPECT_NE((*research)->sources.front()->source_id,
            (*errand)->sources.front()->source_id);
}

}  // namespace
}  // namespace taffy
