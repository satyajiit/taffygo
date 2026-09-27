// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <string>
#include <utility>

#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_profile_registry.h"
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

class BackupRestoreProfileDiscoveryBrowserTest : public PlatformBrowserTest {
 protected:
  Result Discover(BrowserProfilesRestoreLifecycle& lifecycle,
                  Profile* source,
                  Mode mode = Mode::kInspectOnly) {
    base::test::TestFuture<Result> future;
    lifecycle.DiscoverInterruptedBackupRestore(source, mode,
                                               future.GetCallback());
    return future.Take();
  }

  CoreServiceManager* ReadySourceCore() {
    CoreServiceManager* const manager =
        CoreServiceManagerFactory::GetForProfile(GetProfile());
    if (!manager) {
      return nullptr;
    }
    base::test::TestFuture<bool> ready;
    manager->PrepareForCoreApi(ready.GetCallback());
    return ready.Take() && manager->availability() ==
                               CoreServiceManager::Availability::kReady
               ? manager
               : nullptr;
  }
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfileDiscoveryBrowserTest,
                       NoneAndPrecommitNeverExposeAnAction) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);

  BrowserProfilesRestoreLifecycle discovery(manager, local_state);
  auto none = Discover(discovery, GetProfile());
  ASSERT_TRUE(none);
  EXPECT_EQ(Status::kNone, none->status);
  EXPECT_FALSE(none->candidate);

  BrowserProfilesRestoreLifecycle creator(manager, local_state);
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
      reserved_future;
  creator.Reserve(GetProfile(), u"Unfinished restore",
                  reserved_future.GetCallback());
  auto reserved = reserved_future.Take();
  ASSERT_TRUE(reserved);

  auto precommit = Discover(discovery, GetProfile());
  ASSERT_TRUE(precommit);
  EXPECT_EQ(Status::kPrecommit, precommit->status);
  EXPECT_FALSE(precommit->candidate);

  base::test::TestFuture<
      BrowserProfilesRestoreLifecycle::PrecommitCleanupResult>
      cleanup;
  creator.CancelPendingNewReservation(cleanup.GetCallback());
  EXPECT_TRUE(cleanup.Take());
}

IN_PROC_BROWSER_TEST_F(
    BackupRestoreProfileDiscoveryBrowserTest,
    SourceMatchesBeforeLabelAndFreshCandidatePreservesEmptySelection) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  CoreServiceManager* const source_core = ReadySourceCore();
  ASSERT_TRUE(source_core);
  auto created = test::CreateCommittedBackupRestoreCandidate(
      GetProfile(), manager, local_state);
  ASSERT_TRUE(created);
  Profile* const hidden_target =
      manager->GetProfileByPath(created->target_profile_path);
  ASSERT_TRUE(hidden_target);

  BrowserProfilesRestoreLifecycle discovery(manager, local_state);
  auto wrong_source = Discover(discovery, hidden_target);
  ASSERT_TRUE(wrong_source);
  EXPECT_EQ(Status::kSourceUnavailable, wrong_source->status);
  EXPECT_FALSE(wrong_source->candidate);

  ProfileAttributesEntry* const target_entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          created->target_profile_path);
  ASSERT_TRUE(target_entry);
  // Chromium does not persist omission. Model a reconstructed profile store:
  // the sole registry quarantine plus ephemeral metadata remains the durable
  // hidden-state proof and must still permit truthful restart discovery.
  target_entry->SetIsOmitted(false);
  EXPECT_FALSE(target_entry->IsOmitted());

  auto recovered = Discover(discovery, GetProfile());
  ASSERT_TRUE(recovered);
  ASSERT_EQ(Status::kRollbackAvailable, recovered->status);
  ASSERT_TRUE(recovered->candidate);
  EXPECT_EQ(Action::kReview, recovered->candidate->action);
  EXPECT_EQ(created->reservation_id, recovered->candidate->reservation_id);
  const auto& presentation = recovered->candidate->presentation;
  EXPECT_EQ(u"Resolved backup", presentation.target_profile_label);
  EXPECT_EQ(source_core->browser_profile_id(), presentation.source_profile_id);
  ASSERT_EQ(2u, presentation.selected_classes.size());
  EXPECT_EQ(mojom::BackupRecordKind::kAssistantConfiguration,
            presentation.selected_classes[0].kind);
  EXPECT_EQ((std::array<uint32_t, 6>{}),
            presentation.selected_classes[0].action_counts);
  EXPECT_EQ(mojom::BackupRecordKind::kMemoryRecord,
            presentation.selected_classes[1].kind);

  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  ASSERT_TRUE(reservations);
  ASSERT_EQ(1u, reservations->size());
  EXPECT_EQ(created->reservation_id, reservations->front().reservation_id);
  EXPECT_TRUE(target_entry->IsEphemeral());
  EXPECT_FALSE(target_entry->IsOmitted());
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(hidden_target));

  // A valid legacy v1 reservation can retain history, but it has no exact
  // original selection. Discovery never reconstructs that presentation from
  // witness kinds or counts.
  base::DictValue registry =
      local_state
          ->GetDict(application_preferences::kBackupRestoreProfileReservations)
          .Clone();
  base::DictValue* const entry = registry.FindDict(created->reservation_id);
  ASSERT_TRUE(entry);
  entry->Set("version", 1);
  EXPECT_TRUE(entry->Remove("recovery_presentation"));
  local_state->SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      std::move(registry));
  base::test::TestFuture<void> drained;
  local_state->CommitPendingWrite(drained.GetCallback());
  ASSERT_TRUE(drained.Wait());

  auto legacy = Discover(discovery, GetProfile());
  ASSERT_TRUE(legacy);
  EXPECT_EQ(Status::kPresentationUnavailable, legacy->status);
  EXPECT_FALSE(legacy->candidate);
}

}  // namespace
}  // namespace taffy
