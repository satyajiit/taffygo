// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"
#include "taffy/test/recovery/backup_restore_profile_resolution_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Candidate = test::CommittedBackupRestoreCandidate;
using Result = BackupRestoreRestartDiscoveryResult;
using Status = BackupRestoreRestartDiscoveryStatus;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

BackupRestoreRecoveryRecords CloneHistory(
    const BackupRestoreRecoveryRecords& records) {
  BackupRestoreRecoveryRecords copy;
  copy.reserve(records.size());
  for (const auto& record : records) {
    copy.push_back(record.Clone());
  }
  return copy;
}

class BackupRestoreProfileDiscoveryTerminalBrowserTest
    : public PlatformBrowserTest {
 protected:
  bool PrepareSourceCore() {
    source_core_ = CoreServiceManagerFactory::GetForProfile(GetProfile());
    if (!source_core_) {
      return false;
    }
    base::test::TestFuture<bool> ready;
    source_core_->PrepareForCoreApi(ready.GetCallback());
    return ready.Take() && source_core_->availability() ==
                               CoreServiceManager::Availability::kReady;
  }

  Result Discover() {
    BrowserProfilesRestoreLifecycle lifecycle(
        g_browser_process->profile_manager(), g_browser_process->local_state());
    base::test::TestFuture<Result> future;
    lifecycle.DiscoverInterruptedBackupRestore(
        GetProfile(), BackupRestoreRestartDiscoveryMode::kInspectOnly,
        future.GetCallback());
    return future.Take();
  }

  bool Synchronize(const Candidate& candidate) {
    ProfileManager* const profile_manager =
        g_browser_process->profile_manager();
    PrefService* const local_state = g_browser_process->local_state();
    base::test::TestFuture<void> drained;
    local_state->CommitPendingWrite(drained.GetCallback());
    if (!drained.Wait()) {
      return false;
    }
    auto witnesses = restore_resolution_internal::CaptureResolutionWitnesses(
        *local_state, candidate.target_profile_path, false);
    if (witnesses.empty()) {
      return false;
    }
    base::test::TestFuture<bool> synchronized;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
        base::BindOnce(&BackupRestorePreferenceFileMatchesAndSync,
                       profile_manager->user_data_dir().Append(
                           chrome::kLocalStateFilename),
                       std::move(witnesses)),
        synchronized.GetCallback());
    return synchronized.Take();
  }

  mojom::BackupRestoreRecoveryRecordPtr AppendResolutionIntent(
      const Candidate& candidate,
      mojom::BackupRestoreResolutionChoice choice) {
    PrefService* const local_state = g_browser_process->local_state();
    auto history =
        ReadBackupRestoreRecoveryJournal(local_state, candidate.reservation_id);
    if (!source_core_ || !history || history->size() != 2u ||
        !history->front() || !history->front()->binding) {
      return nullptr;
    }
    const std::string intent_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto authorization =
        mojom::BackupRestoreRecoveryResolutionAuthorization::New(
            history->front()->binding.Clone(),
            mojom::OperationEnvelope::New(
                "discovery-terminal", source_core_->service_generation(), 0u,
                NowMonotonicMillis() + 60'000u, "discovery-terminal-once"),
            choice, intent_id, CloneHistory(*history));
    auto appended = BeginBackupRestoreResolutionIntent(
        local_state, candidate.reservation_id, *authorization);
    // `appended` is a base::expected whose value type carries its own
    // explicit operator bool, so expected exposes none: ask it directly.
    if (!appended.has_value() || !Synchronize(candidate)) {
      return nullptr;
    }
    return std::move(*appended);
  }

  bool RecordCompleted(const Candidate& candidate,
                       const mojom::BackupRestoreRecoveryRecord& exact_intent) {
    return RecordBackupRestoreResolutionOutcome(
               g_browser_process->local_state(), candidate.reservation_id,
               exact_intent, mojom::BackupRestoreResolutionOutcome::kCompleted)
               .has_value() &&
           Synchronize(candidate);
  }

  bool FinalizePublished(
      const Candidate& candidate,
      const mojom::BackupRestoreRecoveryRecord& exact_intent) {
    if (!exact_intent.binding) {
      return false;
    }
    auto witness = mojom::BackupRestoreCandidateWitness::New();
    witness->selection = exact_intent.binding->selection;
    witness->record_count = exact_intent.binding->record_count;
    witness->candidate_records_sha256 =
        exact_intent.binding->candidate_records_sha256;
    base::test::TestFuture<bool> finalized;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
        base::BindOnce(
            [](base::FilePath target_path, std::string target_profile_id,
               mojom::BackupRestoreCandidateWitnessPtr witness) {
              auto opened = storage::backup::
                  DormantBackupRestoreTargetFinalizer::OpenForAccept(
                      target_path,
                      mojom::BackupRestoreTarget::New(
                          mojom::BackupRestoreTargetKind::kNewRegularProfile,
                          std::move(target_profile_id)),
                      std::move(witness));
              // Same rule as above for the unique_ptr value type; the
              // short circuit keeps the dereference on the value path.
              return opened.has_value() &&
                     (*opened)->FinalizeForPublication().has_value();
            },
            candidate.target_profile_path, candidate.target_profile_id,
            std::move(witness)),
        finalized.GetCallback());
    if (!finalized.Take()) {
      return false;
    }
    ProfileAttributesEntry* const entry =
        g_browser_process->profile_manager()
            ->GetProfileAttributesStorage()
            .GetProfileAttributesWithPath(candidate.target_profile_path);
    if (!entry || !entry->IsEphemeral()) {
      return false;
    }
    entry->SetIsOmitted(false);
    entry->SetIsEphemeral(false);
    return Synchronize(candidate);
  }

  bool FinalizeDeleted(const Candidate& candidate) {
    ProfileManager* const profile_manager =
        g_browser_process->profile_manager();
    if (!ScheduleProfileDirectoryForDeletion(candidate.target_profile_path) ||
        !PersistProfileDirectoryDeletionMarker(candidate.target_profile_path) ||
        !Synchronize(candidate) ||
        !ArmProfileDirectoryForDeletion(candidate.target_profile_path)) {
      return false;
    }
    base::test::TestFuture<bool> deleted;
    return profile_manager->DeleteMarkedEphemeralProfileOnAndroid(
               candidate.target_profile_path, deleted.GetCallback()) &&
           deleted.Take() && Synchronize(candidate);
  }

  raw_ptr<CoreServiceManager> source_core_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileDiscoveryTerminalBrowserTest,
                       PublishedTerminalIsVerifiedBeforeRetirement) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  auto intent = AppendResolutionIntent(
      *candidate, mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
  ASSERT_TRUE(intent);
  ASSERT_TRUE(FinalizePublished(*candidate, *intent));
  ASSERT_TRUE(RecordCompleted(*candidate, *intent));

  auto discovered = Discover();
  ASSERT_TRUE(discovered);
  EXPECT_EQ(Status::kPublished, discovered->status);
  EXPECT_FALSE(discovered->candidate);
  auto reservations =
      ReadBackupRestoreProfileReservations(g_browser_process->local_state());
  ASSERT_TRUE(reservations);
  EXPECT_TRUE(reservations->empty());
  ProfileAttributesEntry* const entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(candidate->target_profile_path);
  ASSERT_TRUE(entry);
  EXPECT_FALSE(entry->IsOmitted());
  EXPECT_FALSE(entry->IsEphemeral());
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileDiscoveryTerminalBrowserTest,
                       DeletedTerminalIsVerifiedBeforeRetirement) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  auto intent = AppendResolutionIntent(
      *candidate, mojom::BackupRestoreResolutionChoice::kDiscardCandidate);
  ASSERT_TRUE(intent);
  ASSERT_TRUE(FinalizeDeleted(*candidate));
  ASSERT_TRUE(RecordCompleted(*candidate, *intent));

  auto discovered = Discover();
  ASSERT_TRUE(discovered);
  EXPECT_EQ(Status::kVerifiedDeleted, discovered->status);
  EXPECT_FALSE(discovered->candidate);
  auto reservations =
      ReadBackupRestoreProfileReservations(g_browser_process->local_state());
  ASSERT_TRUE(reservations);
  EXPECT_TRUE(reservations->empty());
  EXPECT_FALSE(base::PathExists(candidate->target_profile_path));
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileDiscoveryTerminalBrowserTest,
                       PublishedClaimWithoutPhysicalWitnessKeepsQuarantine) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  auto intent = AppendResolutionIntent(
      *candidate, mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
  ASSERT_TRUE(intent);
  ASSERT_TRUE(RecordCompleted(*candidate, *intent));

  auto discovered = Discover();
  ASSERT_TRUE(discovered);
  EXPECT_EQ(Status::kCustodyAmbiguous, discovered->status);
  EXPECT_FALSE(discovered->candidate);
  auto reservations =
      ReadBackupRestoreProfileReservations(g_browser_process->local_state());
  ASSERT_TRUE(reservations);
  EXPECT_EQ(1u, reservations->size());
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileDiscoveryTerminalBrowserTest,
                       DeletedClaimWithoutPhysicalWitnessKeepsQuarantine) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  auto intent = AppendResolutionIntent(
      *candidate, mojom::BackupRestoreResolutionChoice::kDiscardCandidate);
  ASSERT_TRUE(intent);
  ASSERT_TRUE(RecordCompleted(*candidate, *intent));

  auto discovered = Discover();
  ASSERT_TRUE(discovered);
  EXPECT_EQ(Status::kCustodyAmbiguous, discovered->status);
  EXPECT_FALSE(discovered->candidate);
  auto reservations =
      ReadBackupRestoreProfileReservations(g_browser_process->local_state());
  ASSERT_TRUE(reservations);
  EXPECT_EQ(1u, reservations->size());
  EXPECT_TRUE(base::PathExists(candidate->target_profile_path));
}

}  // namespace
}  // namespace taffy
