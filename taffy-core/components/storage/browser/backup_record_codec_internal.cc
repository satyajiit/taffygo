// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_record_codec_internal.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/numerics/byte_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/secure_util.h"

namespace taffy::storage::backup::codec_internal {
namespace {

constexpr std::array<uint8_t, 8> kMagic = {'T', 'A', 'F', 'F',
                                           'Y', 'R', 'E', 'C'};
constexpr uint32_t kPayloadVersion = 1;

}  // namespace

RecordWriter::RecordWriter(RecordTag tag) {
  Append(kMagic);
  U32(kPayloadVersion);
  U32(static_cast<uint32_t>(tag));
}

RecordWriter::~RecordWriter() {
  crypto::SecureZeroBuffer(bytes_);
}

void RecordWriter::Append(base::span<const uint8_t> bytes) {
  if (!valid_ || bytes.size() > kMaxRecordPayloadBytes - bytes_.size()) {
    valid_ = false;
    return;
  }
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

void RecordWriter::U32(uint32_t value) {
  Append(base::U32ToLittleEndian(value));
}

void RecordWriter::U64(uint64_t value) {
  Append(base::U64ToLittleEndian(value));
}

void RecordWriter::Boolean(bool value) {
  const std::array<uint8_t, 1> bytes = {static_cast<uint8_t>(value)};
  Append(bytes);
}

void RecordWriter::String(std::string_view value) {
  if (value.size() > kMaxRecordPayloadBytes || !base::IsStringUTF8(value)) {
    valid_ = false;
    return;
  }
  U32(static_cast<uint32_t>(value.size()));
  Append(base::as_byte_span(value));
}

void RecordWriter::Bytes(base::span<const uint8_t> value) {
  if (value.size() > kMaxRecordPayloadBytes) {
    valid_ = false;
    return;
  }
  U32(static_cast<uint32_t>(value.size()));
  Append(value);
}

void RecordWriter::OptionalString(const std::optional<std::string>& value) {
  Boolean(value.has_value());
  if (value) {
    String(*value);
  }
}

EncodedBackupRecord RecordWriter::Finish() && {
  if (!valid_) {
    return base::unexpected(BackupRecordCodecError::kPayloadTooLarge);
  }
  return std::move(bytes_);
}

RecordReader::RecordReader(base::span<const uint8_t> bytes, RecordTag tag)
    : remaining_(bytes), valid_(bytes.size() <= kMaxRecordPayloadBytes) {
  const auto magic = Take(kMagic.size());
  uint32_t version = 0;
  uint32_t kind = 0;
  if (!magic || !std::ranges::equal(*magic, kMagic) || !U32(&version) ||
      version != kPayloadVersion || !U32(&kind) ||
      kind != static_cast<uint32_t>(tag)) {
    valid_ = false;
  }
}

std::optional<base::span<const uint8_t>> RecordReader::Take(size_t count) {
  if (!valid_ || count > remaining_.size()) {
    valid_ = false;
    return std::nullopt;
  }
  const auto bytes = remaining_.first(count);
  remaining_ = remaining_.subspan(count);
  return bytes;
}

bool RecordReader::U32(uint32_t* value) {
  const auto bytes = Take(sizeof(uint32_t));
  if (!bytes) {
    return false;
  }
  *value = base::U32FromLittleEndian(bytes->first<sizeof(uint32_t)>());
  return true;
}

bool RecordReader::U64(uint64_t* value) {
  const auto bytes = Take(sizeof(uint64_t));
  if (!bytes) {
    return false;
  }
  *value = base::U64FromLittleEndian(bytes->first<sizeof(uint64_t)>());
  return true;
}

bool RecordReader::Boolean(bool* value) {
  const auto bytes = Take(1);
  if (!bytes || bytes->front() > 1) {
    valid_ = false;
    return false;
  }
  *value = bytes->front() == 1;
  return true;
}

bool RecordReader::String(size_t maximum, std::string* value) {
  uint32_t length = 0;
  if (!U32(&length) || length > maximum) {
    valid_ = false;
    return false;
  }
  const auto bytes = Take(length);
  if (!bytes || !base::IsStringUTF8(base::as_string_view(*bytes))) {
    valid_ = false;
    return false;
  }
  *value = base::as_string_view(*bytes);
  return true;
}

bool RecordReader::Bytes(size_t maximum, std::vector<uint8_t>* value) {
  uint32_t length = 0;
  if (!U32(&length) || length > maximum) {
    valid_ = false;
    return false;
  }
  const auto bytes = Take(length);
  if (!bytes) {
    return false;
  }
  value->assign(bytes->begin(), bytes->end());
  return true;
}

bool RecordReader::OptionalString(size_t maximum,
                                  std::optional<std::string>* value) {
  bool present = false;
  if (!Boolean(&present)) {
    return false;
  }
  value->reset();
  if (!present) {
    return true;
  }
  std::string text;
  if (!String(maximum, &text)) {
    return false;
  }
  *value = std::move(text);
  return true;
}

bool RecordReader::Complete() const {
  return valid_ && remaining_.empty();
}

}  // namespace taffy::storage::backup::codec_internal
