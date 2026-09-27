// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_source_selection_registry.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/accepted_approval_ledger_test_support.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/browser/policy_response_validation.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

api::TaskConsentPreviewPtr Intent(const std::string& host) {
  auto intent = api::TaskConsentPreview::New();
  intent->source_hosts.push_back(host);
  intent->source_discovery_enabled = false;
  intent->new_source_cap = 0u;
  intent->provider_route = api::TaskProviderRoute::kNoModelRequired;
  return intent;
}

constexpr api::TaskTemplateId kDeterministicTemplate =
    api::TaskTemplateId::kBuildSourceTable;

uint64_t ToMonotonicMillis(base::TimeTicks value) {
  const int64_t milliseconds = value.since_origin().InMilliseconds();
  return milliseconds < 0 ? 0u : static_cast<uint64_t>(milliseconds);
}

// The host needs a profile's two authority ledgers, not a profile. That is the
// whole of what its constructor ever wanted from one, and since it is handed
// them rather than looking them up, this fixture supplies its own and the
// suite needs no Profile, no //chrome, and no Chrome test environment.
class TaskSourceSelectionRegistryTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://selected.example/path"));
    TaffyPageIntelligenceHost::AttachWithAuthority(
        web_contents(), &host_leases_, &host_capabilities_, &host_values_);
  }

  // Outlive every WebContents this fixture creates, which is the same
  // guarantee the profile-owned trio gives in production.
  ActorLeaseRegistry host_leases_;
  CapabilityLedger host_capabilities_;
  ValueReferenceVault host_values_;

  TaskSourceSelectionRegistry::WindowToken RegisterSelected(
      TaskSourceSelectionRegistry* registry) {
    const auto window = registry->RegisterProductWindow();
    EXPECT_NE(window, 0u);
    EXPECT_TRUE(registry->RegisterProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->SelectProductTab(window, 17, web_contents()));
    EXPECT_TRUE(registry->ActivateProductWindow(window));
    return window;
  }
};

TEST_F(TaskSourceSelectionRegistryTest,
       DerivesBipIdentityAndOriginFromSelectedProductTab) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);

  auto resolved = registry.ResolveConsentPreview(kDeterministicTemplate,
                                                 *Intent("selected.example"));
  ASSERT_TRUE(resolved.has_value());
  ASSERT_TRUE(*resolved);
  ASSERT_EQ((*resolved)->sources.size(), 1u);
  const service::TaskConsentSource& source = *(*resolved)->sources.front();
  ASSERT_EQ(source.source_id.size(), 32u);
  EXPECT_TRUE(std::all_of(source.source_id.begin(), source.source_id.end(),
                          [](char character) {
                            return (character >= '0' && character <= '9') ||
                                   (character >= 'a' && character <= 'f');
                          }));
  EXPECT_FALSE(source.tab_id.empty());
  EXPECT_EQ(source.normalized_origin, "https://selected.example");
  EXPECT_TRUE(registry.IsLiveIssuedSource(source));

  // A second preview in the same live browser session reuses the browser
  // source identity instead of manufacturing a parallel authority record.
  auto repeated = registry.ResolveConsentPreview(kDeterministicTemplate,
                                                 *Intent("selected.example"));
  ASSERT_TRUE(repeated.has_value());
  EXPECT_EQ((*repeated)->sources.front()->source_id, source.source_id);
  EXPECT_EQ(registry.issued_source_count_for_testing(), 1u);
}

TEST_F(TaskSourceSelectionRegistryTest,
       SelectedProductTabConsentBindsAndSpendsExactServiceGrant) {
  constexpr uint64_t kGeneration = 9u;
  constexpr uint64_t kRevision = 4u;
  constexpr char kTaskId[] = "task-selected-source";
  constexpr char kActionId[] = "action-observe-selected-source";
  constexpr char kProfileId[] = "profile-selected-source";
  constexpr char kBrowserSessionId[] = "browser-session-selected-source";

  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(kDeterministicTemplate,
                                                 *Intent("selected.example"));
  ASSERT_TRUE(resolved.has_value());
  ASSERT_EQ((*resolved)->sources.size(), 1u);
  const service::TaskConsentSource& source = *(*resolved)->sources.front();
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents());
  ASSERT_TRUE(host);
  const std::optional<DirectObservationContext> live =
      host->BuildDirectObservationContext();
  ASSERT_TRUE(live);
  ASSERT_EQ(live->tab_id, source.tab_id);
  ASSERT_EQ(live->origin, source.normalized_origin);
  // Zero, and that is the fact this test needs rather than an accident it
  // tolerates. Nobody has observed this document, the renderer owns the
  // revision and has reported none, and the browser no longer keeps a counter
  // of its own to fill the gap with (decision 0051). The effect below names no
  // node, so it is pinned to nothing and needs to be pinned to nothing — which
  // is what lets a task take the first observation that produces a revision at
  // all.
  ASSERT_EQ(live->graph_revision, 0u);

  CoreStateBindingRegistry state_registry;
  auto empty_state = service::CoreStateBrowserBindings::New();
  empty_state->service_generation = kGeneration;
  empty_state->state_sequence = 1u;
  ASSERT_EQ(state_registry.Replace(std::move(empty_state)),
            service::PendingApprovalRegistrationStatus::kRegistered);
  AcceptedApprovalLedger accepted;
  auto command = service::CoreServiceCommand::New();
  command->operation =
      service::OperationEnvelope::New("start-selected-source", kGeneration, 0u,
                                      1'000u, "start-selected-source-key");
  command->kind = service::CoreServiceCommandKind::kStartTask;
  command->start_task = service::StartTaskCommand::New();
  command->start_task->task_id = kTaskId;
  command->start_task->browser_profile_id = kProfileId;
  command->start_task->browser_session_id = kBrowserSessionId;
  command->start_task->template_id = service::TaskTemplateId::kBuildSourceTable;
  command->start_task->provider_route_id = "no_model_required";
  command->start_task->tool_allowlist = {"browser.dom.read"};
  command->start_task->initial_consent_receipt_id = "receipt-selected-source";
  command->start_task->consent_preview = (*resolved).Clone();
  ASSERT_EQ(
      accepted.StageSubmittedCommand(*command, kProfileId, kBrowserSessionId,
                                     state_registry, 1u, 1u),
      AuthoritySubmissionStage::kStaged);
  ASSERT_TRUE(CompleteStagedStorageCommitForTesting(&accepted, *command,
                                                    kRevision, 1u));

  auto state = service::CoreStateBrowserBindings::New();
  state->service_generation = kGeneration;
  state->state_sequence = 2u;
  state->task_revisions.push_back(service::TaskRevisionBinding::New(
      kTaskId, kGeneration, kRevision,
      std::vector<service::TaskControlKind>()));
  auto consent = service::AcceptedTaskConsentBinding::New();
  consent->task_id = kTaskId;
  consent->service_generation = kGeneration;
  consent->current_task_revision = kRevision;
  consent->accepted_revision = kRevision;
  consent->browser_session_id = kBrowserSessionId;
  consent->receipt_id = "receipt-selected-source";
  consent->consent_preview = (*resolved).Clone();
  state->accepted_task_consents.push_back(std::move(consent));
  ASSERT_EQ(state_registry.Replace(state.Clone()),
            service::PendingApprovalRegistrationStatus::kRegistered);
  accepted.Reconcile(state_registry, kGeneration, 1u);
  accepted.RehydrateDurableAuthority(
      *state, state_registry, kGeneration, kBrowserSessionId, 1u, 1u,
      base::BindRepeating(&TaskSourceSelectionRegistry::IssuedSourceLivenessOf,
                          base::Unretained(&registry)));

  const base::TimeTicks now = base::TimeTicks::Now();
  const uint64_t now_ms = ToMonotonicMillis(now);
  ActorLeaseRegistry leases;
  CapabilityLedger capabilities;
  leases.BeginGeneration(kProfileId, kGeneration);
  capabilities.BeginGeneration(kProfileId, kGeneration);
  ActorLeaseRequest lease_request;
  lease_request.task_id = TaskId{kTaskId};
  lease_request.tab_id = TabId{source.tab_id};
  lease_request.requested_duration_ms = 1'000u;
  lease_request.mutating = false;
  const ActorLeaseResult lease = leases.Issue(lease_request, now);
  ASSERT_EQ(lease.code, ActorLeaseResultCode::kIssued);

  auto effect = service::TaskPolicyEffect::New();
  effect->operation = service::OperationEnvelope::New(
      "policy-selected-source", kGeneration, kRevision, now_ms + 1'000u,
      "idempotency-selected-source");
  effect->effect_id = effect->operation->operation_id;
  effect->task_id = kTaskId;
  effect->action_id = kActionId;
  effect->action_class = service::PolicyActionClass::kObservePage;
  effect->operation_kind = service::TaskActionOperationKind::kDomRead;
  effect->tool_name = "browser.dom.read";
  effect->canonical_intent = {0x01u, 0x02u, 0x03u};
  effect->input = service::TaskActionInput::New();
  effect->input->kind = service::TaskActionInputKind::kNone;
  effect->proposal_digest = std::string(64u, 'a');
  effect->idempotency_key = effect->operation->idempotency_key;
  effect->tab_id = source.tab_id;
  effect->principal = service::PolicyPrincipal::New();
  effect->principal->kind = service::PolicyPrincipalKind::kAssistant;
  effect->data_classes = {service::BipSensitivity::kNotSensitive};
  effect->context_risk = service::PolicyRiskClass::kLocalRead;
  effect->control_mode = service::TaskControlMode::kShared;
  effect->policy_version = 1u;
  ASSERT_TRUE(accepted.IsTaskSourceAuthorized(*effect, source.normalized_origin,
                                              kGeneration));
  ASSERT_TRUE(IsValidReadOnlyTaskPolicyEffect(*effect, kGeneration, kRevision,
                                              kBrowserSessionId, now_ms,
                                              1'800'000'000'000u));

  const TaskPolicyDocumentBinding document{
      .tab_id = live->tab_id,
      .frame_id = live->frame_id,
      .page_epoch = live->page_epoch,
      .origin = live->origin,
      .graph_revision = live->graph_revision,
  };
  auto request =
      BindReadOnlyTaskPolicyRequest(*effect, document, kProfileId, lease,
                                    kGeneration, now_ms, 1'800'000'000'000u);
  ASSERT_TRUE(request);

  auto grant = service::MintedCapabilityGrant::New();
  grant->capability_id = "capability-selected-source";
  grant->service_generation = kGeneration;
  grant->policy_version = request->policy_version;
  grant->actor_lease_id = request->actor_lease->lease_id;
  grant->task_id = request->task_id;
  grant->action_id = request->action_id;
  grant->action_class = request->action_class;
  grant->operation_kind = request->operation_kind;
  grant->canonical_intent_digest = request->canonical_intent_digest;
  grant->principal = request->principal.Clone();
  grant->proposal_digest = request->proposal_digest;
  grant->idempotency_key = request->operation->idempotency_key;
  grant->scope = request->scope.Clone();
  grant->data_classes = request->data_classes;
  grant->effective_risk = service::PolicyRiskClass::kLocalRead;
  grant->issued_at_monotonic_ms = request->now_monotonic_ms;
  grant->expires_at_monotonic_ms = request->expires_at_monotonic_ms;
  grant->authority_subject = request->authority_subject.Clone();
  ASSERT_TRUE(PolicyGrantMatchesRequest(*grant, *request));
  ASSERT_EQ(capabilities.Register(*grant, leases, now),
            service::CapabilityRegistrationStatus::kRegistered);

  auto observation = service::PageObservationEffect::New();
  observation->task_id = kTaskId;
  observation->action_id = kActionId;
  observation->authority_subject = request->authority_subject.Clone();
  observation->tab_id = source.tab_id;
  observation->frame_id = live->frame_id;
  observation->page_epoch = live->page_epoch;
  observation->capability_id = grant->capability_id;
  observation->proposal_digest = effect->proposal_digest;
  observation->idempotency_key = effect->idempotency_key;
  observation->scope = service::ObservationScope::kCurrentDocument;
  observation->max_bytes = service::kMaxTaskObservationTotalBytes;
  observation->max_nodes = service::kMaxTaskObservationNodes;
  observation->max_text_bytes = service::kMaxTaskObservationTextBytes;
  observation->max_frames = service::kMaxTaskObservationFrames;
  observation->deadline_ms = service::kMaxTaskObservationDeadlineMs;
  observation->expected_graph_revision = live->graph_revision;
  EXPECT_EQ(
      capabilities.AdmitObservation(*observation, live->origin, leases, now),
      CapabilityAdmission::kAdmitted);
  EXPECT_EQ(
      capabilities.AdmitObservation(*observation, live->origin, leases, now),
      CapabilityAdmission::kAlreadySpent);
}

TEST_F(TaskSourceSelectionRegistryTest,
       MultiResumedWindowsRefuseInsteadOfChoosingProcessOrder) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto first = RegisterSelected(&registry);
  const auto second = registry.RegisterProductWindow();
  ASSERT_NE(second, 0u);
  ASSERT_TRUE(registry.RegisterProductTab(second, 18, web_contents()));
  ASSERT_TRUE(registry.SelectProductTab(second, 18, web_contents()));
  ASSERT_TRUE(registry.ActivateProductWindow(second));

  EXPECT_FALSE(registry
                   .ResolveConsentPreview(kDeterministicTemplate,
                                          *Intent("selected.example"))
                   .has_value());
  registry.DeactivateProductWindow(first);
  EXPECT_TRUE(registry
                  .ResolveConsentPreview(kDeterministicTemplate,
                                         *Intent("selected.example"))
                  .has_value());
}

TEST_F(TaskSourceSelectionRegistryTest,
       RotationAndTabCloseCannotLeaveAStaleSelection) {
  TaskSourceSelectionRegistry registry(browser_context());
  const auto old_window = RegisterSelected(&registry);
  auto old_resolved = registry.ResolveConsentPreview(
      kDeterministicTemplate, *Intent("selected.example"));
  ASSERT_TRUE(old_resolved.has_value());
  auto old_source = (*old_resolved)->sources.front().Clone();
  registry.UnregisterProductWindow(old_window);
  EXPECT_FALSE(registry.IsLiveIssuedSource(*old_source));
  EXPECT_EQ(registry.issued_source_count_for_testing(), 0u);
  EXPECT_FALSE(registry
                   .ResolveConsentPreview(kDeterministicTemplate,
                                          *Intent("selected.example"))
                   .has_value());

  const auto rotated_window = RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(kDeterministicTemplate,
                                                 *Intent("selected.example"));
  ASSERT_TRUE(resolved.has_value());
  auto closed_source = (*resolved)->sources.front().Clone();
  registry.UnregisterProductTab(rotated_window, 17);
  EXPECT_FALSE(registry
                   .ResolveConsentPreview(kDeterministicTemplate,
                                          *Intent("selected.example"))
                   .has_value());
  EXPECT_FALSE(registry.IsLiveIssuedSource(*closed_source));
  EXPECT_EQ(registry.issued_source_count_for_testing(), 0u);
}

TEST_F(TaskSourceSelectionRegistryTest,
       NavigationInvalidatesIssuedOriginAndStaleHostIntent) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);
  auto resolved = registry.ResolveConsentPreview(kDeterministicTemplate,
                                                 *Intent("selected.example"));
  ASSERT_TRUE(resolved.has_value());
  auto source = (*resolved)->sources.front().Clone();

  NavigateAndCommit(GURL("https://different.example/next"));
  EXPECT_FALSE(registry.IsLiveIssuedSource(*source));
  EXPECT_FALSE(registry
                   .ResolveConsentPreview(kDeterministicTemplate,
                                          *Intent("selected.example"))
                   .has_value());
  auto next = registry.ResolveConsentPreview(kDeterministicTemplate,
                                             *Intent("different.example"));
  ASSERT_TRUE(next.has_value());
  EXPECT_NE((*next)->sources.front()->source_id, source->source_id);
  EXPECT_FALSE(registry.IsLiveIssuedSource(*source));
  EXPECT_EQ(registry.issued_source_count_for_testing(), 1u);
}

TEST_F(TaskSourceSelectionRegistryTest,
       PrivateThinAndAssistantCreatedSourcesAreRefused) {
  content::TestBrowserContext private_context;
  private_context.set_is_off_the_record(true);
  TaskSourceSelectionRegistry private_registry(&private_context);
  EXPECT_EQ(private_registry.RegisterProductWindow(), 0u);

  TaskSourceSelectionRegistry unregistered(browser_context());
  const auto thin_window = unregistered.RegisterProductWindow();
  ASSERT_NE(thin_window, 0u);
  ASSERT_TRUE(unregistered.ActivateProductWindow(thin_window));
  // An eligible WebContents with no product TabModel registration is the thin
  // WebView case: the presence of a BIP host alone is never source ownership.
  EXPECT_FALSE(unregistered.SelectProductTab(thin_window, 17, web_contents()));
  EXPECT_FALSE(unregistered
                   .ResolveConsentPreview(kDeterministicTemplate,
                                          *Intent("selected.example"))
                   .has_value());

  TaskSourceSelectionRegistry assistant(browser_context());
  TaskSourceSelectionRegistry::MarkAssistantCreatedTab(web_contents());
  ASSERT_EQ(TaskSourceSelectionRegistry::BrowserOwnedProvenance(web_contents()),
            TaskSourceTabProvenance::kAssistantCreated);
  RegisterSelected(&assistant);
  EXPECT_FALSE(assistant
                   .ResolveConsentPreview(kDeterministicTemplate,
                                          *Intent("selected.example"))
                   .has_value());
}

TEST_F(TaskSourceSelectionRegistryTest,
       ModelFreeRouteIsBoundOnlyToDeterministicTemplate) {
  TaskSourceSelectionRegistry registry(browser_context());
  RegisterSelected(&registry);

  EXPECT_FALSE(
      registry
          .ResolveConsentPreview(api::TaskTemplateId::kSummarizeEvidence,
                                 *Intent("selected.example"))
          .has_value());

  auto discovery = Intent("selected.example");
  discovery->source_discovery_enabled = true;
  EXPECT_FALSE(
      registry.ResolveConsentPreview(kDeterministicTemplate, *discovery)
          .has_value());
}

TEST_F(TaskSourceSelectionRegistryTest,
       StaleNavigationIsPrunedBeforeSourceCapacityCheck) {
  TaskSourceSelectionRegistry registry(browser_context());
  registry.set_issued_source_limit_for_testing(1u);
  RegisterSelected(&registry);
  auto first = registry.ResolveConsentPreview(kDeterministicTemplate,
                                              *Intent("selected.example"));
  ASSERT_TRUE(first.has_value());
  auto old_source = (*first)->sources.front().Clone();
  ASSERT_EQ(registry.issued_source_count_for_testing(), 1u);

  NavigateAndCommit(GURL("https://next.example/path"));
  auto next = registry.ResolveConsentPreview(kDeterministicTemplate,
                                             *Intent("next.example"));
  ASSERT_TRUE(next.has_value());
  EXPECT_NE((*next)->sources.front()->source_id, old_source->source_id);
  EXPECT_FALSE(registry.IsLiveIssuedSource(*old_source));
  EXPECT_EQ(registry.issued_source_count_for_testing(), 1u);
}

}  // namespace
}  // namespace taffy
