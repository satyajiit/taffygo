// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_preference_readback.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

namespace taffy {
namespace {

bool MatchesWitness(const base::DictValue& root,
                    const BackupRestorePreferenceWriteWitness& witness) {
  const std::string_view path = witness.dotted_path;
  if (path.empty() || path.front() == '.' || path.back() == '.' ||
      path.find("..") != std::string_view::npos ||
      (witness.must_be_absent && !witness.expected.is_none())) {
    return false;
  }
  const base::DictValue* parent = &root;
  std::string_view remaining = path;
  for (;;) {
    const auto separator = remaining.find('.');
    const base::Value* stored = parent->Find(remaining.substr(0u, separator));
    if (!stored) {
      return witness.must_be_absent;
    }
    if (separator == std::string_view::npos) {
      return !witness.must_be_absent && *stored == witness.expected;
    }
    if (!stored->is_dict()) {
      return false;
    }
    parent = &stored->GetDict();
    remaining.remove_prefix(separator + 1u);
  }
}

bool MatchesWitnesses(
    const std::string& serialized,
    const std::vector<BackupRestorePreferenceWriteWitness>& witnesses) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(serialized, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const base::DictValue& root = parsed->GetDict();
  for (const BackupRestorePreferenceWriteWitness& witness : witnesses) {
    if (!MatchesWitness(root, witness)) {
      return false;
    }
  }
  return true;
}

#if BUILDFLAG(IS_POSIX)
// The same rule as the profile-directory walk in
// //taffy/components/storage/browser, and a deliberate second copy of it: this
// target depends on //base alone so that a host test can link it without the
// storage component, and four lines are a smaller price than that edge.
//
// Android reaches every application-private file through /data/user/<n>, a
// symbolic link into /data/data that the platform installs at boot, so a walk
// refusing every symbolic link ancestor refuses every preference file on the
// device. Accept only a link this process could not have created — owned by the
// superuser while this process is not — and refuse every other one.
bool IsRefusableSymbolicLink(const base::FilePath& path) {
  base::stat_wrapper_t info = {};
  return base::File::Lstat(path, &info) == 0 && S_ISLNK(info.st_mode) &&
         (info.st_uid != 0u || geteuid() == 0u);
}

bool IsRegularFilePathWithoutLinks(const base::FilePath& path) {
  if (!path.IsAbsolute() || path.ReferencesParent() || path == path.DirName() ||
      path != path.StripTrailingSeparators() ||
      std::ranges::any_of(path.GetComponents(), [](const auto& component) {
        return component == FILE_PATH_LITERAL(".");
      })) {
    return false;
  }
  for (auto ancestor = path.DirName();; ancestor = ancestor.DirName()) {
    if (IsRefusableSymbolicLink(ancestor)) {
      return false;
    }
    if (ancestor == ancestor.DirName()) {
      break;
    }
  }
  base::stat_wrapper_t info = {};
  // Refuse pipes/devices before opening: even a read can block or have effects.
  return base::File::Lstat(path, &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_nlink == 1;
}

bool SamePathIdentity(const base::File& file,
                      const base::FilePath& path,
                      bool directory) {
  base::stat_wrapper_t held = {};
  base::stat_wrapper_t named = {};
  return file.IsValid() &&
         base::File::Fstat(file.GetPlatformFile(), &held) == 0 &&
         base::File::Lstat(path, &named) == 0 &&
         (directory ? S_ISDIR(held.st_mode) && S_ISDIR(named.st_mode)
                    : S_ISREG(held.st_mode) && S_ISREG(named.st_mode) &&
                          held.st_nlink == 1 && named.st_nlink == 1) &&
         held.st_dev == named.st_dev && held.st_ino == named.st_ino;
}
#endif

}  // namespace

bool BackupRestorePreferenceFileMatches(
    const base::FilePath& preference_file,
    std::vector<BackupRestorePreferenceWriteWitness> witnesses) {
  if (preference_file.empty() || witnesses.empty()) {
    return false;
  }
  std::string serialized;
  return base::ReadFileToStringWithMaxSize(
             preference_file, &serialized,
             kMaximumBackupRestorePreferenceFileBytes) &&
         MatchesWitnesses(serialized, witnesses);
}

bool BackupRestorePreferenceFileMatchesAndSync(
    const base::FilePath& preference_file,
    std::vector<BackupRestorePreferenceWriteWitness> witnesses) {
#if BUILDFLAG(IS_POSIX)
  if (witnesses.empty() || !IsRegularFilePathWithoutLinks(preference_file)) {
    return false;
  }
  base::File file(preference_file, base::File::FLAG_OPEN |
                                       base::File::FLAG_READ |
                                       base::File::FLAG_NO_FOLLOW);
  const auto parent_path = preference_file.DirName();
  base::File parent(parent_path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                     base::File::FLAG_NO_FOLLOW);
  if (!SamePathIdentity(file, preference_file, false) ||
      !SamePathIdentity(parent, parent_path, true)) {
    return false;
  }
  const int64_t length = file.GetLength();
  if (length < 0 || static_cast<uint64_t>(length) >
                        kMaximumBackupRestorePreferenceFileBytes) {
    return false;
  }
  std::string serialized(static_cast<size_t>(length), '\0');
  if (!file.ReadAndCheck(0, base::as_writable_byte_span(serialized)) ||
      file.GetLength() != length || !MatchesWitnesses(serialized, witnesses)) {
    return false;
  }
  // Both fsync calls are required: the file's bytes and the name installed by
  // PrefService's atomic rename are different persistence obligations.
  return HANDLE_EINTR(fsync(file.GetPlatformFile())) == 0 &&
         HANDLE_EINTR(fsync(parent.GetPlatformFile())) == 0 &&
         file.GetLength() == length &&
         IsRegularFilePathWithoutLinks(preference_file) &&
         SamePathIdentity(file, preference_file, false) &&
         SamePathIdentity(parent, parent_path, true);
#else
  return false;
#endif
}

}  // namespace taffy
