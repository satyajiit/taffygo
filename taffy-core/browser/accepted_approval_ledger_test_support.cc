// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/accepted_approval_ledger_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "taffy/browser/core_state_binding_registry.h"
#include <vector>
#include <utility>
#include <string>
#include <optional>

#include "taffy/browser/accepted_approval_ledger.h"

namespace taffy {

namespace mojom = core_service::mojom;

bool CompleteStagedStorageCommitForTesting(
    AcceptedApprovalLedger* ledger,
    const mojom::CoreServiceCommand& command,
    uint64_t resulting_revision,
    uint64_t now_monotonic_ms) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  effect->operation->task_revision = resulting_revision;
  effect->effect_id = command.operation->operation_id;
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = command.start_task->task_id;
  effect->storage_commit->expected_revision = command.operation->task_revision;
  effect->storage_commit->resulting_revision = resulting_revision;
  if (ledger->BindStorageCommit(*effect, now_monotonic_ms) !=
      AuthorityStorageBinding::kBound) {
    return false;
  }

  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->status = mojom::EffectStatus::kCompleted;
  result->storage = mojom::StorageEffectResult::New(resulting_revision);
  return ledger->RecordStorageCompletion(*result) ==
         AuthorityStorageCompletion::kCommitted;
}


namespace accepted_approval_ledger_test {

mojom::CoreStateBrowserBindingsPtr State(std::optional<uint64_t> revision) {
  auto state = mojom::CoreStateBrowserBindings::New();
  state->service_generation = kGeneration;
  state->state_sequence = 1u;
  if (revision) {
    state->task_revisions.push_back(
        mojom::TaskRevisionBinding::New(kTaskId, kGeneration, *revision,
                                        std::vector<mojom::TaskControlKind>()));
  }
  return state;
}

void Register(CoreStateBindingRegistry* registry,
              mojom::CoreStateBrowserBindingsPtr state) {
  ASSERT_EQ(registry->Replace(std::move(state)),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
}

void RestampGeneration(mojom::CoreStateBrowserBindings* state,
                       uint64_t generation) {
  state->service_generation = generation;
  for (auto& task : state->task_revisions) {
    task->service_generation = generation;
  }
  for (auto& consent : state->accepted_task_consents) {
    consent->service_generation = generation;
  }
}

mojom::CoreServiceCommandPtr ErrandStart(bool with_source) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "start-operation", kGeneration, 0u, 25'000u, "start-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  command->start_task->task_id = kTaskId;
  command->start_task->browser_profile_id = kProfileId;
  command->start_task->browser_session_id = kBrowserSessionId;
  command->start_task->template_id = mojom::TaskTemplateId::kWebErrand;
  command->start_task->provider_route_id = "direct_user_key";
  command->start_task->tool_allowlist = {"browser.search"};
  command->start_task->initial_consent_receipt_id = "initial-receipt-1";
  command->start_task->consent_preview = mojom::TaskConsentPreview::New();
  if (with_source) {
    command->start_task->consent_preview->sources.push_back(
        mojom::TaskConsentSource::New("source-1", kTabId, kOrigin,
                                      std::nullopt));
  }
  command->start_task->consent_preview->source_discovery_enabled = true;
  command->start_task->consent_preview->new_source_cap = 4u;
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kDirectUserKey;
  return command;
}

void CommitStart(AcceptedApprovalLedger* ledger,
                 const mojom::CoreServiceCommand& command) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = command.operation.Clone();
  effect->operation->task_revision = kInitialTaskRevision;
  effect->effect_id = command.operation->operation_id;
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = kTaskId;
  effect->storage_commit->expected_revision = 0u;
  effect->storage_commit->resulting_revision = kInitialTaskRevision;
  ASSERT_EQ(ledger->BindStorageCommit(*effect, kNow),
            AuthorityStorageBinding::kBound);

  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->status = mojom::EffectStatus::kCompleted;
  result->storage = mojom::StorageEffectResult::New(kInitialTaskRevision);
  ASSERT_EQ(ledger->RecordStorageCompletion(*result),
            AuthorityStorageCompletion::kCommitted);
}

void AcceptStart(AcceptedApprovalLedger* ledger,
                 CoreStateBindingRegistry* registry,
                 const mojom::CoreServiceCommand& command) {
  ASSERT_EQ(
      ledger->StageSubmittedCommand(command, kProfileId, kBrowserSessionId,
                                    *registry, kNow, kNowUtc),
      AuthoritySubmissionStage::kStaged);
  CommitStart(ledger, command);
  Register(registry, State(kInitialTaskRevision));
  ledger->Reconcile(*registry, kGeneration, kNow);
}

void RetainErrandStart(AcceptedApprovalLedger* ledger, bool with_source) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  auto command = ErrandStart(with_source);
  AcceptStart(ledger, &registry, *command);
  ledger->ResetForServiceGenerationLoss();
}

mojom::CoreStateBrowserBindingsPtr DurableErrand(bool with_source,
                                                 uint32_t remaining_cap) {
  auto state = State(kCommittedRevision);
  auto consent = mojom::AcceptedTaskConsentBinding::New();
  consent->task_id = kTaskId;
  consent->service_generation = kGeneration;
  consent->current_task_revision = kCommittedRevision;
  consent->accepted_revision = kInitialTaskRevision;
  consent->browser_session_id = kBrowserSessionId;
  consent->receipt_id = "initial-receipt-1";
  consent->consent_preview = mojom::TaskConsentPreview::New();
  if (with_source) {
    consent->consent_preview->sources.push_back(mojom::TaskConsentSource::New(
        "source-1", kTabId, kOrigin, std::nullopt));
  }
  consent->consent_preview->source_discovery_enabled = true;
  consent->consent_preview->new_source_cap = remaining_cap;
  consent->consent_preview->provider_route =
      mojom::TaskProviderRoute::kDirectUserKey;
  state->accepted_task_consents.push_back(std::move(consent));
  return state;
}

}  // namespace accepted_approval_ledger_test

}  // namespace taffy
