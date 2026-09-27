// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager_task_discovery_test_support.h"

#include <optional>
#include <utility>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_task_policy_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {

namespace discovery_test {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

service_mojom::CoreStateBrowserBindingsPtr EmptyBindings() {
  auto bindings = service_mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  return bindings;
}

service_mojom::CoreStateUpdatePtr PublishedState(uint64_t sequence) {
  auto state = service_mojom::CoreStateUpdate::New();
  state->service_generation = kGeneration;
  state->sequence = sequence;
  state->core_status_schema_version = 1u;
  state->payload = {1u};
  return state;
}

// A zero-source errand: discovery on, no source named, so the reducer's only
// way forward is the bootstrap under test. The consent it carries is the one
// shape the ledger admits for that template at a start.
service_mojom::CoreServiceCommandPtr StartErrand(
    const std::string& browser_session_id,
    uint64_t deadline) {
  auto command = service_mojom::CoreServiceCommand::New();
  command->operation = service_mojom::OperationEnvelope::New(
      "start-operation", kGeneration, 0u, deadline, "start-idempotency");
  command->kind = service_mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = service_mojom::StartTaskCommand::New();
  command->start_task->task_id = kTaskId;
  command->start_task->browser_profile_id = kProfileId;
  command->start_task->browser_session_id = browser_session_id;
  command->start_task->template_id = service_mojom::TaskTemplateId::kWebErrand;
  command->start_task->provider_route_id = "direct_user_key";
  command->start_task->tool_allowlist = {"browser.dom.read"};
  command->start_task->initial_consent_receipt_id = "initial-receipt-1";
  command->start_task->consent_preview = service_mojom::TaskConsentPreview::New(
      std::vector<service_mojom::TaskConsentSourcePtr>(), true, kNewSourceCap,
      service_mojom::TaskProviderRoute::kDirectUserKey);
  return command;
}

service_mojom::EffectEnvelopePtr StartStorageEffect(
    const service_mojom::CoreServiceCommand& command) {
  auto effect = service_mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  effect->operation->task_revision = kAcceptedRevision;
  effect->effect_id = command.operation->operation_id;
  effect->kind = service_mojom::EffectKind::kStorageCommit;
  effect->retry_class = service_mojom::RetryClass::kIdempotent;
  effect->storage_commit = service_mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      service_mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = kTaskId;
  effect->storage_commit->expected_revision = 0u;
  effect->storage_commit->resulting_revision = kAcceptedRevision;
  effect->storage_commit->transaction_batch = {1u};
  effect->storage_commit->task_id_seed.assign(32u, 0u);
  effect->storage_commit->task_id_seed.front() = 1u;
  return effect;
}

service_mojom::EffectResultPtr SuccessfulStorageResult(
    const service_mojom::EffectEnvelope& effect) {
  auto result = service_mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->kind = service_mojom::EffectKind::kStorageCommit;
  result->status = service_mojom::EffectStatus::kCompleted;
  result->storage = service_mojom::StorageEffectResult::New(
      effect.storage_commit->resulting_revision);
  return result;
}

// The committed errand, as the core's next state binds it.
service_mojom::CoreStateBrowserBindingsPtr ErrandBindings(
    const std::string& browser_session_id) {
  auto bindings = service_mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 2u;
  bindings->task_revisions.push_back(service_mojom::TaskRevisionBinding::New(
      kTaskId, kGeneration, kAcceptedRevision,
      std::vector<service_mojom::TaskControlKind>()));
  auto consent = service_mojom::AcceptedTaskConsentBinding::New();
  consent->task_id = kTaskId;
  consent->service_generation = kGeneration;
  consent->current_task_revision = kAcceptedRevision;
  consent->accepted_revision = kAcceptedRevision;
  consent->browser_session_id = browser_session_id;
  consent->receipt_id = "initial-receipt-1";
  consent->consent_preview = StartErrand(browser_session_id, 30'000u)
                                 ->start_task->consent_preview.Clone();
  bindings->accepted_task_consents.push_back(std::move(consent));
  return bindings;
}

// A search from the discovery tab, as the reducer proposes it: the one shape
// the validator admits for a zero-source errand, carrying the authority fact
// the browser's ledger must recognise.
service_mojom::TaskPolicyEffectPtr DiscoveryPolicyEffect(
    uint64_t deadline,
    const std::string& browser_session_id,
    uint32_t remaining_new_source_cap) {
  auto effect = service_mojom::TaskPolicyEffect::New();
  effect->operation = service_mojom::OperationEnvelope::New(
      "policy-operation", kGeneration, kAcceptedRevision, deadline,
      "policy-idempotency");
  effect->effect_id = "policy-effect";
  effect->task_id = kTaskId;
  effect->action_id = "action-1";
  effect->action_class = service_mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = service_mojom::TaskActionOperationKind::kSearch;
  effect->tool_name = "browser.search";
  effect->transient_search_query = "official site";
  effect->canonical_intent =
      core_task_policy_test::SearchIntent(*effect->transient_search_query);
  effect->input = service_mojom::TaskActionInput::New();
  effect->input->kind = service_mojom::TaskActionInputKind::kNone;
  effect->proposal_digest = std::string(64u, 'a');
  effect->idempotency_key = effect->operation->idempotency_key;
  effect->tab_id = "tab-1";
  effect->principal = service_mojom::PolicyPrincipal::New(
      service_mojom::PolicyPrincipalKind::kAssistant, std::nullopt);
  effect->data_classes = {service_mojom::BipSensitivity::kNotSensitive};
  effect->context_risk = service_mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->control_mode = service_mojom::TaskControlMode::kAssistant;
  effect->policy_version = 3u;
  effect->discovery = service_mojom::TaskDiscoveryAuthorityFact::New(
      "tab-1", browser_session_id, remaining_new_source_cap);
  return effect;
}

service_mojom::TaskEffectBindingPtr DiscoveryEffect(
    uint64_t deadline,
    const std::string& browser_session_id) {
  auto effect = service_mojom::TaskEffectBinding::New();
  effect->operation = service_mojom::OperationEnvelope::New(
      "discovery-operation", kGeneration, kAcceptedRevision, deadline,
      "discovery-idempotency");
  effect->effect_id = "discovery-effect";
  effect->task_id = kTaskId;
  effect->ordinal = 0u;
  effect->kind = service_mojom::TaskReducerEffectKind::kPrepareDiscoveryTab;
  effect->discovery_bootstrap =
      service_mojom::TaskDiscoveryBootstrapEffect::New(browser_session_id,
                                                       kNewSourceCap);
  return effect;
}

// A navigate that has already been dispatched and verified, frozen against
// the document it was proposed from. `destination_origin` differing from the
// frozen document's origin is what makes it a move that leaves the origin it
// started on, which is the only condition the landing branch reads.
service_mojom::TaskEffectBindingPtr LandedNavigateBinding(
    const std::string& document_origin,
    const std::string& destination_origin) {
  auto binding = service_mojom::TaskEffectBinding::New();
  binding->operation = service_mojom::OperationEnvelope::New(
      "navigate-operation", kGeneration, kAcceptedRevision, 30'000u,
      "navigate-key");
  binding->effect_id = "effect-navigate-1";
  binding->task_id = kTaskId;
  binding->kind = service_mojom::TaskReducerEffectKind::kDispatchAction;
  binding->action = service_mojom::TaskActionEffect::New();
  auto& action = *binding->action;
  action.action_id = "action-navigate-1";
  action.proposal_digest = std::string(64u, 'a');
  action.idempotency_key = "navigate-key";
  action.capability_id = "capability-navigate-1";
  action.dispatch_id = "dispatch-navigate-1";
  action.document = service_mojom::TaskFrozenDocument::New(
      "frame-1", "epoch-1", 1u, document_origin, std::nullopt);
  action.executable = service_mojom::TaskExecutableAction::New();
  action.executable->action_class = service_mojom::PolicyActionClass::kOpenLink;
  action.executable->operation_kind =
      service_mojom::TaskActionOperationKind::kNavigate;
  action.executable->tool_name = "browser.navigate";
  action.executable->tab_id = "tab-1";
  action.executable->destination_address = destination_origin + "/";
  action.executable->destination_origin = destination_origin;
  action.executable->input = service_mojom::TaskActionInput::New();
  action.executable->input->kind = service_mojom::TaskActionInputKind::kNone;
  return binding;
}

ActionResult VerifiedResultFor(const service_mojom::TaskEffectBinding& binding) {
  ActionResult result;
  result.action_id = ActionId(binding.action->action_id);
  result.dispatch_id = DispatchId(binding.action->dispatch_id);
  result.result_code = ActionResultCode::kVerified;
  result.dispatched = true;
  return result;
}

CoreServiceManagerTaskDiscoveryTest::CoreServiceManagerTaskDiscoveryTest() =
    default;

CoreServiceManagerTaskDiscoveryTest::~CoreServiceManagerTaskDiscoveryTest() =
    default;

void CoreServiceManagerTaskDiscoveryTest::SetUp() {
  context_ = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context_.get());
  CoreEffectBroker::Handlers handlers;
  handlers.storage = base::BindLambdaForTesting(
      [](service_mojom::EffectEnvelopePtr effect,
         CoreEffectBroker::CompletionCallback done) {
        std::move(done).Run(SuccessfulStorageResult(*effect));
      });
  manager_ = tail_.MakeManager(
      context_.get(), /*storage_broker=*/nullptr, std::move(tools),
      std::move(observation),
      std::make_unique<CoreEffectBroker>(std::move(handlers)));
  session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_,
                                                            kProfileId);
  CoreServiceManagerTaskEffectTestPeer::ActivateEffectBroker(*manager_);
  CoreServiceManagerTaskEffectTestPeer::AllowAllTaskSources(*manager_);
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager_, EmptyBindings()),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager_,
                                                PublishedState(1u));

  const uint64_t now = NowMonotonicMillis();
  const std::string browser_session_id = manager_->browser_session_id();
  auto start = StartErrand(browser_session_id, now + 30'000u);
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::StageSubmittedCommand(
                *manager_, *start, now, NowUtcMillis()),
            AuthoritySubmissionStage::kStaged);
  CoreServiceManagerTaskEffectTestPeer::EmitEffect(
      *manager_, StartStorageEffect(*start));
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager_, ErrandBindings(browser_session_id)),
            service_mojom::PendingApprovalRegistrationStatus::kRegistered);
  ASSERT_EQ(
      CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*manager_),
      1u);
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager_,
                                                PublishedState(2u));
}

void CoreServiceManagerTaskDiscoveryTest::TearDown() {
  manager_->Shutdown();
  manager_.reset();
}

void CoreServiceManagerTaskDiscoveryTest::ExecuteBootstrap() {
  Execute(DiscoveryEffect(NowMonotonicMillis() + 30'000u,
                          manager_->browser_session_id()));
}

void CoreServiceManagerTaskDiscoveryTest::Execute(
    service_mojom::TaskEffectBindingPtr effect) {
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager_, std::move(effect),
      base::BindLambdaForTesting(
          [this](service_mojom::TaskEffectCompletionPtr result) {
            ASSERT_TRUE(result);
            completions_.push_back(result->status);
          }));
}

size_t CoreServiceManagerTaskDiscoveryTest::DeferredCount() const {
  return CoreServiceManagerTaskEffectTestPeer::
      DeferredDiscoveryBootstrapCount(*manager_);
}

size_t CoreServiceManagerTaskDiscoveryTest::PendingCount() const {
  return CoreServiceManagerTaskEffectTestPeer::PendingHostTaskEffectCount(
      *manager_);
}

}  // namespace discovery_test

}  // namespace taffy
