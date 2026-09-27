// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>

#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup_internal.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/profile_backup_coordinator_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class BrowserProfilesRestoreLifecyclePrecommitTestPeer {
 public:
  static void HoldPresentation(
      BrowserProfilesRestoreLifecycle& lifecycle,
      const ReservedBackupRestoreProfile& reservation,
      const base::FilePath& source_profile_path,
      const std::string& target_profile_id,
      BrowserProfilesRestoreLifecycle::RecoveryPresentationCallback callback) {
    auto pending = std::make_unique<
        BrowserProfilesRestoreLifecycle::PendingRecoveryPresentation>();
    pending->reservation_id = reservation.reservation_id;
    pending->source_profile_path = source_profile_path;
    pending->target_profile_path = reservation.target_profile_path;
    pending->target_profile_id = target_profile_id;
    pending->callback = std::move(callback);
    lifecycle.pending_recovery_presentation_ = std::move(pending);
  }

  static bool CleanupWaitsForPresentation(
      const BrowserProfilesRestoreLifecycle& lifecycle) {
    return lifecycle.pending_precommit_cleanup_ &&
           !lifecycle.pending_precommit_cleanup_->readback_started;
  }

  static void ArmInlineCleanupFailure(
      BrowserProfilesRestoreLifecycle& lifecycle,
      base::FilePath target_without_attributes,
      BrowserProfilesRestoreLifecycle::PrecommitCleanupCallback
          cleanup_callback,
      BrowserProfilesRestoreLifecycle::RecoveryPresentationCallback
          presentation_callback) {
    auto presentation = std::make_unique<
        BrowserProfilesRestoreLifecycle::PendingRecoveryPresentation>();
    presentation->callback = std::move(presentation_callback);
    lifecycle.pending_recovery_presentation_ = std::move(presentation);
    auto cleanup = std::make_unique<
        BrowserProfilesRestoreLifecycle::PendingPrecommitCleanup>();
    cleanup->target.target_profile_path = std::move(target_without_attributes);
    cleanup->callback = std::move(cleanup_callback);
    lifecycle.pending_precommit_cleanup_ = std::move(cleanup);
  }

  static void FinishPresentation(BrowserProfilesRestoreLifecycle& lifecycle) {
    lifecycle.FinishRecoveryPresentation(
        base::unexpected(BackupRestoreProfileRegistryError::kUnavailable));
  }
};

namespace {

class BackupRestoreProfilePrecommitPresentationBrowserTest
    : public PlatformBrowserTest {};

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfilePrecommitPresentationBrowserTest,
    InlineCleanupFailureMayDestroyLifecycleBeforePresentationCallback) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  auto lifecycle =
      std::make_unique<BrowserProfilesRestoreLifecycle>(manager, local_state);
  bool cleanup_called = false;
  bool presentation_called = false;
  BrowserProfilesRestoreLifecyclePrecommitTestPeer::ArmInlineCleanupFailure(
      *lifecycle, manager->user_data_dir().AppendASCII("Profile 999999"),
      base::BindLambdaForTesting(
          [&](BrowserProfilesRestoreLifecycle::PrecommitCleanupResult result) {
            ASSERT_FALSE(result);
            EXPECT_EQ(BackupRestorePrecommitCleanupError::kPersistenceFailed,
                      result.error());
            cleanup_called = true;
            lifecycle.reset();
          }),
      base::BindLambdaForTesting(
          [&](BackupRestoreRecoveryPresentationResult result) {
            EXPECT_FALSE(result);
            presentation_called = true;
          }));

  BrowserProfilesRestoreLifecyclePrecommitTestPeer::FinishPresentation(
      *lifecycle);

  EXPECT_FALSE(lifecycle);
  EXPECT_TRUE(cleanup_called);
  EXPECT_TRUE(presentation_called);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitPresentationBrowserTest,
                       CancellationWaitsForPresentationReadbackCheckpoint) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  CoreServiceManager* const source_core =
      CoreServiceManagerFactory::GetForProfile(GetProfile());
  ASSERT_TRUE(source_core);
  base::test::TestFuture<bool> ready;
  source_core->PrepareForCoreApi(ready.GetCallback());
  ASSERT_TRUE(ready.Take());

  BrowserProfilesRestoreLifecycle lifecycle(manager, local_state);
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
      reserve_future;
  lifecycle.Reserve(GetProfile(), u"Cancelled presentation",
                    reserve_future.GetCallback());
  auto reservation = reserve_future.Take();
  ASSERT_TRUE(reservation);
  const base::FilePath target_path = reservation->target_profile_path;
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::BindResult>
      bind_future;
  lifecycle.BindTargetProfileId(reservation->reservation_id, target_profile_id,
                                bind_future.GetCallback());
  auto bound = bind_future.Take();
  ASSERT_TRUE(bound);
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialize_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    initialize_future.GetCallback());
  auto initialized = initialize_future.Take();
  ASSERT_TRUE(initialized);
  base::test::TestFuture<bool> abandon_future;
  initialized->target->Abandon(abandon_future.GetCallback());
  ASSERT_TRUE(abandon_future.Take());
  initialized->target.reset();

  base::test::TestFuture<BackupRestoreRecoveryPresentationResult>
      presentation_future;
  BrowserProfilesRestoreLifecyclePrecommitTestPeer::HoldPresentation(
      lifecycle, *reservation, GetProfile()->GetPath(), target_profile_id,
      presentation_future.GetCallback());
  auto receipt = ProfileBackupCoordinatorTestPeer::
      MintPrecommitCancellationReceiptForTesting(
          "presentation-cancel", source_core->browser_profile_id(),
          target_profile_id, nullptr);
  base::test::TestFuture<
      BrowserProfilesRestoreLifecycle::PrecommitCleanupResult>
      cleanup_future;
  lifecycle.CleanupNewPrecommitReservation(std::move(initialized->cleanup),
                                           std::move(receipt),
                                           cleanup_future.GetCallback());
  EXPECT_FALSE(cleanup_future.IsReady());
  EXPECT_TRUE(BrowserProfilesRestoreLifecyclePrecommitTestPeer::
                  CleanupWaitsForPresentation(lifecycle));

  BrowserProfilesRestoreLifecyclePrecommitTestPeer::FinishPresentation(
      lifecycle);
  auto presentation_result = presentation_future.Take();
  ASSERT_FALSE(presentation_result);
  EXPECT_EQ(BackupRestoreProfileRegistryError::kUnavailable,
            presentation_result.error());
  EXPECT_TRUE(cleanup_future.Take());

  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  ASSERT_TRUE(reservations);
  EXPECT_TRUE(reservations->empty());
  EXPECT_FALSE(manager->GetProfileByPath(target_path));
  EXPECT_FALSE(
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          target_path));
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kNotQuarantined,
            BackupRestoreQuarantineForProfilePath(local_state, target_path));
}

}  // namespace
}  // namespace taffy
