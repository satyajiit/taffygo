// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/profile_backup_coordinator_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestoreBindingPtr CancelledBinding(
    const std::string& source_profile_id,
    const std::string& target_profile_id,
    uint64_t generation) {
  return mojom::BackupRestoreBinding::New(
      mojom::OperationEnvelope::New("precommit-cleanup-plan", generation, 0u,
                                    NowMonotonicMillis() + 60'000u,
                                    "precommit-cleanup-plan-once"),
      source_profile_id,
      mojom::BackupRestoreTarget::New(
          mojom::BackupRestoreTargetKind::kNewRegularProfile,
          target_profile_id),
      "precommit-cleanup-backup", std::vector<uint8_t>(32u, 3u),
      std::vector<uint8_t>(32u, 4u));
}

class BackupRestoreProfilePrecommitReceiptBrowserTest
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

  Lifecycle::DormantTargetResult Initialize(
      Lifecycle& lifecycle,
      BoundBackupRestoreProfileHandle handle) {
    base::test::TestFuture<Lifecycle::DormantTargetResult> future;
    lifecycle.InitializeDormantTarget(std::move(handle), future.GetCallback());
    return future.Take();
  }

  Lifecycle::PrecommitCleanupResult Cleanup(
      Lifecycle& lifecycle,
      PrecommitBackupRestoreCleanupHandle handle,
      ProfileBackupPrecommitCancellationReceipt receipt) {
    base::test::TestFuture<Lifecycle::PrecommitCleanupResult> future;
    lifecycle.CleanupNewPrecommitReservation(
        std::move(handle), std::move(receipt), future.GetCallback());
    return future.Take();
  }

  CoreServiceManager* PrepareSourceCore() {
    CoreServiceManager* const manager =
        CoreServiceManagerFactory::GetForProfile(GetProfile());
    if (!manager) {
      return nullptr;
    }
    base::test::TestFuture<bool> ready;
    manager->PrepareForCoreApi(ready.GetCallback());
    return ready.Take() &&
                   manager->availability() ==
                       CoreServiceManager::Availability::kReady &&
                   !manager->browser_profile_id().empty()
               ? manager
               : nullptr;
  }

  void DrainTarget(std::unique_ptr<ProfileBackupRestoreTarget>* target) {
    ASSERT_TRUE(target);
    ASSERT_TRUE(*target);
    base::test::TestFuture<bool> abandoned;
    (*target)->Abandon(abandoned.GetCallback());
    ASSERT_TRUE(abandoned.Take());
    target->reset();
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

  void ExpectRetained(const base::FilePath& target_path) {
    ProfileManager* const manager = g_browser_process->profile_manager();
    PrefService* const local_state = g_browser_process->local_state();
    ASSERT_TRUE(manager);
    ASSERT_TRUE(local_state);
    auto reservations = ReadBackupRestoreProfileReservations(local_state);
    ASSERT_TRUE(reservations);
    ASSERT_EQ(1u, reservations->size());
    EXPECT_TRUE(manager->GetProfileByPath(target_path));
    ProfileAttributesEntry* const entry =
        manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
            target_path);
    ASSERT_TRUE(entry);
    EXPECT_TRUE(entry->IsOmitted());
    EXPECT_TRUE(entry->IsEphemeral());
    EXPECT_TRUE(base::PathExists(target_path));
    EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
              BackupRestoreQuarantineForProfilePath(local_state, target_path));
    EXPECT_FALSE(BackupRestoreReservationRetirement::IsInProgress());
  }
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitReceiptBrowserTest,
                       ReceiptWithoutPlanDeletesExactTransferredTarget) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  CoreServiceManager* const source_core = PrepareSourceCore();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source_core);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound);
  auto initialized = Initialize(lifecycle, std::move(*bound));
  ASSERT_TRUE(initialized);
  DrainTarget(&initialized->target);

  auto receipt = ProfileBackupCoordinatorTestPeer::
      MintPrecommitCancellationReceiptForTesting(
          "precommit-no-plan", source_core->browser_profile_id(),
          target_profile_id, nullptr);
  EXPECT_TRUE(
      Cleanup(lifecycle, std::move(initialized->cleanup), std::move(receipt)));
  ExpectRemoved(reserved->target_profile_path);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitReceiptBrowserTest,
                       ReceiptWithExactCancelledPlanDeletesTargetAndCache) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  CoreServiceManager* const source_core = PrepareSourceCore();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source_core);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound);
  auto initialized = Initialize(lifecycle, std::move(*bound));
  ASSERT_TRUE(initialized);
  DrainTarget(&initialized->target);
  base::FilePath cache_path;
  chrome::GetUserCacheDirectory(reserved->target_profile_path, &cache_path);
  ASSERT_FALSE(cache_path.empty());
  ASSERT_NE(reserved->target_profile_path, cache_path);
  ASSERT_TRUE(base::CreateDirectory(cache_path));
  ASSERT_TRUE(base::WriteFile(cache_path.AppendASCII("cache-sentinel"),
                              "derived cache"));

  auto receipt = ProfileBackupCoordinatorTestPeer::
      MintPrecommitCancellationReceiptForTesting(
          "precommit-with-plan", source_core->browser_profile_id(),
          target_profile_id,
          CancelledBinding(source_core->browser_profile_id(), target_profile_id,
                           source_core->service_generation()));
  EXPECT_TRUE(
      Cleanup(lifecycle, std::move(initialized->cleanup), std::move(receipt)));
  EXPECT_FALSE(base::PathExists(cache_path));
  ExpectRemoved(reserved->target_profile_path);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitReceiptBrowserTest,
                       MovedReceiptCannotDeleteTransferredTarget) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound);
  auto initialized = Initialize(lifecycle, std::move(*bound));
  ASSERT_TRUE(initialized);
  DrainTarget(&initialized->target);

  auto receipt = ProfileBackupCoordinatorTestPeer::
      MintPrecommitCancellationReceiptForTesting(
          "precommit-consumed-receipt", "11111111-1111-4111-8111-111111111111",
          target_profile_id, nullptr);
  auto consumed = std::move(receipt);
  static_cast<void>(consumed);
  auto result =
      Cleanup(lifecycle, std::move(initialized->cleanup), std::move(receipt));
  ASSERT_FALSE(result);
  EXPECT_EQ(BackupRestorePrecommitCleanupError::kInvalidReceipt,
            result.error());
  ExpectRetained(reserved->target_profile_path);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitReceiptBrowserTest,
                       ForeignSourceReceiptRetainsTransferredTarget) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  CoreServiceManager* const source_core = PrepareSourceCore();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source_core);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound);
  auto initialized = Initialize(lifecycle, std::move(*bound));
  ASSERT_TRUE(initialized);
  DrainTarget(&initialized->target);

  const std::string foreign_source_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  ASSERT_NE(source_core->browser_profile_id(), foreign_source_profile_id);
  auto receipt = ProfileBackupCoordinatorTestPeer::
      MintPrecommitCancellationReceiptForTesting("precommit-foreign-source",
                                                 foreign_source_profile_id,
                                                 target_profile_id, nullptr);
  auto result =
      Cleanup(lifecycle, std::move(initialized->cleanup), std::move(receipt));
  ASSERT_FALSE(result);
  EXPECT_EQ(BackupRestorePrecommitCleanupError::kProfileStateRefused,
            result.error());
  ExpectRetained(reserved->target_profile_path);
}

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePrecommitReceiptBrowserTest,
                       CommitIntentRetainsQuarantinedTransferredTarget) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  CoreServiceManager* const source_core = PrepareSourceCore();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  ASSERT_TRUE(source_core);

  Lifecycle lifecycle(manager, local_state);
  auto reserved = Reserve(lifecycle);
  ASSERT_TRUE(reserved);
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto bound = Bind(lifecycle, reserved->reservation_id, target_profile_id);
  ASSERT_TRUE(bound);
  auto initialized = Initialize(lifecycle, std::move(*bound));
  ASSERT_TRUE(initialized);
  DrainTarget(&initialized->target);

  auto binding =
      CancelledBinding(source_core->browser_profile_id(), target_profile_id,
                       source_core->service_generation());
  auto witness = mojom::BackupRestoreCandidateWitness::New(
      std::vector<mojom::BackupRecordKind>{
          mojom::BackupRecordKind::kMemoryRecord},
      1u, std::vector<uint8_t>(32u, 5u));
  // The journal answers with a record pointer, so the expected's own bool
  // conversion is disabled; assert on the value's presence directly.
  ASSERT_TRUE(BeginBackupRestoreCommitIntent(local_state,
                                             reserved->reservation_id,
                                             *binding, *witness,
                                             "escaped-commit-intent")
                  .has_value());
  auto receipt = ProfileBackupCoordinatorTestPeer::
      MintPrecommitCancellationReceiptForTesting(
          "precommit-history-refusal", source_core->browser_profile_id(),
          target_profile_id, binding.Clone());
  auto result =
      Cleanup(lifecycle, std::move(initialized->cleanup), std::move(receipt));
  ASSERT_FALSE(result);
  EXPECT_EQ(BackupRestorePrecommitCleanupError::kHistoryPresent,
            result.error());
  ExpectRetained(reserved->target_profile_path);
}

}  // namespace
}  // namespace taffy
