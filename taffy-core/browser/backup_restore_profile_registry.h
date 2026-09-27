// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_RESTORE_PROFILE_REGISTRY_H_
#define TAFFY_BROWSER_BACKUP_RESTORE_PROFILE_REGISTRY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/types/expected.h"

class PrefService;

namespace taffy {

// This is a physical browser lifecycle only. None of these values is semantic
// restore authority; that authority is minted consumptively by the source Core
// protocol and is intentionally absent from this registry.
enum class BackupRestoreProfilePhysicalState : uint8_t {
  kPathReserved,
  kProfileCreated,
};

enum class BackupRestoreProfileRegistryError : uint8_t {
  kUnavailable,
  kCorrupt,
  kInvalidArgument,
  kBusy,
  kNotFound,
  kWrongPhysicalState,
};

enum class BackupRestoreProfileQuarantineStatus : uint8_t {
  kNotQuarantined,
  kQuarantined,
  kRegistryUnavailable,
  kRegistryCorrupt,
  kInvalidProfilePath,
};

struct BackupRestoreProfileReservation {
  std::string reservation_id;
  base::FilePath source_profile_base_name;
  base::FilePath target_profile_base_name;
  BackupRestoreProfilePhysicalState physical_state =
      BackupRestoreProfilePhysicalState::kPathReserved;
  std::optional<std::string> target_profile_id;

  bool operator==(const BackupRestoreProfileReservation&) const = default;
};

using BackupRestoreProfileReservationList =
    base::expected<std::vector<BackupRestoreProfileReservation>,
                   BackupRestoreProfileRegistryError>;

// Reads and validates the complete persisted registry. One malformed entry or
// an impossible multi-reservation state is an error, not an empty list.
BackupRestoreProfileReservationList ReadBackupRestoreProfileReservations(
    const PrefService* local_state);

// Claims one safe target basename while it is still absent from the profile
// store. The single-reservation bound gives interrupted work unambiguous
// custody across browser restarts.
base::expected<void, BackupRestoreProfileRegistryError>
ReserveBackupRestoreProfilePath(PrefService* local_state,
                                std::string reservation_id,
                                base::FilePath source_profile_base_name,
                                base::FilePath target_profile_base_name);

// Records only that Chromium finished constructing the already-reserved
// physical profile. It does not make the profile visible or runnable.
base::expected<void, BackupRestoreProfileRegistryError>
MarkBackupRestoreProfileCreated(PrefService* local_state,
                                const std::string& reservation_id);

// Binds the fresh target Core identity before dormant storage initialization.
// Rebinding to a different identity is refused. The caller must durably
// witness this write before creating the target database.
base::expected<void, BackupRestoreProfileRegistryError>
BindBackupRestoreTargetProfileId(PrefService* local_state,
                                 const std::string& reservation_id,
                                 std::string target_profile_id);

// Answers whether a profile path must be denied list/find/lease/activation and
// Core creation. Unavailable and corrupt are distinct so production callers
// can fail closed while isolated tests without application prefs remain
// diagnosable.
BackupRestoreProfileQuarantineStatus BackupRestoreQuarantineForProfilePath(
    const PrefService* local_state,
    const base::FilePath& profile_path);

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_RESTORE_PROFILE_REGISTRY_H_
