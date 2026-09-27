// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_PROJECTION_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_PROJECTION_H_

#include <string>
#include <string_view>
#include <vector>

#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace sql {
class Database;
}

namespace taffy::storage::backup::restore_target_internal {

enum class SchemaMatch {
  kExact,
  kMismatch,
  kUnavailable,
};

enum class ProjectionMatch {
  kExact,
  kMismatch,
  kUnavailable,
};

enum class ImportedMetadataRole {
  kRecord = 0,
  kLibraryCollection = 1,
  kMemoryCollection = 2,
};

// The digest is a versioned, domain-separated encoding of every current
// BackupRecordDescriptor field. Descriptor order is canonical and selection is
// the sorted set of kinds actually present, not the person's manifest choice.
// Empty is valid only for the exact zero-record projection.
core_service::mojom::BackupRestoreCandidateWitnessPtr BuildCandidateWitness(
    const std::vector<BackupSnapshotRecord>& records);
bool IsValidCandidateWitness(
    const core_service::mojom::BackupRestoreCandidateWitness& witness);
bool IsExactCandidateWitness(
    const core_service::mojom::BackupRestoreCandidateWitness& left,
    const core_service::mojom::BackupRestoreCandidateWitness& right);

// Content-free local metadata is reproducible from the immutable candidate
// witness, target identity and exact record/collection role. It never carries
// a stage path or source effect identity.
std::string ImportedMetadataId(
    std::string_view target_profile_id,
    const core_service::mojom::BackupRestoreCandidateWitness& witness,
    ImportedMetadataRole role,
    core_service::mojom::BackupRecordKind kind,
    std::string_view stable_id);

// Reconciliation refuses any schema other than the generated head. Projection
// matching reads every supported class and requires all other tables to remain
// empty, in addition to exact deterministic imported metadata.
SchemaMatch InspectCurrentSchema(sql::Database* database,
                                 std::string_view target_profile_id);
ProjectionMatch InspectPristineProjection(sql::Database* database,
                                          std::string_view target_profile_id);
ProjectionMatch InspectCommittedProjection(
    sql::Database* database,
    std::string_view target_profile_id,
    const core_service::mojom::BackupRestoreCandidateWitness& expected);

}  // namespace taffy::storage::backup::restore_target_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_PROJECTION_H_
