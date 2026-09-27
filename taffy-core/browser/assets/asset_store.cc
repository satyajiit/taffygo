// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_store.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace taffy {
namespace {

// The two directories the store owns.
constexpr base::FilePath::CharType kStagingDirectory[] =
    FILE_PATH_LITERAL("staging");
constexpr base::FilePath::CharType kInstalledDirectory[] =
    FILE_PATH_LITERAL("installed");

// The staged file's suffix, and the character joining identity to revision.
// `@` cannot appear in either component, so the split is unambiguous.
constexpr char kStagingSuffix[] = ".part";
constexpr char kStagingJoin = '@';

// The installed artifact's name. It is the same for every asset, because the
// name a person sees is not this one: a file here is never shown, never opened
// by another application, and never handed out as a path.
constexpr base::FilePath::CharType kArtifactName[] =
    FILE_PATH_LITERAL("artifact");

// The longest either component may be, matching the delivery crate.
constexpr size_t kMaxComponentBytes = 64u;

bool IsSeparator(char character) {
  return character == '.' || character == '-' || character == '+';
}

// The staged file's name, or nothing when either component is unsafe.
std::optional<std::string> StagingName(std::string_view asset_id,
                                       std::string_view asset_revision) {
  if (!AssetStore::IsSafeComponent(asset_id) ||
      !AssetStore::IsSafeComponent(asset_revision)) {
    return std::nullopt;
  }
  return base::StrCat({asset_id, std::string_view(&kStagingJoin, 1),
                       asset_revision, kStagingSuffix});
}

// The identity and revision a staged file's name stands for.
std::optional<std::pair<std::string, std::string>> ParseStagingName(
    std::string_view name) {
  if (!name.ends_with(kStagingSuffix)) {
    return std::nullopt;
  }
  name.remove_suffix(std::string_view(kStagingSuffix).size());
  const size_t split = name.find(kStagingJoin);
  if (split == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view identity = name.substr(0, split);
  const std::string_view revision = name.substr(split + 1);
  if (!AssetStore::IsSafeComponent(identity) ||
      !AssetStore::IsSafeComponent(revision)) {
    return std::nullopt;
  }
  return std::make_pair(std::string(identity), std::string(revision));
}

uint64_t FileSizeOrZero(const base::FilePath& path) {
  const std::optional<int64_t> size = base::GetFileSize(path);
  if (!size.has_value() || *size < 0) {
    return 0u;
  }
  return static_cast<uint64_t>(*size);
}

}  // namespace

AssetStore::AssetStore(base::FilePath root) : root_(std::move(root)) {}

AssetStore::~AssetStore() = default;

bool AssetStore::IsSafeComponent(std::string_view value) {
  if (value.empty() || value.size() > kMaxComponentBytes) {
    return false;
  }
  if (IsSeparator(value.front()) || IsSeparator(value.back())) {
    return false;
  }
  bool previous_was_separator = false;
  for (const char character : value) {
    const bool separator = IsSeparator(character);
    if (separator && previous_was_separator) {
      return false;
    }
    if (!separator && !base::IsAsciiDigit(character) &&
        !base::IsAsciiLower(character)) {
      return false;
    }
    previous_was_separator = separator;
  }
  return true;
}

std::vector<AssetStore::Found> AssetStore::Scan() const {
  std::vector<Found> found;
  if (root_.empty()) {
    return found;
  }
  base::FileEnumerator staged(root_.Append(kStagingDirectory),
                              /*recursive=*/false,
                              base::FileEnumerator::FILES);
  for (base::FilePath path = staged.Next(); !path.empty();
       path = staged.Next()) {
    const auto parsed = ParseStagingName(path.BaseName().MaybeAsASCII());
    if (!parsed.has_value()) {
      continue;
    }
    Found row;
    row.asset_id = parsed->first;
    row.asset_revision = parsed->second;
    row.staged_bytes = FileSizeOrZero(path);
    found.push_back(std::move(row));
  }

  base::FileEnumerator identities(root_.Append(kInstalledDirectory),
                                  /*recursive=*/false,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath identity_path = identities.Next();
       !identity_path.empty(); identity_path = identities.Next()) {
    const std::string identity = identity_path.BaseName().MaybeAsASCII();
    if (!IsSafeComponent(identity)) {
      continue;
    }
    base::FileEnumerator revisions(identity_path, /*recursive=*/false,
                                   base::FileEnumerator::DIRECTORIES);
    for (base::FilePath revision_path = revisions.Next();
         !revision_path.empty(); revision_path = revisions.Next()) {
      const std::string revision = revision_path.BaseName().MaybeAsASCII();
      if (!IsSafeComponent(revision)) {
        continue;
      }
      const base::FilePath artifact = revision_path.Append(kArtifactName);
      if (!base::PathExists(artifact)) {
        continue;
      }
      auto existing = std::ranges::find_if(found, [&](const Found& row) {
        return row.asset_id == identity && row.asset_revision == revision;
      });
      if (existing == found.end()) {
        Found row;
        row.asset_id = identity;
        row.asset_revision = revision;
        found.push_back(std::move(row));
        existing = std::prev(found.end());
      }
      existing->installed = true;
      existing->installed_bytes = FileSizeOrZero(artifact);
    }
  }
  return found;
}

base::FilePath AssetStore::StagingPath(std::string_view asset_id,
                                       std::string_view asset_revision) const {
  const auto name = StagingName(asset_id, asset_revision);
  if (root_.empty() || !name.has_value()) {
    return base::FilePath();
  }
  return root_.Append(kStagingDirectory).AppendASCII(*name);
}

base::FilePath AssetStore::InstalledDirectory(
    std::string_view asset_id,
    std::string_view asset_revision) const {
  if (root_.empty() || !IsSafeComponent(asset_id) ||
      !IsSafeComponent(asset_revision)) {
    return base::FilePath();
  }
  return root_.Append(kInstalledDirectory)
      .AppendASCII(asset_id)
      .AppendASCII(asset_revision);
}

base::FilePath AssetStore::InstalledPath(
    std::string_view asset_id,
    std::string_view asset_revision) const {
  const base::FilePath directory = InstalledDirectory(asset_id, asset_revision);
  if (directory.empty()) {
    return base::FilePath();
  }
  return directory.Append(kArtifactName);
}

base::File AssetStore::OpenStaging(std::string_view asset_id,
                                   std::string_view asset_revision,
                                   uint64_t resume_from) const {
  const base::FilePath path = StagingPath(asset_id, asset_revision);
  if (path.empty()) {
    return base::File();
  }
  if (!base::CreateDirectory(path.DirName())) {
    return base::File();
  }
  base::File file(path, base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_READ |
                            base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    return base::File();
  }
  // Truncating to the offset the caller named is what stops a resumed
  // transfer writing past a gap. A caller that asks for less than the file
  // holds is asking to discard the rest, and gets exactly that.
  if (!file.SetLength(static_cast<int64_t>(resume_from))) {
    return base::File();
  }
  return file;
}

uint64_t AssetStore::StagedBytes(std::string_view asset_id,
                                 std::string_view asset_revision) const {
  const base::FilePath path = StagingPath(asset_id, asset_revision);
  if (path.empty()) {
    return 0u;
  }
  return FileSizeOrZero(path);
}

bool AssetStore::Commit(std::string_view asset_id,
                        std::string_view asset_revision) {
  const base::FilePath staged = StagingPath(asset_id, asset_revision);
  const base::FilePath installed = InstalledPath(asset_id, asset_revision);
  if (staged.empty() || installed.empty() || !base::PathExists(staged)) {
    return false;
  }
  if (!base::CreateDirectory(installed.DirName())) {
    return false;
  }
  return base::ReplaceFile(staged, installed, /*error=*/nullptr);
}

uint64_t AssetStore::Remove(std::string_view asset_id,
                            std::string_view asset_revision) {
  uint64_t freed = 0;
  const base::FilePath staged = StagingPath(asset_id, asset_revision);
  if (!staged.empty()) {
    freed += FileSizeOrZero(staged);
    base::DeleteFile(staged);
  }
  const base::FilePath directory = InstalledDirectory(asset_id, asset_revision);
  if (!directory.empty()) {
    freed += FileSizeOrZero(directory.Append(kArtifactName));
    base::DeletePathRecursively(directory);
  }
  return freed;
}

std::string AssetStore::InstalledRevision(std::string_view asset_id) const {
  if (root_.empty() || !IsSafeComponent(asset_id)) {
    return std::string();
  }
  const base::FilePath identity =
      root_.Append(kInstalledDirectory).AppendASCII(asset_id);
  std::string newest;
  base::FileEnumerator revisions(identity, /*recursive=*/false,
                                 base::FileEnumerator::DIRECTORIES);
  for (base::FilePath revision_path = revisions.Next(); !revision_path.empty();
       revision_path = revisions.Next()) {
    const std::string revision = revision_path.BaseName().MaybeAsASCII();
    // The same two refusals `Scan` makes, for the same reason: a directory the
    // store did not write is not the store's to describe or to open.
    if (!IsSafeComponent(revision) ||
        !base::PathExists(revision_path.Append(kArtifactName))) {
      continue;
    }
    if (revision > newest) {
      newest = revision;
    }
  }
  return newest;
}

base::File AssetStore::OpenInstalled(std::string_view asset_id,
                                     std::string_view asset_revision) const {
  const base::FilePath path = InstalledPath(asset_id, asset_revision);
  if (path.empty()) {
    return base::File();
  }
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
}

}  // namespace taffy
