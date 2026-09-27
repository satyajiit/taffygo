// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_PRESENTATION_H_
#define TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_PRESENTATION_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

class PrefService;

namespace taffy {

// One original selected class and its bounded, content-free plan counts. Rows
// with six zero counts are retained: the candidate witness later records only
// classes that produced rows, so it cannot reconstruct the person's original
// selection.
struct BackupRestoreRecoveryPresentationClass {
  core_service::mojom::BackupRecordKind kind =
      core_service::mojom::BackupRecordKind::kAssistantConfiguration;
  // Indexed by the generated closed BackupRestoreAction wire value.
  std::array<uint32_t, 6> action_counts{};

  bool operator==(const BackupRestoreRecoveryPresentationClass&) const =
      default;
};

// Durable, content-free facts needed to present one hidden restore candidate
// after restart. Stable identity fields are copied from the exact accepted
// plan binding. No operation envelope, token, service generation, portable
// authority, record identity or payload is represented here.
struct BackupRestoreRecoveryPresentation {
  std::u16string target_profile_label;
  std::string source_profile_id;
  std::string target_profile_id;
  std::string backup_id;
  std::array<uint8_t, 32> snapshot_sha256{};
  std::array<uint8_t, 32> confirmation_sha256{};
  std::vector<BackupRestoreRecoveryPresentationClass> selected_classes;
  bool has_conflicts = false;
  bool can_stage = false;

  bool operator==(const BackupRestoreRecoveryPresentation&) const = default;
};

using BackupRestoreRecoveryPresentationResult =
    base::expected<BackupRestoreRecoveryPresentation,
                   BackupRestoreProfileRegistryError>;

// Attaches presentation to one exact created/bound reservation before any
// recovery journal fact exists. Counts, conflict posture and stable identities
// are derived here from the accepted plan rather than trusted from a caller.
// The returned value is the exact in-memory registry representation. As with
// other registry mutations, the caller must drain Local State and establish an
// exact file readback witness before exposing it as restart-durable UI state.
// Repeating the exact attachment is idempotent; a different attachment or a
// non-empty journal is refused.
BackupRestoreRecoveryPresentationResult AttachBackupRestoreRecoveryPresentation(
    PrefService* local_state,
    const std::string& reservation_id,
    std::u16string target_profile_label,
    base::span<const core_service::mojom::BackupRecordKind> original_selection,
    const core_service::mojom::BackupRestorePlanResult& exact_plan);

// Reads only an explicitly attached v2 presentation. A valid legacy v1
// reservation returns kNotFound; a v2 entry missing its required presentation
// is corrupt. Callers must not infer selected classes or counts from recovery
// witness kinds/counts. A present journal is accepted only when its first
// binding has the same exact reservation/source/target/archive/snapshot/
// confirmation identity, its record count equals all six counters, and its
// selection equals precisely the non-empty rows. Extra zero-count rows remain
// the original-selection proof.
BackupRestoreRecoveryPresentationResult ReadBackupRestoreRecoveryPresentation(
    const PrefService* local_state,
    const std::string& reservation_id);

namespace backup_restore_recovery_presentation_internal {

base::expected<BackupRestoreRecoveryPresentation,
               BackupRestoreProfileRegistryError>
Build(
    std::u16string target_profile_label,
    base::span<const core_service::mojom::BackupRecordKind> original_selection,
    const core_service::mojom::BackupRestorePlanResult& exact_plan);

base::expected<base::DictValue, BackupRestoreProfileRegistryError> Encode(
    const BackupRestoreRecoveryPresentation& presentation);

base::expected<BackupRestoreRecoveryPresentation,
               BackupRestoreProfileRegistryError>
Decode(const base::Value& value);

bool MatchesRecoveryBinding(
    const BackupRestoreRecoveryPresentation& presentation,
    std::string_view reservation_id,
    const core_service::mojom::BackupRestoreRecoveryBinding& binding);

}  // namespace backup_restore_recovery_presentation_internal
}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_PRESENTATION_H_
