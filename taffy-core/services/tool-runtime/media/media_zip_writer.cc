// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/media/media_zip_writer.h"

#include <limits>
#include <utility>

namespace taffy::media_tool {
namespace {

constexpr uint32_t kLocalFileHeader = 0x04034b50u;
constexpr uint32_t kCentralDirectoryHeader = 0x02014b50u;
constexpr uint32_t kEndOfCentralDirectory = 0x06054b50u;
constexpr uint16_t kZipVersion = 20u;

void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
}

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
  AppendU16(out, static_cast<uint16_t>(value));
  AppendU16(out, static_cast<uint16_t>(value >> 16u));
}

uint32_t Crc32(base::span<const uint8_t> bytes) {
  uint32_t crc = 0xffffffffu;
  for (const uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1u) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

bool IsSafeName(std::string_view name) {
  if (name.empty() || name.size() > 96u || name.front() == '/' ||
      name.find("..") != std::string_view::npos ||
      name.find('\\') != std::string_view::npos) {
    return false;
  }
  for (const char value : name) {
    const unsigned char byte = static_cast<unsigned char>(value);
    if (byte < 0x20u || byte > 0x7eu) {
      return false;
    }
  }
  return true;
}

}  // namespace

MediaZipWriter::MediaZipWriter(size_t max_bytes) : max_bytes_(max_bytes) {}
MediaZipWriter::~MediaZipWriter() = default;

bool MediaZipWriter::CanAppend(size_t count) const {
  return count <= max_bytes_ && bytes_.size() <= max_bytes_ - count &&
         bytes_.size() <= std::numeric_limits<uint32_t>::max() - count;
}

bool MediaZipWriter::Add(std::string name, base::span<const uint8_t> contents) {
  constexpr size_t kHeaderBytes = 30u;
  if (finished_ || !IsSafeName(name) || entries_.size() >= 16u ||
      contents.size() > std::numeric_limits<uint32_t>::max() ||
      !CanAppend(kHeaderBytes + name.size() + contents.size())) {
    return false;
  }
  Entry entry;
  entry.name = std::move(name);
  entry.crc32 = Crc32(contents);
  entry.size = static_cast<uint32_t>(contents.size());
  entry.local_offset = static_cast<uint32_t>(bytes_.size());

  AppendU32(&bytes_, kLocalFileHeader);
  AppendU16(&bytes_, kZipVersion);
  AppendU16(&bytes_, 0u);  // flags
  AppendU16(&bytes_, 0u);  // stored, no compression
  AppendU16(&bytes_, 0u);  // deterministic DOS time
  AppendU16(&bytes_, 0u);  // deterministic DOS date
  AppendU32(&bytes_, entry.crc32);
  AppendU32(&bytes_, entry.size);
  AppendU32(&bytes_, entry.size);
  AppendU16(&bytes_, static_cast<uint16_t>(entry.name.size()));
  AppendU16(&bytes_, 0u);  // extra length
  bytes_.insert(bytes_.end(), entry.name.begin(), entry.name.end());
  bytes_.insert(bytes_.end(), contents.begin(), contents.end());
  entries_.push_back(std::move(entry));
  return true;
}

bool MediaZipWriter::Finish(std::vector<uint8_t>* output) {
  constexpr size_t kCentralHeaderBytes = 46u;
  constexpr size_t kEndBytes = 22u;
  if (finished_ || !output || entries_.empty()) {
    return false;
  }
  size_t directory_bytes = 0u;
  for (const Entry& entry : entries_) {
    directory_bytes += kCentralHeaderBytes + entry.name.size();
  }
  if (entries_.size() > std::numeric_limits<uint16_t>::max() ||
      directory_bytes > std::numeric_limits<uint32_t>::max() ||
      !CanAppend(directory_bytes + kEndBytes)) {
    return false;
  }
  const uint32_t directory_offset = static_cast<uint32_t>(bytes_.size());
  for (const Entry& entry : entries_) {
    AppendU32(&bytes_, kCentralDirectoryHeader);
    AppendU16(&bytes_, kZipVersion);
    AppendU16(&bytes_, kZipVersion);
    AppendU16(&bytes_, 0u);  // flags
    AppendU16(&bytes_, 0u);  // stored
    AppendU16(&bytes_, 0u);  // time
    AppendU16(&bytes_, 0u);  // date
    AppendU32(&bytes_, entry.crc32);
    AppendU32(&bytes_, entry.size);
    AppendU32(&bytes_, entry.size);
    AppendU16(&bytes_, static_cast<uint16_t>(entry.name.size()));
    AppendU16(&bytes_, 0u);  // extra
    AppendU16(&bytes_, 0u);  // comment
    AppendU16(&bytes_, 0u);  // disk
    AppendU16(&bytes_, 0u);  // internal attributes
    AppendU32(&bytes_, 0u);  // external attributes
    AppendU32(&bytes_, entry.local_offset);
    bytes_.insert(bytes_.end(), entry.name.begin(), entry.name.end());
  }
  AppendU32(&bytes_, kEndOfCentralDirectory);
  AppendU16(&bytes_, 0u);
  AppendU16(&bytes_, 0u);
  AppendU16(&bytes_, static_cast<uint16_t>(entries_.size()));
  AppendU16(&bytes_, static_cast<uint16_t>(entries_.size()));
  AppendU32(&bytes_, static_cast<uint32_t>(directory_bytes));
  AppendU32(&bytes_, directory_offset);
  AppendU16(&bytes_, 0u);  // comment

  finished_ = true;
  *output = std::move(bytes_);
  return true;
}

}  // namespace taffy::media_tool
