// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RESOLUTION_INTERNAL_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RESOLUTION_INTERNAL_H_

#include <string_view>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/types/expected.h"

namespace taffy::storage::backup::restore_resolution_internal {

bool IsCanonicalUuidV4(std::string_view value);
// True when |path| is a symbolic link a path check must refuse.
//
// Android reaches every application-private file through /data/user/<n>, a
// symbolic link into /data/data that the platform installs at boot. The data
// directory the framework hands an application is named through it, so no path
// the browser can name avoids it, and a walk that refuses every symbolic link
// ancestor refuses every profile on the device. The first device run of these
// suites is where that showed: 48 unit tests failed in one guard because
// `/data/user/0` is a link.
//
// The one link accepted here is one this process could not have created: owned
// by the superuser, while this process is not the superuser. A link that root
// installed is not an attack this process could have defended against anyway.
// Every link this application, or any other application, could plant is still
// refused, which is what keeps the profile and ancestor refusals real.
bool IsRefusableSymbolicLink(const base::FilePath& path);
bool IsRegularDirectoryWithoutLinks(const base::FilePath& path);
bool HasNoSqliteSidecars(const base::FilePath& database_path);
bool PathIsAbsent(const base::FilePath& path);
bool IsCanonicalStageChild(const base::FilePath& child);
base::expected<std::vector<base::FilePath>, base::File::Error> DirectChildren(
    const base::FilePath& directory);
bool SameOpenedPath(const base::File& held,
                    const base::FilePath& path,
                    bool directory);
bool OpenedPathWasUnlinked(const base::File& held,
                           const base::FilePath& path,
                           bool directory);
// Removes only the directory entry that still names |held_child| beneath the
// already-opened exact parent, then synchronizes that parent. If a previous
// attempt already removed the entry but failed the sync, this performs only
// the missing sync and is therefore safe to retry.
bool RemoveOpenedChildAndSync(const base::File& held_parent,
                              const base::FilePath& parent_path,
                              const base::File& held_child,
                              const base::FilePath& child_path,
                              bool directory);
bool SyncOpenedDirectory(const base::File& directory);
base::File OpenPath(const base::FilePath& path);

}  // namespace taffy::storage::backup::restore_resolution_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_RESOLUTION_INTERNAL_H_
