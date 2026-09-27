// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

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
#include "taffy/test/recovery/backup_restore_profile_resolution_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Action = BackupRestoreRestartCandidateAction;
using Mode = BackupRestoreRestartDiscoveryMode;
using Result = BackupRestoreRestartDiscoveryResult;
using Status = BackupRestoreRestartDiscoveryStatus;
using Candidate = test::CommittedBackupRestoreCandidate;

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

class BackupRestoreProfileDiscoveryReconciliationBrowserTest
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

  Result Discover(Mode mode = Mode::kInspectOnly) {
    BrowserProfilesRestoreLifecycle lifecycle(
        g_browser_process->profile_manager(), g_browser_process->local_state());
    base::test::TestFuture<Result> future;
    lifecycle.DiscoverInterruptedBackupRestore(GetProfile(), mode,
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
    auto witnesses = CaptureDormantBackupRestoreTargetPreferenceWitnesses(
        *local_state, candidate.target_profile_path);
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

  bool AppendResolution(const Candidate& candidate,
                        mojom::BackupRestoreResolutionChoice choice,
                        std::optional<mojom::BackupRestoreResolutionOutcome>
                            outcome = std::nullopt) {
    PrefService* const local_state = g_browser_process->local_state();
    auto history =
        ReadBackupRestoreRecoveryJournal(local_state, candidate.reservation_id);
    if (!source_core_ || !history || history->size() != 2u ||
        !history->front() || !history->front()->binding) {
      return false;
    }
    const std::string intent_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto authorization =
        mojom::BackupRestoreRecoveryResolutionAuthorization::New(
            history->front()->binding.Clone(),
            mojom::OperationEnvelope::New(
                "discovery-resolution", source_core_->service_generation(), 0u,
                NowMonotonicMillis() + 60'000u, "discovery-resolution-once"),
            choice, intent_id, CloneHistory(*history));
    auto intent = BeginBackupRestoreResolutionIntent(
        local_state, candidate.reservation_id, *authorization);
    // base::expected disables its operator bool when the value type is itself
    // bool-constructible, which a mojo::StructPtr is, so a refusal is read
    // through has_value() rather than through the object.
    if (!intent.has_value()) {
      return false;
    }
    if (outcome) {
      const auto recorded = RecordBackupRestoreResolutionOutcome(
          local_state, candidate.reservation_id, **intent, *outcome);
      if (!recorded.has_value()) {
        return false;
      }
    }
    return Synchronize(candidate);
  }

  bool ModelRestartedProfileStore(const Candidate& candidate) {
    ProfileAttributesEntry* const entry =
        g_browser_process->profile_manager()
            ->GetProfileAttributesStorage()
            .GetProfileAttributesWithPath(candidate.target_profile_path);
    if (!entry) {
      return false;
    }
    entry->SetIsOmitted(false);
    return !entry->IsOmitted() && entry->IsEphemeral();
  }

  raw_ptr<CoreServiceManager> source_core_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfileDiscoveryReconciliationBrowserTest,
    CommitIsReadOnlyOnlyWhenRequestedAndReturnsDiscardOnlyCandidate) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateInterruptedBackupRestoreCommit(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  ASSERT_TRUE(ModelRestartedProfileStore(*candidate));

  auto inspect_only = Discover();
  ASSERT_TRUE(inspect_only);
  EXPECT_EQ(Status::kOutcomeUnknown, inspect_only->status);
  EXPECT_FALSE(inspect_only->candidate);
  auto history = ReadBackupRestoreRecoveryJournal(
      g_browser_process->local_state(), candidate->reservation_id);
  ASSERT_TRUE(history);
  EXPECT_EQ(1u, history->size());

  auto reconciled = Discover(Mode::kReconcileCommit);
  ASSERT_TRUE(reconciled);
  ASSERT_EQ(Status::kCleanupRequired, reconciled->status);
  ASSERT_TRUE(reconciled->candidate);
  EXPECT_EQ(Action::kDiscardOnly, reconciled->candidate->action);
  EXPECT_EQ(u"Interrupted backup",
            reconciled->candidate->presentation.target_profile_label);
  ASSERT_EQ(2u, reconciled->candidate->presentation.selected_classes.size());
  EXPECT_EQ(
      (std::array<uint32_t, 6>{}),
      reconciled->candidate->presentation.selected_classes[0].action_counts);

  history = ReadBackupRestoreRecoveryJournal(g_browser_process->local_state(),
                                             candidate->reservation_id);
  ASSERT_TRUE(history);
  ASSERT_EQ(2u, history->size());
  ASSERT_TRUE(history->back()->outcome);
  EXPECT_EQ(mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted,
            history->back()->outcome->outcome);
}

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfileDiscoveryReconciliationBrowserTest,
    CommitSchemaMismatchAppendsNoOutcomeAndStaysQuarantined) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateInterruptedBackupRestoreCommit(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  ASSERT_TRUE(ModelRestartedProfileStore(*candidate));

  // The tag is checked at compile time against the DatabaseTag histogram
  // variants, so a test-only name does not exist: this opens the journal the
  // product wrote, under the product's own tag.
  sql::Database writer(sql::Database::Tag("TaffyCore"));
  ASSERT_TRUE(
      writer.Open(candidate->target_profile_path.AppendASCII("TaffyCore")
                      .AppendASCII("core.sqlite3")));
  ASSERT_TRUE(writer.Execute("UPDATE taffy_storage_schema SET version=999"));
  writer.Close();

  auto discovered = Discover(Mode::kReconcileCommit);
  ASSERT_TRUE(discovered);
  EXPECT_EQ(Status::kSchemaMismatch, discovered->status);
  EXPECT_FALSE(discovered->candidate);
  auto history = ReadBackupRestoreRecoveryJournal(
      g_browser_process->local_state(), candidate->reservation_id);
  ASSERT_TRUE(history);
  EXPECT_EQ(1u, history->size());
  EXPECT_EQ(
      BackupRestoreProfileQuarantineStatus::kQuarantined,
      BackupRestoreQuarantineForProfilePath(g_browser_process->local_state(),
                                            candidate->target_profile_path));
}

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfileDiscoveryReconciliationBrowserTest,
    PendingAcceptIsObservedNotReplayedAndReturnsReviewCandidate) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  ASSERT_TRUE(AppendResolution(
      *candidate, mojom::BackupRestoreResolutionChoice::kAcceptCandidate));
  ASSERT_TRUE(ModelRestartedProfileStore(*candidate));

  auto discovered = Discover();
  ASSERT_TRUE(discovered);
  ASSERT_EQ(Status::kRollbackAvailable, discovered->status);
  ASSERT_TRUE(discovered->candidate);
  EXPECT_EQ(Action::kReview, discovered->candidate->action);
  auto history = ReadBackupRestoreRecoveryJournal(
      g_browser_process->local_state(), candidate->reservation_id);
  ASSERT_TRUE(history);
  ASSERT_EQ(4u, history->size());
  ASSERT_TRUE(history->back()->outcome);
  EXPECT_EQ(mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted,
            history->back()->outcome->outcome);
}

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfileDiscoveryReconciliationBrowserTest,
    PendingDiscardRecordsUnknownOnceAndNeverExposesAnAction) {
  ASSERT_TRUE(PrepareSourceCore());
  auto candidate = test::CreateCommittedBackupRestoreCandidate(
      GetProfile(), g_browser_process->profile_manager(),
      g_browser_process->local_state());
  ASSERT_TRUE(candidate);
  ASSERT_TRUE(AppendResolution(
      *candidate, mojom::BackupRestoreResolutionChoice::kDiscardCandidate));

  auto first = Discover();
  ASSERT_TRUE(first);
  EXPECT_EQ(Status::kOutcomeUnknown, first->status);
  EXPECT_FALSE(first->candidate);
  auto history = ReadBackupRestoreRecoveryJournal(
      g_browser_process->local_state(), candidate->reservation_id);
  ASSERT_TRUE(history);
  ASSERT_EQ(4u, history->size());
  ASSERT_TRUE(history->back()->outcome);
  EXPECT_EQ(mojom::BackupRestoreObservedOutcome::kOutcomeUnknown,
            history->back()->outcome->outcome);

  auto second = Discover();
  ASSERT_TRUE(second);
  EXPECT_EQ(Status::kOutcomeUnknown, second->status);
  EXPECT_FALSE(second->candidate);
  history = ReadBackupRestoreRecoveryJournal(g_browser_process->local_state(),
                                             candidate->reservation_id);
  ASSERT_TRUE(history);
  EXPECT_EQ(4u, history->size());
}

}  // namespace
}  // namespace taffy
