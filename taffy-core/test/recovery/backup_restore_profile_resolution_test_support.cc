// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/backup_restore_profile_resolution_test_support.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy::test {
namespace {

namespace mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestorePlanResultPtr TombstonePlan(
    const std::string& owner_profile_id,
    const std::string& target_profile_id,
    uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New(
      "restore-resolution-plan", generation, 0u, NowMonotonicMillis() + 60'000u,
      "restore-resolution-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, target_profile_id);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "backup-browser-resolution-test";
  plan->snapshot_sha256.assign(32u, 4u);
  plan->target = target.Clone();
  plan->confirmation_sha256.assign(32u, 5u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), owner_profile_id, std::move(target),
      plan->backup_id, plan->snapshot_sha256, plan->confirmation_sha256);
  auto entry = mojom::BackupRestorePlanEntry::New();
  entry->kind = mojom::BackupRecordKind::kMemoryRecord;
  entry->stable_id = "11111111111111111111111111111111";
  entry->archive_revision = 2u;
  entry->action = mojom::BackupRestoreAction::kStageDeletion;
  entry->schema_version = 1u;
  entry->state = mojom::BackupRecordState::kTombstone;
  entry->plaintext_bytes = 0u;
  entry->plaintext_sha256.assign(32u, 0u);
  plan->entries.push_back(std::move(entry));
  return plan;
}

mojom::BackupRestoreStageAuthorizationPtr StageAuthorization(
    const mojom::BackupRestorePlanResult& plan,
    uint64_t generation) {
  return mojom::BackupRestoreStageAuthorization::New(
      plan.binding.Clone(),
      mojom::OperationEnvelope::New("restore-resolution-stage", generation, 0u,
                                    NowMonotonicMillis() + 60'000u,
                                    "restore-resolution-stage-once"));
}

mojom::BackupRestoreCommitAuthorizationPtr CommitAuthorization(
    const mojom::BackupRestorePlanResult& plan,
    uint64_t generation) {
  return mojom::BackupRestoreCommitAuthorization::New(
      plan.binding.Clone(),
      mojom::OperationEnvelope::New("restore-resolution-commit", generation, 0u,
                                    NowMonotonicMillis() + 60'000u,
                                    "restore-resolution-commit-once"));
}

mojom::BackupRestoreCandidateWitnessPtr RecoveryWitness() {
  auto witness = mojom::BackupRestoreCandidateWitness::New();
  witness->selection = {mojom::BackupRecordKind::kMemoryRecord};
  witness->record_count = 1u;
  witness->candidate_records_sha256.assign(32u, 6u);
  return witness;
}

bool SyncRecoveryState(ProfileManager* profile_manager,
                       PrefService* local_state,
                       const base::FilePath& target_profile_path) {
  base::test::TestFuture<void> drained;
  local_state->CommitPendingWrite(drained.GetCallback());
  if (!drained.Wait()) {
    return false;
  }
  auto witnesses = CaptureDormantBackupRestoreTargetPreferenceWitnesses(
      *local_state, target_profile_path);
  if (witnesses.empty()) {
    return false;
  }
  base::test::TestFuture<bool> synchronized;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          &BackupRestorePreferenceFileMatchesAndSync,
          profile_manager->user_data_dir().Append(chrome::kLocalStateFilename),
          std::move(witnesses)),
      synchronized.GetCallback());
  return synchronized.Take();
}

}  // namespace

std::optional<CommittedBackupRestoreCandidate>
CreateCommittedBackupRestoreCandidate(Profile* source,
                                      ProfileManager* profile_manager,
                                      PrefService* local_state) {
  if (!source || !profile_manager || !local_state) {
    return std::nullopt;
  }
  CoreServiceManager* const source_core =
      CoreServiceManagerFactory::GetForProfileIfExists(source);
  if (!source_core ||
      source_core->availability() != CoreServiceManager::Availability::kReady) {
    return std::nullopt;
  }
  const uint64_t generation = source_core->service_generation();
  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
      reserved_future;
  lifecycle.Reserve(source, u"Resolved backup", reserved_future.GetCallback());
  auto reserved = reserved_future.Take();
  if (!reserved) {
    return std::nullopt;
  }
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::BindResult>
      bound_future;
  lifecycle.BindTargetProfileId(reserved->reservation_id, target_profile_id,
                                bound_future.GetCallback());
  auto bound = bound_future.Take();
  if (!bound) {
    return std::nullopt;
  }
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialized_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    initialized_future.GetCallback());
  auto initialized = initialized_future.Take();
  if (!initialized) {
    return std::nullopt;
  }
  std::unique_ptr<ProfileBackupRestoreTarget> target =
      std::move(initialized->target);
  auto plan = TombstonePlan(source_core->browser_profile_id(),
                            target_profile_id, generation);
  base::test::TestFuture<BackupRestoreRecoveryPresentationResult>
      presentation_future;
  lifecycle.PersistRecoveryPresentation(
      reserved->reservation_id, u"Resolved backup",
      {mojom::BackupRecordKind::kAssistantConfiguration,
       mojom::BackupRecordKind::kMemoryRecord},
      plan.Clone(), presentation_future.GetCallback());
  if (!presentation_future.Take()) {
    return std::nullopt;
  }
  const base::FilePath payload_path = source->GetPath().AppendASCII(
      "restore-resolution-" + reserved->reservation_id);
  if (!base::WriteFile(payload_path, std::string_view())) {
    return std::nullopt;
  }
  base::File payload(payload_path,
                     base::File::FLAG_OPEN | base::File::FLAG_READ);
  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult> staged;
  target->Stage(plan.Clone(), StageAuthorization(*plan, generation),
                std::move(payload), staged.GetCallback());
  if (!staged.Take()) {
    return std::nullopt;
  }
  base::test::TestFuture<ProfileBackupRestoreTarget::CommitResult> committed;
  target->Commit(plan.Clone(), CommitAuthorization(*plan, generation),
                 committed.GetCallback());
  auto commit = committed.Take();
  if (!commit || !commit->journal_durable ||
      commit->outcome != mojom::BackupRestoreCommitOutcome::kCommitted) {
    return std::nullopt;
  }
  base::test::TestFuture<bool> closed;
  target->CloseForRecovery(closed.GetCallback());
  if (!closed.Take()) {
    return std::nullopt;
  }
  target.reset();
  return CommittedBackupRestoreCandidate{
      .reservation_id = reserved->reservation_id,
      .target_profile_id = target_profile_id,
      .target_profile_path = reserved->target_profile_path,
  };
}

std::optional<CommittedBackupRestoreCandidate>
CreateInterruptedBackupRestoreCommit(Profile* source,
                                     ProfileManager* profile_manager,
                                     PrefService* local_state) {
  if (!source || !profile_manager || !local_state) {
    return std::nullopt;
  }
  CoreServiceManager* const source_core =
      CoreServiceManagerFactory::GetForProfileIfExists(source);
  if (!source_core ||
      source_core->availability() != CoreServiceManager::Availability::kReady) {
    return std::nullopt;
  }
  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
      reserved_future;
  lifecycle.Reserve(source, u"Interrupted backup",
                    reserved_future.GetCallback());
  auto reserved = reserved_future.Take();
  if (!reserved) {
    return std::nullopt;
  }
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::BindResult>
      bound_future;
  lifecycle.BindTargetProfileId(reserved->reservation_id, target_profile_id,
                                bound_future.GetCallback());
  auto bound = bound_future.Take();
  if (!bound) {
    return std::nullopt;
  }
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialized_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    initialized_future.GetCallback());
  auto initialized = initialized_future.Take();
  if (!initialized || !initialized->target) {
    return std::nullopt;
  }
  auto plan =
      TombstonePlan(source_core->browser_profile_id(), target_profile_id,
                    source_core->service_generation());
  base::test::TestFuture<BackupRestoreRecoveryPresentationResult>
      presentation_future;
  lifecycle.PersistRecoveryPresentation(
      reserved->reservation_id, u"Interrupted backup",
      {mojom::BackupRecordKind::kAssistantConfiguration,
       mojom::BackupRecordKind::kMemoryRecord},
      plan.Clone(), presentation_future.GetCallback());
  if (!presentation_future.Take()) {
    return std::nullopt;
  }
  auto witness = RecoveryWitness();
  auto intent = BeginBackupRestoreCommitIntent(
      local_state, reserved->reservation_id, *plan->binding, *witness,
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  // base::expected exposes no operator bool when its value type has one of
  // its own, as StructPtr does, so ask the expected directly.
  if (!intent.has_value() ||
      !SyncRecoveryState(profile_manager, local_state,
                         reserved->target_profile_path)) {
    return std::nullopt;
  }
  base::test::TestFuture<bool> closed;
  initialized->target->CloseForRecovery(closed.GetCallback());
  if (!closed.Take()) {
    return std::nullopt;
  }
  initialized->target.reset();
  return CommittedBackupRestoreCandidate{
      .reservation_id = reserved->reservation_id,
      .target_profile_id = target_profile_id,
      .target_profile_path = reserved->target_profile_path,
  };
}

}  // namespace taffy::test
