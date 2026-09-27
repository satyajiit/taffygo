// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestorePlanResultPtr TombstonePlan(
    const std::string& target_profile_id) {
  // This fixture constructs the already-issued wire values only to exercise
  // the private physical adapter. Portable mint/phase behavior has its own
  // source-Core protocol suites and remains unavailable through production UI.
  auto operation = mojom::OperationEnvelope::New("restore-plan", 7u, 0u, 1u,
                                                 "restore-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, target_profile_id);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "backup-browser-adapter-test";
  plan->snapshot_sha256.assign(32u, 4u);
  plan->target = target.Clone();
  plan->confirmation_sha256.assign(32u, 5u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), "11111111-1111-4111-8111-111111111111",
      std::move(target), plan->backup_id, plan->snapshot_sha256,
      plan->confirmation_sha256);
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
    const mojom::BackupRestorePlanResult& plan) {
  return mojom::BackupRestoreStageAuthorization::New(
      plan.binding.Clone(),
      mojom::OperationEnvelope::New("restore-confirm", 7u, 0u,
                                    NowMonotonicMillis() + 60'000u,
                                    "restore-confirm-once"));
}

base::File EmptyPayload(const base::FilePath& path) {
  EXPECT_TRUE(base::WriteFile(path, std::string_view()));
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
}

class BackupRestoreProfileLifecycleBrowserTest : public PlatformBrowserTest {
 protected:
  BrowserProfilesRestoreLifecycle::ReserveResult Reserve(
      BrowserProfilesRestoreLifecycle& lifecycle) {
    base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
        future;
    lifecycle.Reserve(GetProfile(), u"Restored backup", future.GetCallback());
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
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileLifecycleBrowserTest,
                       ReservesBindsAndInitializesOneDormantTarget) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  Profile* const source = GetProfile();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source);
  ASSERT_TRUE(manager->IsValidProfile(source));
  ASSERT_EQ(source->GetPath(), manager->GetLastUsedProfileDir());
  const base::FilePath expected_target =
      manager->GetNextExpectedProfileDirectoryPath();
  ASSERT_FALSE(base::PathExists(expected_target));

  BrowserProfilesRestoreLifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);

  ASSERT_TRUE(reserved.has_value());
  EXPECT_EQ(expected_target, reserved->target_profile_path);
  ASSERT_TRUE(base::Uuid::ParseLowercase(reserved->reservation_id).is_valid());
  EXPECT_EQ(source->GetPath(), manager->GetLastUsedProfileDir());
  Profile* const target = manager->GetProfileByPath(expected_target);
  ASSERT_TRUE(target);
  ASSERT_TRUE(manager->IsValidProfile(target));
  EXPECT_FALSE(target->IsOffTheRecord());
  ProfileAttributesEntry* const entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          expected_target);
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->IsOmitted());
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_EQ(
      BackupRestoreProfileQuarantineStatus::kQuarantined,
      BackupRestoreQuarantineForProfilePath(local_state, expected_target));
  EXPECT_EQ(nullptr, CoreServiceManagerFactory::GetForProfile(target));
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));
  EXPECT_FALSE(base::PathExists(expected_target.AppendASCII("TaffyCore")));

  BackupRestoreProfileReservationList reservations =
      ReadBackupRestoreProfileReservations(local_state);
  ASSERT_TRUE(reservations.has_value());
  ASSERT_EQ(1u, reservations->size());
  EXPECT_EQ(reserved->reservation_id, reservations->front().reservation_id);
  EXPECT_EQ(BackupRestoreProfilePhysicalState::kProfileCreated,
            reservations->front().physical_state);
  EXPECT_FALSE(reservations->front().target_profile_id);

  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);

  ASSERT_TRUE(bound.has_value());
  reservations = ReadBackupRestoreProfileReservations(local_state);
  ASSERT_TRUE(reservations.has_value());
  ASSERT_EQ(1u, reservations->size());
  ASSERT_TRUE(reservations->front().target_profile_id);
  EXPECT_EQ(target_profile_id, *reservations->front().target_profile_id);
  EXPECT_EQ(source->GetPath(), manager->GetLastUsedProfileDir());
  EXPECT_TRUE(entry->IsOmitted());
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_EQ(nullptr, CoreServiceManagerFactory::GetForProfile(target));
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));
  EXPECT_FALSE(base::PathExists(expected_target.AppendASCII("TaffyCore")));

  auto second_handle =
      Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(second_handle.has_value());
  auto third_handle =
      Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(third_handle.has_value());
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialize_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    initialize_future.GetCallback());
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      pending_duplicate_future;
  lifecycle.InitializeDormantTarget(std::move(*second_handle),
                                    pending_duplicate_future.GetCallback());
  auto pending_duplicate = pending_duplicate_future.Take();
  ASSERT_FALSE(pending_duplicate.has_value());
  EXPECT_EQ(BackupRestoreDormantTargetError::kBusy, pending_duplicate.error());

  auto initialized = initialize_future.Take();
  ASSERT_TRUE(initialized.has_value());
  std::unique_ptr<ProfileBackupRestoreTarget> dormant_target =
      std::move(initialized->target);
  ASSERT_TRUE(dormant_target);
  EXPECT_EQ(target_profile_id, dormant_target->target_profile_id());
  EXPECT_TRUE(base::PathExists(
      expected_target.AppendASCII("TaffyCore").AppendASCII("core.sqlite3")));
  EXPECT_EQ(source->GetPath(), manager->GetLastUsedProfileDir());
  EXPECT_TRUE(entry->IsOmitted());
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_EQ(nullptr, CoreServiceManagerFactory::GetForProfile(target));
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));

  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialized_duplicate_future;
  lifecycle.InitializeDormantTarget(std::move(*third_handle),
                                    initialized_duplicate_future.GetCallback());
  auto initialized_duplicate = initialized_duplicate_future.Take();
  ASSERT_FALSE(initialized_duplicate.has_value());
  EXPECT_EQ(BackupRestoreDormantTargetError::kBusy,
            initialized_duplicate.error());

  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult> stage_future;
  dormant_target->Stage(nullptr, nullptr, base::File(),
                        stage_future.GetCallback());
  auto stage_result = stage_future.Take();
  ASSERT_FALSE(stage_result.has_value());
  EXPECT_EQ(ProfileBackupRestoreTargetError::kInvalidAuthorization,
            stage_result.error());

  auto plan = TombstonePlan(target_profile_id);
  auto duplicate_plan = plan.Clone();
  auto duplicate_authorization = StageAuthorization(*plan);
  const base::FilePath payload_path =
      source->GetPath().AppendASCII("restore-adapter-empty-payload");
  const base::FilePath duplicate_payload_path =
      source->GetPath().AppendASCII("restore-adapter-empty-payload-2");
  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult>
      valid_stage_future;
  dormant_target->Stage(std::move(plan), StageAuthorization(*duplicate_plan),
                        EmptyPayload(payload_path),
                        valid_stage_future.GetCallback());
  EXPECT_TRUE(valid_stage_future.Take().has_value());
  const base::FilePath staging_path = expected_target.AppendASCII("TaffyCore")
                                          .AppendASCII("TaffyRestoreStaging");
  EXPECT_TRUE(base::DirectoryExists(staging_path));

  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult>
      duplicate_stage_future;
  dormant_target->Stage(std::move(duplicate_plan),
                        std::move(duplicate_authorization),
                        EmptyPayload(duplicate_payload_path),
                        duplicate_stage_future.GetCallback());
  auto duplicate_stage = duplicate_stage_future.Take();
  ASSERT_FALSE(duplicate_stage.has_value());
  EXPECT_EQ(ProfileBackupRestoreTargetError::kBusy, duplicate_stage.error());

  base::test::TestFuture<bool> cleanup_future;
  dormant_target->Abandon(cleanup_future.GetCallback());
  EXPECT_TRUE(cleanup_future.Take());
  EXPECT_FALSE(base::PathExists(staging_path));

  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult>
      stage_after_abandon_future;
  dormant_target->Stage(nullptr, nullptr, base::File(),
                        stage_after_abandon_future.GetCallback());
  auto stage_after_abandon = stage_after_abandon_future.Take();
  ASSERT_FALSE(stage_after_abandon.has_value());
  EXPECT_EQ(ProfileBackupRestoreTargetError::kUnavailable,
            stage_after_abandon.error());
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileLifecycleBrowserTest,
                       ForeignAndConsumedHandlesCannotInitializeStorage) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  BrowserProfilesRestoreLifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved.has_value());
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound.has_value());

  BoundBackupRestoreProfileHandle handle = std::move(*bound);
  BrowserProfilesRestoreLifecycle foreign_lifecycle(manager, local_state);
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      foreign_future;
  foreign_lifecycle.InitializeDormantTarget(std::move(handle),
                                            foreign_future.GetCallback());
  auto foreign = foreign_future.Take();
  ASSERT_FALSE(foreign.has_value());
  EXPECT_EQ(BackupRestoreDormantTargetError::kInvalidHandle, foreign.error());

  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      consumed_future;
  lifecycle.InitializeDormantTarget(std::move(handle),
                                    consumed_future.GetCallback());
  auto consumed = consumed_future.Take();
  ASSERT_FALSE(consumed.has_value());
  EXPECT_EQ(BackupRestoreDormantTargetError::kInvalidHandle, consumed.error());
  EXPECT_FALSE(
      base::PathExists(reserved->target_profile_path.AppendASCII("TaffyCore")));
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileLifecycleBrowserTest,
                       AbandonDuringValidationPreventsPhysicalStage) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);

  BrowserProfilesRestoreLifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved.has_value());
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound.has_value());
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialize_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    initialize_future.GetCallback());
  auto initialized = initialize_future.Take();
  ASSERT_TRUE(initialized.has_value());
  std::unique_ptr<ProfileBackupRestoreTarget> dormant_target =
      std::move(initialized->target);

  auto plan = TombstonePlan(target_profile_id);
  const base::FilePath payload_path =
      GetProfile()->GetPath().AppendASCII("restore-cancelled-empty-payload");
  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult> stage_future;
  base::test::TestFuture<bool> cleanup_future;
  dormant_target->Stage(plan.Clone(), StageAuthorization(*plan),
                        EmptyPayload(payload_path), stage_future.GetCallback());
  dormant_target->Abandon(cleanup_future.GetCallback());

  auto stage_result = stage_future.Take();
  ASSERT_FALSE(stage_result.has_value());
  EXPECT_EQ(ProfileBackupRestoreTargetError::kUnavailable,
            stage_result.error());
  EXPECT_TRUE(cleanup_future.Take());
  EXPECT_FALSE(
      base::PathExists(reserved->target_profile_path.AppendASCII("TaffyCore")
                           .AppendASCII("TaffyRestoreStaging")));
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileLifecycleBrowserTest,
                       StageCallbackMaySynchronouslyAbandonOnce) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);

  BrowserProfilesRestoreLifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved.has_value());
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound.has_value());
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialize_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    initialize_future.GetCallback());
  auto initialized = initialize_future.Take();
  ASSERT_TRUE(initialized.has_value());
  std::unique_ptr<ProfileBackupRestoreTarget> dormant_target =
      std::move(initialized->target);

  auto plan = TombstonePlan(target_profile_id);
  const base::FilePath payload_path =
      GetProfile()->GetPath().AppendASCII("restore-callback-empty-payload");
  base::test::TestFuture<ProfileBackupRestoreTarget::StageResult> stage_future;
  base::test::TestFuture<bool> cleanup_future;
  int stage_callback_count = 0;
  auto forward_stage = stage_future.GetCallback();
  dormant_target->Stage(
      plan.Clone(), StageAuthorization(*plan), EmptyPayload(payload_path),
      base::BindLambdaForTesting(
          [&, forward_stage = std::move(forward_stage)](
              ProfileBackupRestoreTarget::StageResult result) mutable {
            ++stage_callback_count;
            dormant_target->Abandon(cleanup_future.GetCallback());
            std::move(forward_stage).Run(std::move(result));
          }));

  EXPECT_TRUE(stage_future.Take().has_value());
  EXPECT_TRUE(cleanup_future.Take());
  EXPECT_EQ(1, stage_callback_count);
  EXPECT_FALSE(
      base::PathExists(reserved->target_profile_path.AppendASCII("TaffyCore")
                           .AppendASCII("TaffyRestoreStaging")));
}

}  // namespace
}  // namespace taffy
