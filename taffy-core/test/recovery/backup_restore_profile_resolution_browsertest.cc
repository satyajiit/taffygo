// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_preference_readback.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/test/recovery/backup_restore_profile_resolution_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

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

bool SynchronizeRegistry(ProfileManager* profile_manager,
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

class BackupRestoreProfileResolutionBrowserTest : public PlatformBrowserTest {
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

  BrowserProfilesRestoreLifecycle::CandidateResolutionResult Resolve(
      BrowserProfilesRestoreLifecycle& lifecycle,
      const std::string& reservation_id,
      mojom::BackupRestoreResolutionChoice choice) {
    base::test::TestFuture<
        BrowserProfilesRestoreLifecycle::CandidateResolutionResult>
        future;
    lifecycle.ResolveBackupRestoreCandidate(reservation_id, choice,
                                            future.GetCallback());
    return future.Take();
  }

  raw_ptr<CoreServiceManager> source_core_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileResolutionBrowserTest,
                       AcceptPublishesOnlyAfterDurableTerminal) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const profile_manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  Profile* const source = GetProfile();
  ASSERT_TRUE(profile_manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source);
  ASSERT_TRUE(PrepareSourceCore());

  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      source, profile_manager, local_state);
  ASSERT_TRUE(candidate);
  Profile* const target =
      profile_manager->GetProfileByPath(candidate->target_profile_path);
  ProfileAttributesEntry* const hidden_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(candidate->target_profile_path);
  ASSERT_TRUE(target);
  ASSERT_TRUE(hidden_entry);
  EXPECT_TRUE(hidden_entry->IsEphemeral());
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));

  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  auto accepted =
      Resolve(lifecycle, candidate->reservation_id,
              mojom::BackupRestoreResolutionChoice::kAcceptCandidate);

  ASSERT_TRUE(accepted.has_value());
  ASSERT_TRUE(accepted->classification);
  EXPECT_EQ(mojom::BackupRestoreRecoveryClassificationKind::kPublished,
            accepted->classification->kind);
  EXPECT_TRUE(accepted->physical_action_dispatched);
  EXPECT_TRUE(accepted->reservation_retired);
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  ASSERT_TRUE(reservations.has_value());
  EXPECT_TRUE(reservations->empty());
  ProfileAttributesEntry* const published_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(candidate->target_profile_path);
  ASSERT_TRUE(published_entry);
  EXPECT_FALSE(published_entry->IsOmitted());
  EXPECT_FALSE(published_entry->IsEphemeral());
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kNotQuarantined,
            BackupRestoreQuarantineForProfilePath(
                local_state, candidate->target_profile_path));
  EXPECT_FALSE(
      base::PathExists(candidate->target_profile_path.AppendASCII("TaffyCore")
                           .AppendASCII("TaffyRestoreStaging")));
  EXPECT_TRUE(
      base::PathExists(candidate->target_profile_path.AppendASCII("TaffyCore")
                           .AppendASCII("core.sqlite3")));
  EXPECT_EQ(source->GetPath(), profile_manager->GetLastUsedProfileDir());
  EXPECT_TRUE(base::PathExists(source->GetPath()));
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileResolutionBrowserTest,
                       DiscardDeletesOnlyReservedHiddenCandidate) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const profile_manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  Profile* const source = GetProfile();
  ASSERT_TRUE(profile_manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source);
  ASSERT_TRUE(PrepareSourceCore());

  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      source, profile_manager, local_state);
  ASSERT_TRUE(candidate);
  Profile* const target =
      profile_manager->GetProfileByPath(candidate->target_profile_path);
  ASSERT_TRUE(target);
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));

  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  auto discarded =
      Resolve(lifecycle, candidate->reservation_id,
              mojom::BackupRestoreResolutionChoice::kDiscardCandidate);

  ASSERT_TRUE(discarded.has_value());
  ASSERT_TRUE(discarded->classification);
  EXPECT_EQ(mojom::BackupRestoreRecoveryClassificationKind::kVerifiedDeleted,
            discarded->classification->kind);
  EXPECT_TRUE(discarded->physical_action_dispatched);
  EXPECT_TRUE(discarded->reservation_retired);
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  ASSERT_TRUE(reservations.has_value());
  EXPECT_TRUE(reservations->empty());
  EXPECT_EQ(nullptr,
            profile_manager->GetProfileByPath(candidate->target_profile_path));
  EXPECT_EQ(nullptr,
            profile_manager->GetProfileAttributesStorage()
                .GetProfileAttributesWithPath(candidate->target_profile_path));
  EXPECT_FALSE(base::PathExists(candidate->target_profile_path));
  EXPECT_TRUE(local_state->GetList(prefs::kProfilesDeleted).empty());
  EXPECT_EQ(source->GetPath(), profile_manager->GetLastUsedProfileDir());
  EXPECT_TRUE(profile_manager->IsValidProfile(source));
  EXPECT_TRUE(base::PathExists(source->GetPath()));
  EXPECT_EQ(source_core_,
            CoreServiceManagerFactory::GetForProfileIfExists(source));
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileResolutionBrowserTest,
                       UnknownAcceptIsObservedAndNeverReplayed) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const profile_manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  Profile* const source = GetProfile();
  ASSERT_TRUE(profile_manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source);
  ASSERT_TRUE(PrepareSourceCore());

  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      source, profile_manager, local_state);
  ASSERT_TRUE(candidate);
  auto history =
      ReadBackupRestoreRecoveryJournal(local_state, candidate->reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(2u, history->size());
  const std::string intent_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto authorization = mojom::BackupRestoreRecoveryResolutionAuthorization::New(
      history->front()->binding.Clone(),
      mojom::OperationEnvelope::New(
          "resolution-unknown-test", source_core_->service_generation(), 0u,
          NowMonotonicMillis() + 60'000u, "resolution-unknown-test-once"),
      mojom::BackupRestoreResolutionChoice::kAcceptCandidate, intent_id,
      CloneHistory(*history));
  auto intent = BeginBackupRestoreResolutionIntent(
      local_state, candidate->reservation_id, *authorization);
  ASSERT_TRUE(intent.has_value());
  auto unknown = RecordBackupRestoreResolutionOutcome(
      local_state, candidate->reservation_id, **intent,
      mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
  ASSERT_TRUE(unknown.has_value());
  ASSERT_TRUE(SynchronizeRegistry(profile_manager, local_state,
                                  candidate->target_profile_path));

  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  auto observed =
      Resolve(lifecycle, candidate->reservation_id,
              mojom::BackupRestoreResolutionChoice::kAcceptCandidate);

  ASSERT_TRUE(observed.has_value());
  ASSERT_TRUE(observed->classification);
  EXPECT_EQ(mojom::BackupRestoreRecoveryClassificationKind::kRollbackAvailable,
            observed->classification->kind);
  EXPECT_FALSE(observed->physical_action_dispatched);
  EXPECT_FALSE(observed->reservation_retired);
  ProfileAttributesEntry* const entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(candidate->target_profile_path);
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                local_state, candidate->target_profile_path));
  EXPECT_TRUE(
      base::PathExists(candidate->target_profile_path.AppendASCII("TaffyCore")
                           .AppendASCII("TaffyRestoreStaging")));
  history =
      ReadBackupRestoreRecoveryJournal(local_state, candidate->reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(5u, history->size());
  ASSERT_TRUE(history->back()->outcome);
  EXPECT_EQ(mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted,
            history->back()->outcome->outcome);

  // The first callback is an owner-release barrier. A new explicit Core
  // decision in the same UI turn must not race the old finalizer/path lease.
  auto retried =
      Resolve(lifecycle, candidate->reservation_id,
              mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
  ASSERT_TRUE(retried.has_value());
  ASSERT_TRUE(retried->classification);
  EXPECT_EQ(mojom::BackupRestoreRecoveryClassificationKind::kPublished,
            retried->classification->kind);
  EXPECT_TRUE(retried->reservation_retired);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileResolutionBrowserTest,
                       UnknownDiscardRemainsQuarantinedAndIsNeverReplayed) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const profile_manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  Profile* const source = GetProfile();
  ASSERT_TRUE(profile_manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source);
  ASSERT_TRUE(PrepareSourceCore());

  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      source, profile_manager, local_state);
  ASSERT_TRUE(candidate);
  auto history =
      ReadBackupRestoreRecoveryJournal(local_state, candidate->reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(2u, history->size());
  const std::string intent_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto authorization = mojom::BackupRestoreRecoveryResolutionAuthorization::New(
      history->front()->binding.Clone(),
      mojom::OperationEnvelope::New(
          "discard-unknown-test", source_core_->service_generation(), 0u,
          NowMonotonicMillis() + 60'000u, "discard-unknown-test-once"),
      mojom::BackupRestoreResolutionChoice::kDiscardCandidate, intent_id,
      CloneHistory(*history));
  auto intent = BeginBackupRestoreResolutionIntent(
      local_state, candidate->reservation_id, *authorization);
  ASSERT_TRUE(intent.has_value());
  auto unknown = RecordBackupRestoreResolutionOutcome(
      local_state, candidate->reservation_id, **intent,
      mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
  ASSERT_TRUE(unknown.has_value());
  ASSERT_TRUE(SynchronizeRegistry(profile_manager, local_state,
                                  candidate->target_profile_path));

  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  auto observed =
      Resolve(lifecycle, candidate->reservation_id,
              mojom::BackupRestoreResolutionChoice::kDiscardCandidate);

  ASSERT_TRUE(observed.has_value());
  ASSERT_TRUE(observed->classification);
  EXPECT_EQ(mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired,
            observed->classification->kind);
  EXPECT_FALSE(observed->physical_action_dispatched);
  EXPECT_FALSE(observed->reservation_retired);
  EXPECT_TRUE(base::PathExists(candidate->target_profile_path));
  EXPECT_TRUE(
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(candidate->target_profile_path));
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                local_state, candidate->target_profile_path));
  EXPECT_TRUE(local_state->GetList(prefs::kProfilesDeleted).empty());
  history =
      ReadBackupRestoreRecoveryJournal(local_state, candidate->reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(4u, history->size());
  ASSERT_TRUE(history->back()->outcome);
  EXPECT_EQ(mojom::BackupRestoreObservedOutcome::kOutcomeUnknown,
            history->back()->outcome->outcome);
}

}  // namespace
}  // namespace taffy
