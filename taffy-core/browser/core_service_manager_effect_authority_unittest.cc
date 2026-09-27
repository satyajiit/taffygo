// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kResultingRevision = 3u;
constexpr uint64_t kPausingRevision = 4u;
constexpr uint64_t kPausedRevision = 5u;
constexpr uint64_t kResumedRevision = 6u;
constexpr char kProfileId[] = "profile-1";
constexpr char kTaskId[] = "task-1";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::CoreStateBrowserBindingsPtr EmptyBindings() {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  return bindings;
}

mojom::CoreStateUpdatePtr PublishedState(uint64_t sequence) {
  auto state = mojom::CoreStateUpdate::New();
  state->service_generation = kGeneration;
  state->sequence = sequence;
  state->core_status_schema_version = 1u;
  state->payload = {1u};
  return state;
}

mojom::CoreServiceCommandPtr StartTask(const std::string& browser_session_id,
                                       uint64_t deadline) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "start-operation", kGeneration, 0u, deadline, "start-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  command->start_task->task_id = kTaskId;
  command->start_task->browser_profile_id = kProfileId;
  command->start_task->browser_session_id = browser_session_id;
  command->start_task->template_id = mojom::TaskTemplateId::kBuildSourceTable;
  command->start_task->provider_route_id = "no_model_required";
  command->start_task->tool_allowlist = {"browser.dom.read"};
  command->start_task->initial_consent_receipt_id = "initial-receipt-1";
  command->start_task->consent_preview = mojom::TaskConsentPreview::New();
  command->start_task->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-1", "tab-1", "https://example.test",
                                    std::nullopt));
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  return command;
}

mojom::EffectEnvelopePtr MismatchedStorageEffect(
    const mojom::CoreServiceCommand& command) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  effect->operation->task_revision = kResultingRevision;
  effect->effect_id = command.operation->operation_id;
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = "different-task";
  effect->storage_commit->expected_revision = 0u;
  effect->storage_commit->resulting_revision = kResultingRevision;
  effect->storage_commit->transaction_batch = {1u};
  effect->storage_commit->task_id_seed.assign(32u, 0u);
  effect->storage_commit->task_id_seed.front() = 1u;
  return effect;
}

mojom::EffectEnvelopePtr ExactStorageEffect(
    const mojom::CoreServiceCommand& command,
    uint64_t resulting_revision) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  effect->operation->task_revision = resulting_revision;
  effect->effect_id = command.operation->operation_id;
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = kTaskId;
  effect->storage_commit->expected_revision = command.operation->task_revision;
  effect->storage_commit->resulting_revision = resulting_revision;
  effect->storage_commit->transaction_batch = {1u};
  effect->storage_commit->task_id_seed.assign(32u, 0u);
  effect->storage_commit->task_id_seed.front() = 1u;
  return effect;
}

mojom::EffectResultPtr SuccessfulStorageResult(
    const mojom::EffectEnvelope& effect) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->status = mojom::EffectStatus::kCompleted;
  result->storage = mojom::StorageEffectResult::New(
      effect.storage_commit->resulting_revision);
  return result;
}

mojom::CoreStateBrowserBindingsPtr TaskBindings(
    uint64_t state_sequence,
    uint64_t task_revision,
    const std::string& browser_session_id,
    bool accepted_consent,
    std::vector<mojom::TaskControlKind> controls = {},
    std::optional<mojom::TaskSettlementKind> settlement = std::nullopt) {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = state_sequence;
  bindings->task_revisions.push_back(mojom::TaskRevisionBinding::New(
      kTaskId, kGeneration, task_revision, std::move(controls)));
  if (accepted_consent) {
    auto consent = mojom::AcceptedTaskConsentBinding::New();
    consent->task_id = kTaskId;
    consent->service_generation = kGeneration;
    consent->current_task_revision = task_revision;
    consent->accepted_revision = kResultingRevision;
    consent->browser_session_id = browser_session_id;
    consent->receipt_id = "initial-receipt-1";
    consent->consent_preview = StartTask(browser_session_id, 30'000u)
                                   ->start_task->consent_preview.Clone();
    bindings->accepted_task_consents.push_back(std::move(consent));
  }
  if (settlement) {
    bindings->task_settlements.push_back(mojom::TaskSettlementBinding::New(
        kTaskId, kGeneration, task_revision, *settlement));
  }
  return bindings;
}

mojom::CoreServiceCommandPtr ResumeTask(uint64_t deadline) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "resume-operation", kGeneration, kPausedRevision, deadline,
      "resume-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kResumeTask;
  command->resume_task = mojom::ResumeTaskCommand::New(kTaskId, "resume-trace");
  return command;
}

TEST(CoreServiceManagerEffectAuthorityTest,
     InvalidStagedOperationNeverReachesStorageAdapter) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  int storage_dispatches = 0;
  CoreEffectBroker::Handlers handlers;
  handlers.storage = base::BindLambdaForTesting(
      [&](mojom::EffectEnvelopePtr effect,
          CoreEffectBroker::CompletionCallback done) {
        ++storage_dispatches;
        std::move(done).Run(SuccessfulStorageResult(*effect));
      });
  test::QuietManagerTail tail;
  auto manager = tail.MakeManager(
      context.get(), /*storage_broker=*/nullptr, std::move(tools), observation,
      std::make_unique<CoreEffectBroker>(std::move(handlers)));
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager,
                                                            kProfileId);
  CoreServiceManagerTaskEffectTestPeer::ActivateEffectBroker(*manager);
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, EmptyBindings()),
            mojom::PendingApprovalRegistrationStatus::kRegistered);

  const uint64_t now = NowMonotonicMillis();
  auto command = StartTask(manager->browser_session_id(), now + 30'000u);
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::StageSubmittedCommand(
                *manager, *command, now, NowUtcMillis()),
            AuthoritySubmissionStage::kStaged);
  const uint64_t late_before = manager->late_reply_count_for_testing();

  CoreServiceManagerTaskEffectTestPeer::EmitEffect(
      *manager, MismatchedStorageEffect(*command));

  EXPECT_EQ(storage_dispatches, 0);
  EXPECT_EQ(manager->late_reply_count_for_testing(), late_before + 1u);
  manager->Shutdown();
  static_cast<void>(session);
}

TEST(CoreServiceManagerEffectAuthorityTest,
     PauseRegistrationKeepsConsentInertUntilDurableResume) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  CoreEffectBroker::Handlers handlers;
  handlers.storage =
      base::BindLambdaForTesting([](mojom::EffectEnvelopePtr effect,
                                    CoreEffectBroker::CompletionCallback done) {
        std::move(done).Run(SuccessfulStorageResult(*effect));
      });
  test::QuietManagerTail tail;
  auto manager = tail.MakeManager(
      context.get(), /*storage_broker=*/nullptr, std::move(tools), observation,
      std::make_unique<CoreEffectBroker>(std::move(handlers)));
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager,
                                                            kProfileId);
  CoreServiceManagerTaskEffectTestPeer::ActivateEffectBroker(*manager);
  CoreServiceManagerTaskEffectTestPeer::AllowAllTaskSources(*manager);
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, EmptyBindings()),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, PublishedState(1u));

  const uint64_t now = NowMonotonicMillis();
  const std::string browser_session_id = manager->browser_session_id();
  auto start = StartTask(browser_session_id, now + 30'000u);
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::StageSubmittedCommand(
                *manager, *start, now, NowUtcMillis()),
            AuthoritySubmissionStage::kStaged);
  CoreServiceManagerTaskEffectTestPeer::EmitEffect(
      *manager, ExactStorageEffect(*start, kResultingRevision));
  ASSERT_EQ(
      CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
          *manager, TaskBindings(2u, kResultingRevision, browser_session_id,
                                 /*accepted_consent=*/true)),
      mojom::PendingApprovalRegistrationStatus::kRegistered);
  ASSERT_EQ(
      CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*manager), 1u);
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, PublishedState(2u));

  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, TaskBindings(3u, kPausingRevision, browser_session_id,
                                       /*accepted_consent=*/false, {},
                                       mojom::TaskSettlementKind::kPause)),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*manager), 0u);
  ASSERT_EQ(
      CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*manager),
      1u);
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, PublishedState(3u));

  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, TaskBindings(4u, kPausedRevision, browser_session_id,
                                       /*accepted_consent=*/false,
                                       {mojom::TaskControlKind::kResume,
                                        mojom::TaskControlKind::kStop})),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
  ASSERT_EQ(
      CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*manager),
      1u);
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, PublishedState(4u));

  auto resume = ResumeTask(now + 30'000u);
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::StageSubmittedCommand(
                *manager, *resume, now, NowUtcMillis()),
            AuthoritySubmissionStage::kStaged);
  CoreServiceManagerTaskEffectTestPeer::EmitEffect(
      *manager, ExactStorageEffect(*resume, kResumedRevision));
  ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                *manager, TaskBindings(5u, kResumedRevision, browser_session_id,
                                       /*accepted_consent=*/true)),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::AcceptedConsentCount(*manager), 1u);
  EXPECT_EQ(
      CoreServiceManagerTaskEffectTestPeer::SuspendedConsentCount(*manager),
      0u);
  CoreServiceManagerTaskEffectTestPeer::Publish(*manager, PublishedState(5u));

  manager->Shutdown();
  static_cast<void>(session);
}

}  // namespace
}  // namespace taffy
