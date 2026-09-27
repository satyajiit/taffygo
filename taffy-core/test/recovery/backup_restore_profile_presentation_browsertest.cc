// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
#include "taffy/browser/backup_restore_recovery_presentation.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Kind = mojom::BackupRecordKind;

mojom::BackupRestorePlanResultPtr Plan(const std::string& target_profile_id) {
  auto operation = mojom::OperationEnvelope::New(
      "presentation-plan", 7u, 0u,
      base::TimeTicks::Now().since_origin().InMilliseconds() + 60'000u,
      "presentation-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, target_profile_id);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "presentation-archive";
  plan->snapshot_sha256.assign(32u, 1u);
  plan->target = target.Clone();
  plan->entries.push_back(mojom::BackupRestorePlanEntry::New(
      Kind::kMemoryRecord, "memory-1", 1u,
      mojom::BackupRestoreAction::kStageCreate, 1u,
      mojom::BackupRecordState::kActive, 4u, std::vector<uint8_t>(32u, 3u)));
  plan->has_conflicts = false;
  plan->confirmation_sha256.assign(32u, 2u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), "11111111-1111-4111-8111-111111111111",
      std::move(target), plan->backup_id, plan->snapshot_sha256,
      plan->confirmation_sha256);
  return plan;
}

class BackupRestoreProfilePresentationBrowserTest : public PlatformBrowserTest {
 protected:
  BrowserProfilesRestoreLifecycle::ReserveResult Reserve(
      BrowserProfilesRestoreLifecycle& lifecycle) {
    base::test::TestFuture<BrowserProfilesRestoreLifecycle::ReserveResult>
        future;
    lifecycle.Reserve(GetProfile(), u"Restored profile", future.GetCallback());
    return future.Take();
  }
};

IN_PROC_BROWSER_TEST_F(BackupRestoreProfilePresentationBrowserTest,
                       PreviewWaitsForExactDurableRegistryPresentation) {
  ASSERT_TRUE(g_browser_process);
  ProfileManager* const manager = g_browser_process->profile_manager();
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(manager);
  ASSERT_TRUE(local_state);
  BrowserProfilesRestoreLifecycle lifecycle(manager, local_state);
  auto reservation = Reserve(lifecycle);
  ASSERT_TRUE(reservation);
  const std::string target_profile_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::BindResult>
      binding_future;
  lifecycle.BindTargetProfileId(reservation->reservation_id, target_profile_id,
                                binding_future.GetCallback());
  auto binding = binding_future.Take();
  ASSERT_TRUE(binding);
  base::test::TestFuture<BrowserProfilesRestoreLifecycle::DormantTargetResult>
      initialization_future;
  lifecycle.InitializeDormantTarget(std::move(*binding),
                                    initialization_future.GetCallback());
  auto initialized = initialization_future.Take();
  ASSERT_TRUE(initialized);
  ASSERT_TRUE(initialized->target);

  auto plan = Plan(target_profile_id);
  const std::array selection = {Kind::kMemoryRecord,
                                Kind::kAssistantConfiguration};
  base::test::TestFuture<BackupRestoreRecoveryPresentationResult> future;
  lifecycle.PersistRecoveryPresentation(
      reservation->reservation_id, u"Restored profile",
      std::vector<Kind>(selection.begin(), selection.end()), std::move(plan),
      future.GetCallback());
  auto persisted = future.Take();

  ASSERT_TRUE(persisted);
  ASSERT_EQ(2u, persisted->selected_classes.size());
  EXPECT_EQ(Kind::kAssistantConfiguration, persisted->selected_classes[0].kind);
  EXPECT_EQ((std::array<uint32_t, 6>{}),
            persisted->selected_classes[0].action_counts);
  EXPECT_EQ(Kind::kMemoryRecord, persisted->selected_classes[1].kind);
  EXPECT_EQ((std::array<uint32_t, 6>{1u, 0u, 0u, 0u, 0u, 0u}),
            persisted->selected_classes[1].action_counts);
  auto read_back = ReadBackupRestoreRecoveryPresentation(
      local_state, reservation->reservation_id);
  ASSERT_TRUE(read_back);
  EXPECT_EQ(*persisted, *read_back);

  Profile* const target =
      manager->GetProfileByPath(reservation->target_profile_path);
  ProfileAttributesEntry* const entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          reservation->target_profile_path);
  ASSERT_TRUE(target);
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->IsOmitted());
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(target));
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                local_state, reservation->target_profile_path));
}

}  // namespace
}  // namespace taffy
