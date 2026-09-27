// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

// Why a core that did not come up did not come up.
//
// `PrepareForCoreApi` answers a bare bool, and a bare bool is the same answer
// for a utility process that failed to launch, one whose restart circuit is
// open, and one still starting when the wait gave up. Those are three
// different defects and the assertion has to be able to tell them apart on the
// run that fails, because this suite needs a Chrome Profile and a device and
// is not cheap to reproduce.
std::string_view DescribeAvailability(CoreServiceAvailability availability) {
  switch (availability) {
    case CoreServiceAvailability::kStopped:
      return "kStopped (never launched)";
    case CoreServiceAvailability::kStarting:
      return "kStarting (the wait gave up before the core answered)";
    case CoreServiceAvailability::kReady:
      return "kReady";
    case CoreServiceAvailability::kUnavailable:
      return "kUnavailable (the utility process did not come up)";
    case CoreServiceAvailability::kCircuitOpen:
      return "kCircuitOpen (the restart budget is spent)";
    case CoreServiceAvailability::kShuttingDown:
      return "kShuttingDown";
  }
  return "an availability this switch does not name";
}

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestorePlanResultPtr TombstonePlan(
    const std::string& owner_profile_id,
    const std::string& target_profile_id,
    uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New(
      "restore-commit-plan", generation, 0u, NowMonotonicMillis() + 60'000u,
      "restore-commit-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, target_profile_id);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "backup-browser-commit-test";
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
      mojom::OperationEnvelope::New("restore-commit-stage", generation, 0u,
                                    NowMonotonicMillis() + 60'000u,
                                    "restore-commit-stage-once"));
}

mojom::BackupRestoreCommitAuthorizationPtr CommitAuthorization(
    const mojom::BackupRestorePlanResult& plan,
    uint64_t generation) {
  return mojom::BackupRestoreCommitAuthorization::New(
      plan.binding.Clone(),
      mojom::OperationEnvelope::New("restore-commit-decision", generation, 0u,
                                    NowMonotonicMillis() + 60'000u,
                                    "restore-commit-decision-once"));
}

base::File EmptyPayload(const base::FilePath& path) {
  EXPECT_TRUE(base::WriteFile(path, std::string_view()));
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
}

class BackupRestoreProfileCommitBrowserTest : public PlatformBrowserTest {
 protected:
  BrowserProfilesRestoreLifecycle::ReserveResult Reserve(
      BrowserProfilesRestoreLifecycle& lifecycle) {
    base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
        future;
    lifecycle.Reserve(GetProfile(), u"Committed backup", future.GetCallback());
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
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileCommitBrowserTest,
                       CommitsOneDurablyWitnessedDormantCandidate) {
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
  ASSERT_TRUE(ready.Take())
      << "the core never became ready, so nothing below this line is about "
         "restore. It is "
      << DescribeAvailability(source_core->availability());
  ASSERT_EQ(CoreServiceManager::Availability::kReady,
            source_core->availability());
  ASSERT_FALSE(source_core->browser_profile_id().empty());
  const uint64_t generation = source_core->service_generation();

  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved.has_value());
  const std::string reservation_id = reserved->reservation_id;
  const base::FilePath target_path = reserved->target_profile_path;
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reservation_id, target_profile_id);
  ASSERT_TRUE(bound.has_value());
  std::unique_ptr<ProfileBackupRestoreTarget> target =
      Initialize(lifecycle, std::move(*bound));
  ASSERT_TRUE(target);

  auto plan = TombstonePlan(source_core->browser_profile_id(),
                            target_profile_id, generation);
  const base::FilePath payload_path =
      source->GetPath().AppendASCII("restore-commit-empty-payload");
  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult> staged;
  target->Stage(plan.Clone(), StageAuthorization(*plan, generation),
                EmptyPayload(payload_path), staged.GetCallback());
  ASSERT_TRUE(staged.Take().has_value());

  // A stale decision is refused before it is consumed, so the one current
  // source-Core decision remains usable by this private physical fixture.
  base::test::TestFuture<ProfileBackupRestoreTarget::CommitResult> stale_commit;
  target->Commit(plan.Clone(), CommitAuthorization(*plan, generation + 1u),
                 stale_commit.GetCallback());
  auto stale_result = stale_commit.Take();
  ASSERT_FALSE(stale_result.has_value());
  EXPECT_EQ(ProfileBackupRestoreTargetError::kInvalidAuthorization,
            stale_result.error());

  base::test::TestFuture<ProfileBackupRestoreTarget::CommitResult> committed;
  target->Commit(plan.Clone(), CommitAuthorization(*plan, generation),
                 committed.GetCallback());
  auto commit_result = committed.Take();
  ASSERT_TRUE(commit_result.has_value());
  EXPECT_EQ(mojom::BackupRestoreCommitOutcome::kCommitted,
            commit_result->outcome);
  EXPECT_TRUE(commit_result->journal_durable);

  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(2u, history->size());
  ASSERT_TRUE(history->front()->intent);
  ASSERT_TRUE(history->back()->outcome);
  EXPECT_EQ(mojom::BackupRestorePhysicalIntent::kCommitCandidate,
            history->front()->intent->intent);
  EXPECT_EQ(history->front()->intent->intent_id,
            history->back()->outcome->intent_id);
  EXPECT_EQ(mojom::BackupRestoreObservedOutcome::kCompleted,
            history->back()->outcome->outcome);
  ASSERT_TRUE(history->front()->binding);
  EXPECT_EQ(target_profile_id, history->front()->binding->target_profile_id);
  EXPECT_EQ(1u, history->front()->binding->record_count);
  EXPECT_EQ(std::vector{mojom::BackupRecordKind::kMemoryRecord},
            history->front()->binding->selection);
  EXPECT_TRUE(
      std::ranges::any_of(history->front()->binding->candidate_records_sha256,
                          [](uint8_t byte) { return byte != 0u; }));

  sql::Database database(sql::DatabaseOptions().set_read_only(true),
                         sql::Database::Tag("TaffyCore"));
  ASSERT_TRUE(database.Open(
      target_path.AppendASCII("TaffyCore").AppendASCII("core.sqlite3")));
  sql::Statement tombstone(database.GetUniqueStatement(
      "SELECT revision FROM core_memory_tombstone WHERE memory_id=?"));
  tombstone.BindString(0, "11111111111111111111111111111111");
  ASSERT_TRUE(tombstone.Step());
  EXPECT_EQ(2, tombstone.ColumnInt(0));
  EXPECT_FALSE(tombstone.Step());
  EXPECT_TRUE(tombstone.Succeeded());
  sql::Statement effects(
      database.GetUniqueStatement("SELECT COUNT(*) FROM core_effect_journal"));
  ASSERT_TRUE(effects.Step());
  EXPECT_EQ(0, effects.ColumnInt(0));

  Profile* const target_profile =
      profile_manager->GetProfileByPath(target_path);
  ProfileAttributesEntry* const target_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_path);
  ASSERT_TRUE(target_profile);
  ASSERT_TRUE(target_entry);
  EXPECT_TRUE(target_entry->IsOmitted());
  EXPECT_TRUE(target_entry->IsEphemeral());
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(local_state, target_path));
  EXPECT_EQ(source->GetPath(), profile_manager->GetLastUsedProfileDir());
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target_profile));

  base::test::TestFuture<ProfileBackupRestoreTarget::CommitResult> duplicate;
  target->Commit(plan.Clone(), CommitAuthorization(*plan, generation),
                 duplicate.GetCallback());
  auto duplicate_result = duplicate.Take();
  ASSERT_FALSE(duplicate_result.has_value());
  EXPECT_EQ(ProfileBackupRestoreTargetError::kBusy, duplicate_result.error());
  base::test::TestFuture<bool> cleanup;
  target->Abandon(cleanup.GetCallback());
  EXPECT_FALSE(cleanup.Take());
  EXPECT_TRUE(base::DirectoryExists(
      target_path.AppendASCII("TaffyCore").AppendASCII("TaffyRestoreStaging")));

  // A consumed, drained commit can permanently hand its mutable SQL custody
  // to recovery. The interface remains alive but cannot clean or mutate the
  // retained candidate after the blocking-sequence owner has closed.
  base::test::TestFuture<bool> closed;
  target->CloseForRecovery(closed.GetCallback());
  EXPECT_TRUE(closed.Take());
  base::test::TestFuture<bool> duplicate_close;
  target->CloseForRecovery(duplicate_close.GetCallback());
  EXPECT_FALSE(duplicate_close.Take());
  base::test::TestFuture<bool> cleanup_after_close;
  target->Abandon(cleanup_after_close.GetCallback());
  EXPECT_FALSE(cleanup_after_close.Take());
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileCommitBrowserTest,
                       AuthorityReplyLossCloseRetainsVerifiedStage) {
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
  ASSERT_TRUE(ready.Take())
      << "the core never became ready, so nothing below this line is about "
         "restore. It is "
      << DescribeAvailability(source_core->availability());
  const uint64_t generation = source_core->service_generation();

  BrowserProfilesRestoreLifecycle lifecycle(profile_manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved.has_value());
  const base::FilePath target_path = reserved->target_profile_path;
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound.has_value());
  std::unique_ptr<ProfileBackupRestoreTarget> target =
      Initialize(lifecycle, std::move(*bound));
  ASSERT_TRUE(target);

  auto plan = TombstonePlan(source_core->browser_profile_id(),
                            target_profile_id, generation);
  const base::FilePath payload_path =
      source->GetPath().AppendASCII("restore-close-retained-payload");
  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult> staged;
  target->Stage(plan.Clone(), StageAuthorization(*plan, generation),
                EmptyPayload(payload_path), staged.GetCallback());
  ASSERT_TRUE(staged.Take().has_value());
  const base::FilePath staging_path =
      target_path.AppendASCII("TaffyCore").AppendASCII("TaffyRestoreStaging");
  ASSERT_TRUE(base::DirectoryExists(staging_path));

  // The coordinator invokes this path only after a commit-authority request
  // has escaped and its terminal was drained. The physical owner has not seen
  // that lost reply, so CloseForRecovery itself must freeze the exact staged
  // witness before releasing its blocking lease.
  base::test::TestFuture<bool> closed;
  target->CloseForRecovery(closed.GetCallback());
  EXPECT_TRUE(closed.Take());
  EXPECT_TRUE(base::DirectoryExists(staging_path));
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(local_state, target_path));
  EXPECT_FALSE(CoreServiceManagerFactory::HasExistingInstanceForProfile(
      profile_manager->GetProfileByPath(target_path)));
}

}  // namespace
}  // namespace taffy
