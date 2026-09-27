// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"

#if BUILDFLAG(IS_POSIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

namespace taffy::storage::backup::restore_resolution_internal {

bool IsCanonicalUuidV4(std::string_view value) {
  const base::Uuid parsed = base::Uuid::ParseLowercase(value);
  return parsed.is_valid() && parsed.AsLowercaseString() == value &&
         value.size() == 36u && value[14] == '4' &&
         (value[19] == '8' || value[19] == '9' || value[19] == 'a' ||
          value[19] == 'b');
}

bool IsRefusableSymbolicLink(const base::FilePath& path) {
#if BUILDFLAG(IS_POSIX)
  base::stat_wrapper_t info = {};
  // An unstattable path is not a link, which is what base::IsLink answers too;
  // the directory and opened-identity checks around this one settle the rest.
  return base::File::Lstat(path, &info) == 0 && S_ISLNK(info.st_mode) &&
         (info.st_uid != 0u || geteuid() == 0u);
#else
  return base::IsLink(path);
#endif
}

bool IsRegularDirectoryWithoutLinks(const base::FilePath& path) {
  if (!path.IsAbsolute() || path.ReferencesParent() ||
      path.BaseName().empty() || path == path.DirName() ||
      path != path.StripTrailingSeparators() ||
      std::ranges::any_of(path.GetComponents(),
                          [](const auto& component) {
                            return component == FILE_PATH_LITERAL(".");
                          }) ||
      !base::DirectoryExists(path)) {
    return false;
  }
  for (auto ancestor = path;; ancestor = ancestor.DirName()) {
    if (IsRefusableSymbolicLink(ancestor)) {
      return false;
    }
    if (ancestor == ancestor.DirName()) {
      return true;
    }
  }
}

bool HasNoSqliteSidecars(const base::FilePath& database_path) {
  // A write-ahead log or its shared-memory index must not exist at all: this
  // owner opens with WAL disabled, so either file means something else opened
  // this database.
  for (const auto* suffix :
       {FILE_PATH_LITERAL("-wal"), FILE_PATH_LITERAL("-shm")}) {
    const base::FilePath sidecar(database_path.value() + suffix);
    if (base::PathExists(sidecar) || base::IsLink(sidecar)) {
      return false;
    }
  }
  // The rollback journal is different, and reading it as "must not exist" is
  // what made every prepared, committed and reopened target refuse itself.
  // Chromium's sql::Database sets `journal_mode=TRUNCATE` for every non-WAL
  // database (sql/database.cc), which commits by truncating the journal to
  // zero bytes instead of unlinking it: a zero-length `-journal` beside a
  // cleanly committed database is the steady state, not a symptom. The first
  // execution of these suites failed 32 tests in this one guard, identically
  // on a host and on a phone.
  //
  // A journal carrying bytes still fails, because that is the interrupted
  // transaction or the second writer this check exists to catch, and a
  // symbolic link in place of any of the three still fails outright.
  const base::FilePath journal(database_path.value() +
                               FILE_PATH_LITERAL("-journal"));
  if (base::IsLink(journal)) {
    return false;
  }
  if (!base::PathExists(journal)) {
    return true;
  }
  const std::optional<int64_t> journal_size = base::GetFileSize(journal);
  return journal_size.has_value() && *journal_size == 0;
}

bool PathIsAbsent(const base::FilePath& path) {
  return !path.empty() && !base::PathExists(path) && !base::IsLink(path);
}

bool IsCanonicalStageChild(const base::FilePath& child) {
  const base::FilePath::StringType prefix(
      base::FilePath::FromASCII(kBackupRestoreStageDirectoryPrefix).value());
  const base::FilePath::StringType name = child.BaseName().value();
  return name.size() == prefix.size() + 6u && base::StartsWith(name, prefix) &&
         std::ranges::all_of(name.substr(prefix.size()), [](auto byte) {
           return (byte >= '0' && byte <= '9') ||
                  (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
         });
}

base::expected<std::vector<base::FilePath>, base::File::Error> DirectChildren(
    const base::FilePath& directory) {
  std::vector<base::FilePath> children;
  base::FileEnumerator entries(
      directory, false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS,
      base::FilePath::StringType(),
      base::FileEnumerator::FolderSearchPolicy::MATCH_ONLY,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (base::FilePath child = entries.Next(); !child.empty();
       child = entries.Next()) {
    children.push_back(std::move(child));
  }
  if (entries.GetError() != base::File::FILE_OK) {
    return base::unexpected(entries.GetError());
  }
  return children;
}

#if BUILDFLAG(IS_POSIX)
bool SameOpenedPath(const base::File& held,
                    const base::FilePath& path,
                    bool directory) {
  base::stat_wrapper_t owned = {};
  base::stat_wrapper_t current = {};
  if (!held.IsValid() ||
      base::File::Fstat(held.GetPlatformFile(), &owned) != 0 ||
      base::File::Lstat(path, &current) != 0 ||
      owned.st_dev != current.st_dev || owned.st_ino != current.st_ino) {
    return false;
  }
  if (directory) {
    // A directory's link count includes `.` and each child's `..`; requiring
    // one would reject every ordinary directory on POSIX. Identity and a
    // positive link count prove the held directory is still named here.
    return owned.st_nlink > 0 && current.st_nlink > 0 &&
           S_ISDIR(owned.st_mode) && S_ISDIR(current.st_mode);
  }
  return owned.st_nlink == 1 && current.st_nlink == 1 &&
         S_ISREG(owned.st_mode) && S_ISREG(current.st_mode);
}

bool OpenedPathWasUnlinked(const base::File& held,
                           const base::FilePath& path,
                           bool directory) {
  base::stat_wrapper_t owned = {};
  if (!held.IsValid() || !PathIsAbsent(path) ||
      base::File::Fstat(held.GetPlatformFile(), &owned) != 0 ||
      owned.st_nlink != 0) {
    return false;
  }
  return directory ? S_ISDIR(owned.st_mode) : S_ISREG(owned.st_mode);
}

bool RemoveOpenedChildAndSync(const base::File& held_parent,
                              const base::FilePath& parent_path,
                              const base::File& held_child,
                              const base::FilePath& child_path,
                              bool directory) {
  if (child_path.DirName() != parent_path || child_path.BaseName().empty() ||
      !SameOpenedPath(held_parent, parent_path, true)) {
    return false;
  }
  if (OpenedPathWasUnlinked(held_child, child_path, directory)) {
    return SyncOpenedDirectory(held_parent);
  }

  base::stat_wrapper_t owned = {};
  base::stat_wrapper_t current = {};
  const std::string child_name = child_path.BaseName().value();
  if (!held_child.IsValid() ||
      base::File::Fstat(held_child.GetPlatformFile(), &owned) != 0 ||
      HANDLE_EINTR(fstatat(held_parent.GetPlatformFile(), child_name.c_str(),
                           &current, AT_SYMLINK_NOFOLLOW)) != 0 ||
      owned.st_dev != current.st_dev || owned.st_ino != current.st_ino ||
      (directory ? (!S_ISDIR(owned.st_mode) || !S_ISDIR(current.st_mode))
                 : (!S_ISREG(owned.st_mode) || !S_ISREG(current.st_mode))) ||
      HANDLE_EINTR(unlinkat(held_parent.GetPlatformFile(), child_name.c_str(),
                            directory ? AT_REMOVEDIR : 0)) != 0 ||
      !OpenedPathWasUnlinked(held_child, child_path, directory)) {
    return false;
  }
  return SyncOpenedDirectory(held_parent);
}

bool SyncOpenedDirectory(const base::File& directory) {
  base::stat_wrapper_t info = {};
  return directory.IsValid() &&
         base::File::Fstat(directory.GetPlatformFile(), &info) == 0 &&
         S_ISDIR(info.st_mode) &&
         HANDLE_EINTR(fsync(directory.GetPlatformFile())) == 0;
}
#else
bool SameOpenedPath(const base::File&, const base::FilePath&, bool) {
  return false;
}
bool OpenedPathWasUnlinked(const base::File&, const base::FilePath&, bool) {
  return false;
}
bool RemoveOpenedChildAndSync(const base::File&,
                              const base::FilePath&,
                              const base::File&,
                              const base::FilePath&,
                              bool) {
  return false;
}
bool SyncOpenedDirectory(const base::File&) {
  return false;
}
#endif

base::File OpenPath(const base::FilePath& path) {
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                              base::File::FLAG_NO_FOLLOW);
}

}  // namespace taffy::storage::backup::restore_resolution_internal
