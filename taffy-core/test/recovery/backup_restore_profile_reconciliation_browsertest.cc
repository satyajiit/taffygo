// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "sql/database.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

uint64_t RecoveryNowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestoreBindingPtr RecoveryBinding(
    const std::string& owner_profile_id,
    const std::string& target_profile_id,
    uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New(
      "restore-recovery-plan", generation, 0u,
      RecoveryNowMonotonicMillis() + 60'000u, "restore-recovery-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, target_profile_id);
  return mojom::BackupRestoreBinding::New(
      std::move(operation), owner_profile_id, std::move(target),
      "backup-browser-recovery-test", std::vector<uint8_t>(32u, 4u),
      std::vector<uint8_t>(32u, 5u));
}

mojom::BackupRestoreCandidateWitnessPtr RecoveryWitness() {
  auto witness = mojom::BackupRestoreCandidateWitness::New();
  witness->selection = {mojom::BackupRecordKind::kMemoryRecord};
  witness->record_count = 1u;
  witness->candidate_records_sha256.assign(32u, 6u);
  return witness;
}

bool SyncRecoveryRegistry(ProfileManager* profile_manager,
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

class BackupRestoreProfileReconciliationBrowserTest
    : public PlatformBrowserTest {
 protected:
  BrowserProfilesRestoreLifecycle::ReserveResult Reserve(
      BrowserProfilesRestoreLifecycle& lifecycle) {
    base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
        future;
    lifecycle.Reserve(GetProfile(), u"Recovered backup", future.GetCallback());
    return future.Take();
  }

  BrowserProfilesRestoreLifecycle::BindResult Bind(
      BrowserProfilesRestoreLifecycle& lifecycle,
      const std::string& reservation_id,
      const std::string& target_profile_id) {
    base::test::TestFuture<BrowserProfilesRestoreLifecycle::BindResult> future;
    lifecycle.BindTargetProfileId(reservation_id, target_profile_id,
                                  future.GetCallback());
    return future.Take();
  }

  std::unique_ptr<ProfileBackupRestoreTarget> Initialize(
      BrowserProfilesRestoreLifecycle& lifecycle,
      BoundBackupRestoreProfileHandle handle) {
    base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
        future;
    lifecycle.InitializeDormantTarget(std::move(handle), future.GetCallback());
    auto result = future.Take();
    EXPECT_TRUE(result.has_value());
    return result.has_value() ? std::move(result->target) : nullptr;
  }

  BrowserProfilesRestoreLifecycle::CommitReconciliationResult Reconcile(
      BrowserProfilesRestoreLifecycle& lifecycle,
      const std::string& reservation_id) {
    base::test::TestFuture<
        BrowserProfilesRestoreLifecycle::CommitReconciliationResult>
        future;
    lifecycle.ReconcileInterruptedCommit(reservation_id, future.GetCallback());
    return future.Take();
  }
};

TEST(BackupRestoreProfileReconciliationProjectionTest,
     StorageErrorsRemainTypedAndNeverBecomePhysicalUnknown) {
  using StorageError = storage::backup::DormantBackupRestoreReconcileError;
  using Error = BackupRestoreCommitReconciliationError;
  using restore_reconciliation_internal::ProjectStorageReconcileError;

  EXPECT_EQ(Error::kBusy,
            ProjectStorageReconcileError(StorageError::kTargetBusy));
  EXPECT_EQ(Error::kProfileStateRefused,
            ProjectStorageReconcileError(StorageError::kInvalidTarget));
  EXPECT_EQ(Error::kProfileStateRefused,
            ProjectStorageReconcileError(StorageError::kTargetChanged));
  EXPECT_EQ(Error::kSchemaMismatch,
            ProjectStorageReconcileError(StorageError::kSchemaMismatch));
  EXPECT_EQ(Error::kHistoryRefused,
            ProjectStorageReconcileError(StorageError::kInvalidWitness));
  EXPECT_EQ(Error::kStorageUnavailable,
            ProjectStorageReconcileError(StorageError::kStorageUnavailable));
}

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfileReconciliationBrowserTest,
    BusyTargetReturnsTypedErrorThenFreshLifecycleSettlesPristineCommit) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const profile_manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  Profile* const source = GetProfile();
  ASSERT_TRUE(profile_manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source);

  CoreServiceManager* const source_core =
      CoreServiceManagerFactory::GetForProfile(source);
  ASSERT_TRUE(source_core);
  base::test::TestFuture<bool> ready;
  source_core->PrepareForCoreApi(ready.GetCallback());
  ASSERT_TRUE(ready.Take());
  ASSERT_EQ(CoreServiceManager::Availability::kReady,
            source_core->availability());

  BrowserProfilesRestoreLifecycle creator(profile_manager, local_state);
  auto reserved = Reserve(creator);
  ASSERT_TRUE(reserved.has_value());
  const std::string reservation_id = reserved->reservation_id;
  const base::FilePath target_path = reserved->target_profile_path;
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(creator, reservation_id, target_profile_id);
  ASSERT_TRUE(bound.has_value());
  std::unique_ptr<ProfileBackupRestoreTarget> live_target =
      Initialize(creator, std::move(*bound));
  ASSERT_TRUE(live_target);

  auto binding =
      RecoveryBinding(source_core->browser_profile_id(), target_profile_id,
                      source_core->service_generation());
  auto witness = RecoveryWitness();
  auto intent = BeginBackupRestoreCommitIntent(
      local_state, reservation_id, *binding, *witness,
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  ASSERT_TRUE(intent.has_value());
  ASSERT_TRUE(SyncRecoveryRegistry(profile_manager, local_state, target_path));

  // A different lifecycle cannot race a still-live mutable target and infer
  // pristine. Storage's process-local lease returns a typed busy error, not a
  // fabricated unknown physical observation.
  BrowserProfilesRestoreLifecycle while_live(profile_manager, local_state);
  auto busy_result = Reconcile(while_live, reservation_id);
  ASSERT_FALSE(busy_result.has_value());
  EXPECT_EQ(BackupRestoreCommitReconciliationError::kBusy, busy_result.error());

  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(1u, history->size());
  ASSERT_TRUE(history->back()->intent);

  live_target.reset();
  base::test::TestFuture<void> target_closed;
  base::ThreadPoolInstance::Get()->FlushAsyncForTesting(
      target_closed.GetCallback());
  ASSERT_TRUE(target_closed.Wait());

  ProfileAttributesEntry* const restarted_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_path);
  ASSERT_TRUE(restarted_entry);
  // Chromium deliberately does not persist omission: a reconstructed
  // ProfileAttributesStorage loads every disk entry as non-omitted. Model that
  // restart boundary while retaining the durable ephemeral bit and registry
  // quarantine. Recovery must not mistake the in-memory bit for disk custody.
  restarted_entry->SetIsOmitted(false);
  EXPECT_FALSE(restarted_entry->IsOmitted());
  EXPECT_TRUE(restarted_entry->IsEphemeral());

  BrowserProfilesRestoreLifecycle after_restart(profile_manager, local_state);
  auto settled = Reconcile(after_restart, reservation_id);
  ASSERT_TRUE(settled.has_value());
  ASSERT_TRUE(settled->physical_observation.has_value());
  EXPECT_EQ(mojom::BackupRestoreCommitOutcome::kDefinitelyNotCommitted,
            settled->physical_observation->outcome);
  EXPECT_TRUE(settled->physical_observation->journal_durable);
  ASSERT_TRUE(settled->classification);
  EXPECT_EQ(mojom::BackupRestoreRecoveryClassificationKind::kCleanupRequired,
            settled->classification->kind);

  history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(2u, history->size());
  ASSERT_TRUE(history->back()->outcome);
  EXPECT_EQ(mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted,
            history->back()->outcome->outcome);

  // A terminal journal classification is observational only. It performs no
  // second SQL read and appends no further physical fact.
  BrowserProfilesRestoreLifecycle journal_only(profile_manager, local_state);
  auto classified = Reconcile(journal_only, reservation_id);
  ASSERT_TRUE(classified.has_value());
  EXPECT_FALSE(classified->physical_observation.has_value());
  ASSERT_TRUE(classified->classification);
  EXPECT_EQ(mojom::BackupRestoreRecoveryClassificationKind::kCleanupRequired,
            classified->classification->kind);
  history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  ASSERT_TRUE(history.has_value());
  EXPECT_EQ(2u, history->size());

  Profile* const target = profile_manager->GetProfileByPath(target_path);
  ProfileAttributesEntry* const entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_path);
  ASSERT_TRUE(target);
  ASSERT_TRUE(entry);
  EXPECT_FALSE(entry->IsOmitted());
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(local_state, target_path));
  EXPECT_EQ(source->GetPath(), profile_manager->GetLastUsedProfileDir());
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));
}

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfileReconciliationBrowserTest,
    SchemaMismatchLeavesCommitIntentUnchangedAndCandidateQuarantined) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const profile_manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  Profile* const source = GetProfile();
  ASSERT_TRUE(profile_manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source);

  CoreServiceManager* const source_core =
      CoreServiceManagerFactory::GetForProfile(source);
  ASSERT_TRUE(source_core);
  base::test::TestFuture<bool> ready;
  source_core->PrepareForCoreApi(ready.GetCallback());
  ASSERT_TRUE(ready.Take());

  BrowserProfilesRestoreLifecycle creator(profile_manager, local_state);
  auto reserved = Reserve(creator);
  ASSERT_TRUE(reserved.has_value());
  const std::string reservation_id = reserved->reservation_id;
  const base::FilePath target_path = reserved->target_profile_path;
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(creator, reservation_id, target_profile_id);
  ASSERT_TRUE(bound.has_value());
  std::unique_ptr<ProfileBackupRestoreTarget> live_target =
      Initialize(creator, std::move(*bound));
  ASSERT_TRUE(live_target);

  auto binding =
      RecoveryBinding(source_core->browser_profile_id(), target_profile_id,
                      source_core->service_generation());
  auto witness = RecoveryWitness();
  auto intent = BeginBackupRestoreCommitIntent(
      local_state, reservation_id, *binding, *witness,
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  ASSERT_TRUE(intent.has_value());
  ASSERT_TRUE(SyncRecoveryRegistry(profile_manager, local_state, target_path));

  live_target.reset();
  base::test::TestFuture<void> target_closed;
  base::ThreadPoolInstance::Get()->FlushAsyncForTesting(
      target_closed.GetCallback());
  ASSERT_TRUE(target_closed.Wait());

  const base::FilePath database_path =
      target_path.AppendASCII("TaffyCore").AppendASCII("core.sqlite3");
  // The tag is checked at compile time against the DatabaseTag histogram
  // variants, so a test-only name does not exist: this opens the journal the
  // product wrote, under the product's own tag.
  sql::Database writer(sql::Database::Tag("TaffyCore"));
  ASSERT_TRUE(writer.Open(database_path));
  ASSERT_TRUE(writer.Execute("UPDATE taffy_storage_schema SET version=999"));
  writer.Close();

  BrowserProfilesRestoreLifecycle after_restart(profile_manager, local_state);
  auto result = Reconcile(after_restart, reservation_id);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(BackupRestoreCommitReconciliationError::kSchemaMismatch,
            result.error());

  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(1u, history->size());
  EXPECT_TRUE(history->front()->intent);
  EXPECT_FALSE(history->front()->outcome);
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(local_state, target_path));
}

}  // namespace
}  // namespace taffy
