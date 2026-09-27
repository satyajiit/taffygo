// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_pack.h"

#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/numerics/safe_conversions.h"
#include "taffy/browser/assets/asset_store.h"
#include "third_party/zlib/google/zip_reader.h"

namespace taffy {
namespace {

// Whether a caller may ask for this name.
//
// Relative, no parent segment, no separator run, and non-empty. The archive's
// own normalization would already make an escaping entry unreachable, so this
// is the second of two independent refusals rather than the only one.
bool IsRequestableMember(std::string_view member_path, size_t max_path_bytes) {
  if (member_path.empty() || member_path.size() > max_path_bytes) {
    return false;
  }
  if (member_path.front() == '/' || member_path.back() == '/') {
    return false;
  }
  if (member_path.find("//") != std::string_view::npos) {
    return false;
  }
  if (member_path.find('\\') != std::string_view::npos) {
    return false;
  }
  for (const base::FilePath::StringType& segment :
       base::FilePath::FromASCII(member_path).GetComponents()) {
    if (segment.empty() || segment == FILE_PATH_LITERAL(".") ||
        segment == FILE_PATH_LITERAL("..")) {
      return false;
    }
  }
  return true;
}

}  // namespace

PackMemberResult ReadPackMember(const AssetStore& store,
                                std::string_view asset_id,
                                std::string_view member_path,
                                size_t max_path_bytes,
                                size_t max_bytes) {
  PackMemberResult result;
  const std::string revision = store.InstalledRevision(asset_id);
  if (revision.empty()) {
    return result;
  }
  if (!IsRequestableMember(member_path, max_path_bytes)) {
    result.verdict = PackMemberVerdict::kNotFound;
    return result;
  }

  base::File file = store.OpenInstalled(asset_id, revision);
  if (!file.IsValid()) {
    return result;
  }

  zip::ZipReader reader;
  if (!reader.OpenFromPlatformFile(file.GetPlatformFile())) {
    result.verdict = PackMemberVerdict::kUnreadable;
    return result;
  }

  // A forward walk of the central directory rather than a lookup, because the
  // reader offers no lookup. It is bounded by the archive this product built
  // and stays in memory the open already parsed.
  const base::FilePath wanted = base::FilePath::FromASCII(member_path);
  while (const zip::ZipReader::Entry* entry = reader.Next()) {
    if (entry->is_directory || entry->path != wanted) {
      continue;
    }
    std::string contents;
    if (!reader.ExtractCurrentEntryToString(
            base::checked_cast<uint64_t>(max_bytes), &contents)) {
      // Either the member is past the bound or the extraction failed part way.
      // Both leave `contents` holding something that is not the member, so
      // neither may be returned as one.
      result.verdict = contents.size() >= max_bytes ? PackMemberVerdict::kTooLarge
                                                    : PackMemberVerdict::kUnreadable;
      return result;
    }
    result.verdict = PackMemberVerdict::kOk;
    result.bytes.assign(contents.begin(), contents.end());
    return result;
  }

  // A walk that stopped early is a broken archive; one that ran out is a name
  // the archive does not carry. The difference matters: the first is a defect
  // in what was delivered and the second is an ordinary miss.
  result.verdict =
      reader.ok() ? PackMemberVerdict::kNotFound : PackMemberVerdict::kUnreadable;
  return result;
}

}  // namespace taffy
