// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/media/test/synthetic_media.h"

#include <string_view>

#include "base/check_op.h"
#include "base/files/file_util.h"
#include "base/numerics/safe_conversions.h"

namespace taffy::media_tool::test {

namespace {

constexpr uint32_t kSampleRate = 8000u;
constexpr uint16_t kBitsPerSample = 16u;

void AppendTag(std::vector<uint8_t>& out, std::string_view tag) {
  CHECK_EQ(4u, tag.size());
  for (const char letter : tag) {
    out.push_back(static_cast<uint8_t>(letter));
  }
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xffu));
}

void AppendU16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
}

}  // namespace

std::vector<uint8_t> BuildSilentWave(uint32_t duration_ms) {
  return BuildSilentWave(duration_ms, 1u);
}

std::vector<uint8_t> BuildSilentWave(uint32_t duration_ms, uint16_t channels) {
  CHECK(channels == 1u || channels == 2u);
  const uint32_t frames = kSampleRate * duration_ms / 1000u;
  const uint16_t block_align = channels * kBitsPerSample / 8u;
  const uint32_t data_bytes = frames * block_align;

  std::vector<uint8_t> out;
  out.reserve(44u + data_bytes);
  AppendTag(out, "RIFF");
  AppendU32(out, 36u + data_bytes);
  AppendTag(out, "WAVE");
  AppendTag(out, "fmt ");
  AppendU32(out, 16u);  // PCM header length
  AppendU16(out, 1u);   // format: PCM
  AppendU16(out, channels);
  AppendU32(out, kSampleRate);
  AppendU32(out, kSampleRate * block_align);  // byte rate
  AppendU16(out, block_align);
  AppendU16(out, kBitsPerSample);
  AppendTag(out, "data");
  AppendU32(out, data_bytes);
  out.insert(out.end(), data_bytes, 0u);
  return out;
}

base::File WriteReadOnlyFile(const base::FilePath& directory,
                             base::span<const uint8_t> bytes) {
  base::FilePath path;
  if (!base::CreateTemporaryFileInDir(directory, &path)) {
    return base::File();
  }
  if (!base::WriteFile(path, bytes)) {
    return base::File();
  }
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
}

}  // namespace taffy::media_tool::test
