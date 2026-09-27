// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_TEST_SUPPORT_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_TEST_SUPPORT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {

class DormantBackupRestoreTarget;

class DormantBackupRestoreCommitTestPeer {
 public:
  static void SetAfterCommit(DormantBackupRestoreTarget* target,
                             base::OnceClosure callback);
  static bool ExecuteTargetSql(DormantBackupRestoreTarget* target,
                               std::string_view statement);
  static std::optional<int64_t> CountTargetRows(
      DormantBackupRestoreTarget* target,
      std::string_view table);
  static base::FilePath StageDatabasePath(
      const DormantBackupRestoreTarget& target);
};

namespace test {

inline constexpr char kRestoreSourceProfileId[] =
    "11111111-1111-4111-8111-111111111111";
inline constexpr char kRestoreLibraryActiveId[] =
    "11111111111111111111111111111111";
inline constexpr char kRestoreMemoryActiveId[] =
    "22222222222222222222222222222222";
inline constexpr char kRestoreLibraryTombstoneId[] =
    "33333333333333333333333333333333";
inline constexpr char kRestoreMemoryTombstoneId[] =
    "44444444444444444444444444444444";

struct DormantBackupRestoreScenario {
  core_service::mojom::BackupRestorePlanResultPtr plan;
  std::vector<uint8_t> payload;
};

std::optional<DormantBackupRestoreScenario> MakeMixedRestoreScenario(
    std::string target_profile_id);
DormantBackupRestoreScenario MakeEmptyRestoreScenario(
    std::string target_profile_id);
core_service::mojom::BackupRestoreStageAuthorizationPtr MakeStageAuthorization(
    const core_service::mojom::BackupRestorePlanResult& plan,
    uint64_t deadline_monotonic_ms = 0u);
core_service::mojom::BackupRestoreCommitAuthorizationPtr
MakeCommitAuthorization(
    const core_service::mojom::BackupRestorePlanResult& plan,
    uint64_t deadline_monotonic_ms = 0u);

}  // namespace test
}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_TEST_SUPPORT_H_
