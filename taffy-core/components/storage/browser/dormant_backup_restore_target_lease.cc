// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"

#include <set>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"

namespace taffy::storage::backup {
namespace {

struct LeaseRegistry {
  base::Lock lock;
  std::set<base::FilePath> paths GUARDED_BY(lock);
};

LeaseRegistry& Registry() {
  static base::NoDestructor<LeaseRegistry> registry;
  return *registry;
}

}  // namespace

// The lease is keyed by the resolved directory plus the exact trailing names
// the caller supplied, and no longer requires that key to equal the path the
// caller passed. Requiring equality meant requiring a caller to hold a path
// with no symbolic link anywhere in it, which is unobtainable on Android: the
// application data directory the framework hands the browser is named through
// /data/user/<n>, a boot-time link into /data/data. Resolving instead of
// refusing is also the stronger rule for what a lease is for — two names for
// one physical target now collide rather than each taking a lease of its own.
// Refusing a symbolically-reached profile remains the path checks' job, and
// they run before this.
// static
std::unique_ptr<DormantBackupRestoreTargetLease>
DormantBackupRestoreTargetLease::TryAcquire(
    const base::FilePath& canonical_database_path) {
  if (!canonical_database_path.IsAbsolute() ||
      canonical_database_path.ReferencesParent() ||
      canonical_database_path.empty()) {
    return nullptr;
  }
  const base::FilePath database_directory = canonical_database_path.DirName();
  const base::FilePath canonical_profile =
      base::MakeAbsoluteFilePath(database_directory.DirName());
  if (canonical_profile.empty() || database_directory.BaseName().empty() ||
      canonical_database_path.BaseName().empty()) {
    return nullptr;
  }
  return Acquire(canonical_profile.Append(database_directory.BaseName())
                     .Append(canonical_database_path.BaseName()));
}

// static
std::unique_ptr<DormantBackupRestoreTargetLease>
DormantBackupRestoreTargetLease::TryAcquireForAbsentTarget(
    const base::FilePath& canonical_database_path) {
  if (!canonical_database_path.IsAbsolute() ||
      canonical_database_path.ReferencesParent() ||
      canonical_database_path.empty()) {
    return nullptr;
  }
  const base::FilePath database_directory = canonical_database_path.DirName();
  const base::FilePath profile_path = database_directory.DirName();
  const base::FilePath canonical_parent =
      base::MakeAbsoluteFilePath(profile_path.DirName());
  if (canonical_parent.empty() || profile_path.BaseName().empty() ||
      database_directory.BaseName().empty() ||
      canonical_database_path.BaseName().empty()) {
    return nullptr;
  }
  return Acquire(canonical_parent.Append(profile_path.BaseName())
                     .Append(database_directory.BaseName())
                     .Append(canonical_database_path.BaseName()));
}

// static
std::unique_ptr<DormantBackupRestoreTargetLease>
DormantBackupRestoreTargetLease::Acquire(const base::FilePath& database_path) {
  LeaseRegistry& registry = Registry();
  base::AutoLock lock(registry.lock);
  if (!registry.paths.insert(database_path).second) {
    return nullptr;
  }
  // base::WrapUnique rather than std::make_unique: the constructor is private
  // so that no caller can mint a lease without this registry insertion.
  return base::WrapUnique(new DormantBackupRestoreTargetLease(database_path));
}

DormantBackupRestoreTargetLease::DormantBackupRestoreTargetLease(
    base::FilePath database_path)
    : database_path_(std::move(database_path)) {}

DormantBackupRestoreTargetLease::~DormantBackupRestoreTargetLease() {
  LeaseRegistry& registry = Registry();
  base::AutoLock lock(registry.lock);
  CHECK_EQ(registry.paths.erase(database_path_), 1u);
}

}  // namespace taffy::storage::backup
