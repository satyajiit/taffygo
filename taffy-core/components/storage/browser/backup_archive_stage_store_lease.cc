// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_archive_stage_store_lease.h"

#include <set>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"

namespace taffy::storage::backup::internal {
namespace {

struct LeaseRegistry {
  base::Lock lock;
  std::set<base::FilePath> directories GUARDED_BY(lock);
};

LeaseRegistry& Registry() {
  static base::NoDestructor<LeaseRegistry> registry;
  return *registry;
}

}  // namespace

// static
std::unique_ptr<BackupStageDirectoryLease> BackupStageDirectoryLease::Acquire(
    const base::FilePath& requested_directory) {
  if (!requested_directory.IsAbsolute() ||
      requested_directory.ReferencesParent() ||
      requested_directory.BaseName() !=
          base::FilePath(kBackupStagingDirectoryName)) {
    return nullptr;
  }

  // Resolve only the existing profile parent. The dedicated child may not
  // exist yet, and resolving the parent folds symlink aliases onto one key.
  const base::FilePath canonical_parent =
      base::MakeAbsoluteFilePath(requested_directory.DirName());
  if (canonical_parent.empty() || !base::DirectoryExists(canonical_parent) ||
      canonical_parent == canonical_parent.DirName()) {
    return nullptr;
  }
  const base::FilePath canonical_directory =
      canonical_parent.Append(kBackupStagingDirectoryName);

  {
    LeaseRegistry& registry = Registry();
    base::AutoLock guard(registry.lock);
    if (!registry.directories.insert(canonical_directory).second) {
      return nullptr;
    }
  }
  auto lease = std::unique_ptr<BackupStageDirectoryLease>(
      new BackupStageDirectoryLease(canonical_directory));

  // Inspect the destructive target only after exclusive ownership exists.
  if (base::IsLink(canonical_directory) ||
      (base::PathExists(canonical_directory) &&
       !base::DirectoryExists(canonical_directory))) {
    return nullptr;
  }
  return lease;
}

BackupStageDirectoryLease::BackupStageDirectoryLease(base::FilePath directory)
    : directory_(std::move(directory)) {}

BackupStageDirectoryLease::~BackupStageDirectoryLease() {
  LeaseRegistry& registry = Registry();
  base::AutoLock guard(registry.lock);
  CHECK_EQ(registry.directories.erase(directory_), 1u);
}

}  // namespace taffy::storage::backup::internal
