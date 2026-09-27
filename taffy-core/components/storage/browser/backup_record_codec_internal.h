// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECORD_CODEC_INTERNAL_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECORD_CODEC_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "taffy/components/storage/browser/backup_record_codec.h"

namespace taffy::storage::backup::codec_internal {

inline constexpr size_t kMaxRecordPayloadBytes = 64u * 1024u * 1024u;

// These are frozen backup-format tags, independent of Mojo enum ordinals.
enum class RecordTag : uint32_t {
  kAssistantConfiguration = 0,
  kSavedWorkspace = 1,
  kLibrary = 2,
  kMemory = 3,
  kUserAuthoredSkill = 4,
  kLearnedProcedure = 5,
  kBookmark = 6,
  kBrowserPreference = 7,
};

class RecordWriter {
 public:
  explicit RecordWriter(RecordTag tag);
  ~RecordWriter();
  void U32(uint32_t value);
  void U64(uint64_t value);
  void Boolean(bool value);
  void String(std::string_view value);
  void Bytes(base::span<const uint8_t> value);
  void OptionalString(const std::optional<std::string>& value);
  EncodedBackupRecord Finish() &&;

 private:
  void Append(base::span<const uint8_t> bytes);
  std::vector<uint8_t> bytes_;
  bool valid_ = true;
};

class RecordReader {
 public:
  RecordReader(base::span<const uint8_t> bytes, RecordTag tag);
  bool U32(uint32_t* value);
  bool U64(uint64_t* value);
  bool Boolean(bool* value);
  bool String(size_t maximum, std::string* value);
  bool Bytes(size_t maximum, std::vector<uint8_t>* value);
  bool OptionalString(size_t maximum, std::optional<std::string>* value);
  bool Complete() const;

 private:
  std::optional<base::span<const uint8_t>> Take(size_t count);
  base::raw_span<const uint8_t> remaining_;
  bool valid_ = true;
};

}  // namespace taffy::storage::backup::codec_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECORD_CODEC_INTERNAL_H_
