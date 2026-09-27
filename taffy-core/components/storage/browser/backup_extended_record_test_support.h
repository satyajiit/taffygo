// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_EXTENDED_RECORD_TEST_SUPPORT_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_EXTENDED_RECORD_TEST_SUPPORT_H_

#include <stdint.h>

#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup::test {

struct DormantBackupRestoreScenario;

inline constexpr char kExtendedWorkspaceId[] =
    "11111111111111111111111111111111";
inline constexpr char kExtendedDeletedWorkspaceId[] =
    "22222222222222222222222222222222";
inline constexpr char kExtendedAuthoredSkillId[] = "compare-products";
inline constexpr char kExtendedLearnedProcedureId[] = "capture-details";

core_service::mojom::WorkspaceRestoreRecordPtr SavedWorkspace(
    std::string_view workspace_id,
    uint64_t revision,
    bool saved = true);
core_service::mojom::SkillRecordPtr CurrentProcedure(
    std::string_view skill_id,
    uint32_t version,
    core_service::mojom::BackupRecordKind kind,
    core_service::mojom::SkillStatus status =
        core_service::mojom::SkillStatus::kDraft);

// Appends one active saved workspace, one sanitized workspace deletion and the
// current authored/learned definitions to an existing new-profile test plan.
bool AppendExtendedRestoreRecords(DormantBackupRestoreScenario* scenario);

}  // namespace taffy::storage::backup::test

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_EXTENDED_RECORD_TEST_SUPPORT_H_
