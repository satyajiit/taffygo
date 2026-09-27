// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_BACKUP_RESTORE_PROFILE_RESOLUTION_TEST_SUPPORT_H_
#define TAFFY_TEST_RECOVERY_BACKUP_RESTORE_PROFILE_RESOLUTION_TEST_SUPPORT_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"

class PrefService;
class Profile;
class ProfileManager;

namespace taffy::test {

struct CommittedBackupRestoreCandidate {
  std::string reservation_id;
  std::string target_profile_id;
  base::FilePath target_profile_path;
};

// Creates one real hidden Android Profile, stages and commits a tombstone
// through the production dormant target, and closes its mutable storage lease.
// Portable recovery resolution remains untouched for the caller to exercise.
std::optional<CommittedBackupRestoreCandidate>
CreateCommittedBackupRestoreCandidate(Profile* source,
                                      ProfileManager* profile_manager,
                                      PrefService* local_state);

// Creates the same durable presentation and target but stops after journaling
// the commit intent. The target is pristine and its mutable lease is closed,
// so restart discovery can exercise inspect-only and requested read-only
// reconciliation without any portable resolution authority.
std::optional<CommittedBackupRestoreCandidate>
CreateInterruptedBackupRestoreCommit(Profile* source,
                                     ProfileManager* profile_manager,
                                     PrefService* local_state);

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_BACKUP_RESTORE_PROFILE_RESOLUTION_TEST_SUPPORT_H_
