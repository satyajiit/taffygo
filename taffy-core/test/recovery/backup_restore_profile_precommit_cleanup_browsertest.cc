// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/test/test_future.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class BackupRestoreProfilePrecommitCleanupBrowserTest
    : public PlatformBrowserTest {
 protected:
  using Lifecycle = BrowserProfilesRestoreLifecycle;

  Lifecycle::ReserveResult Reserve(Lifecycle& lifecycle) {
    base::test::TestFuture<Lifecycle::ReserveResult> future;
    lifecycle.Reserve(GetProfile(), u"Cancelled restore", future.GetCallback());
    return future.Take();
  }

  Lifecycle::BindResult Bind(Lifecycle& lifecycle,
                             const std::string& reservation_id,
                             const std::string& target_profile_id) {
    base::test::TestFuture<Lifecycle::BindResult> future;
    lifecycle.BindTargetProfileId(reservation_id, target_profile_id,
                                  future.GetCallback());
    return future.Take();
  }

  Lifecycle::PrecommitCleanupResult Cancel(Lifecycle& lifecycle) {
    base::test::TestFuture<Lifecycle::PrecommitCleanupResult> future;
    lifecycle.CancelPendingNewReservation(future.GetCallback());
    return future.Take();
  }

  void ExpectRemoved(const base::FilePath& target_path) {
    ProfileManager* const manager = g_browser_process->profile_manager();
    PrefService* const local_state = g_browser_process->local_state();
    ASSERT_TRUE(manager);
    ASSERT_TRUE(local_state);
    auto reservations = ReadBackupRestoreProfileReservations(local_state);
    ASSERT_TRUE(reservations);
    EXPECT_TRUE(reservations->empty());
    EXPECT_FALSE(manager->GetProfileByPath(target_path));
    EXPECT_FALSE(
        manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
            target_path));
    EXPECT_FALSE(base::PathExists(target_path));
    EXPECT_TRUE(VerifyProfileAndCacheDirectoryDeletion(target_path));
    EXPECT_EQ(GetProfile()->GetPath(), manager->GetLastUsedProfileDir());
    EXPECT_FALSE(BackupRestoreReservationRetirement::IsInProgress());
  }
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitCleanupBrowserTest,
                       CancelWhileReservePendingDeletesOnlyCreatedTarget) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  const base::FilePath target_path =
      manager->GetNextExpectedProfileDirectoryPath();

  Lifecycle lifecycle(manager, local_state);
  base::test::TestFuture<Lifecycle::ReserveResult> reserve_future;
  base::test::TestFuture<Lifecycle::PrecommitCleanupResult> cleanup_future;
  lifecycle.Reserve(GetProfile(), u"Cancelled during creation",
                    reserve_future.GetCallback());
  lifecycle.CancelPendingNewReservation(cleanup_future.GetCallback());

  auto reserve = reserve_future.Take();
  ASSERT_FALSE(reserve);
  EXPECT_EQ(BackupRestoreProfileReserveError::kCancelled, reserve.error());
  EXPECT_TRUE(cleanup_future.Take());
  ExpectRemoved(target_path);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitCleanupBrowserTest,
                       CancelAfterReserveNeedsNoPortableReceipt) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  const base::FilePath target_path = reserved->target_profile_path;

  EXPECT_TRUE(Cancel(lifecycle));
  ExpectRemoved(target_path);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitCleanupBrowserTest,
                       CancelAfterBindingClosesUninitializedCustody) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  auto bound = Bind(lifecycle, reserved->reservation_id,
                    base::Uuid::GenerateRandomV4().AsLowercaseString());
  ASSERT_TRUE(bound);

  EXPECT_TRUE(Cancel(lifecycle));
  ExpectRemoved(reserved->target_profile_path);

  base::test::TestFuture<Lifecycle::DormantTargetResult> stale_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    stale_future.GetCallback());
  auto stale = stale_future.Take();
  ASSERT_FALSE(stale);
  EXPECT_EQ(BackupRestoreDormantTargetError::kInvalidHandle, stale.error());
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitCleanupBrowserTest,
                       CancelWhileInitializationPendingDrainsSqlOwnerFirst) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  auto bound = Bind(lifecycle, reserved->reservation_id,
                    base::Uuid::GenerateRandomV4().AsLowercaseString());
  ASSERT_TRUE(bound);

  base::test::TestFuture<Lifecycle::DormantTargetResult> initialize_future;
  base::test::TestFuture<Lifecycle::PrecommitCleanupResult> cleanup_future;
  lifecycle.InitializeDormantTarget(std::move(*bound),
                                    initialize_future.GetCallback());
  lifecycle.CancelPendingNewReservation(cleanup_future.GetCallback());

  auto initialized = initialize_future.Take();
  ASSERT_FALSE(initialized);
  EXPECT_EQ(BackupRestoreDormantTargetError::kCancelled, initialized.error());
  EXPECT_TRUE(cleanup_future.Take());
  ExpectRemoved(reserved->target_profile_path);
}

}  // namespace
}  // namespace taffy
