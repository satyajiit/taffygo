// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_RESTORE_PREFERENCE_READBACK_H_
#define TAFFY_BROWSER_BACKUP_RESTORE_PREFERENCE_READBACK_H_

#include <cstddef>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"

namespace taffy {

inline constexpr size_t kMaximumBackupRestorePreferenceFileBytes =
    64u * 1024u * 1024u;

// One exact value that must be present in a JSON-backed preference file after
// its ordinary PrefService write queue drains. The explicit disk readback is
// necessary because CommitPendingWrite's callback reports queue ordering, not
// whether JsonPrefStore successfully replaced the file.
// For ClearPref, set must_be_absent and leave expected as its default NONE.
// Absence is distinct from a stored JSON null or a malformed parent object.
struct BackupRestorePreferenceWriteWitness {
  std::string dotted_path;
  base::Value expected;
  bool must_be_absent = false;
};

// Blocking, thread-pool-only verifier. It is deliberately content-agnostic and
// emits no values or file contents; callers receive only an exact-match bit.
bool BackupRestorePreferenceFileMatches(
    const base::FilePath& preference_file,
    std::vector<BackupRestorePreferenceWriteWitness> witnesses);

// The stronger barrier required before a dormant restore writes its target.
// Reads one held regular file, verifies the witnesses, then synchronizes that
// file and its parent directory and checks their path identities again.
// Chromium's ordinary preference writer flushes before rename; its completion
// and a subsequent read alone do not synchronize that directory entry.
//
// The caller must derive the path from app-private profile metadata and own
// its write ordering. This helper checks file shape, not profile ownership.
// A replacement is refused, not followed or retried. Neither
// this barrier nor its tests constitute a device power-loss recovery drill.
// Unsupported platforms fail closed; this product boundary is Android/Linux.
bool BackupRestorePreferenceFileMatchesAndSync(
    const base::FilePath& preference_file,
    std::vector<BackupRestorePreferenceWriteWitness> witnesses);

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_RESTORE_PREFERENCE_READBACK_H_
