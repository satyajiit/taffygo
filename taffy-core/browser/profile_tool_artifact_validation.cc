// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_artifact_validation.h"

#include <stddef.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "crypto/hash.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr size_t kDigestBytes = 32u;
constexpr size_t kZipEndBytes = 22u;

struct StoredZipMember {
  std::string name;
  size_t data_offset = 0u;
  size_t data_size = 0u;
};

bool HasTag(base::span<const uint8_t> bytes,
            size_t offset,
            std::string_view tag) {
  return offset <= bytes.size() && tag.size() <= bytes.size() - offset &&
         std::equal(tag.begin(), tag.end(), bytes.begin() + offset);
}

std::optional<uint16_t> ReadU16(base::span<const uint8_t> bytes,
                                size_t offset) {
  if (offset > bytes.size() || 2u > bytes.size() - offset) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(bytes[offset]) |
         static_cast<uint16_t>(bytes[offset + 1u]) << 8u;
}

std::optional<uint32_t> ReadU32(base::span<const uint8_t> bytes,
                                size_t offset) {
  if (offset > bytes.size() || 4u > bytes.size() - offset) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(bytes[offset]) |
         static_cast<uint32_t>(bytes[offset + 1u]) << 8u |
         static_cast<uint32_t>(bytes[offset + 2u]) << 16u |
         static_cast<uint32_t>(bytes[offset + 3u]) << 24u;
}

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
  out->push_back(static_cast<uint8_t>(value >> 16u));
  out->push_back(static_cast<uint8_t>(value >> 24u));
}

void AppendU64(std::vector<uint8_t>* out, uint64_t value) {
  AppendU32(out, static_cast<uint32_t>(value));
  AppendU32(out, static_cast<uint32_t>(value >> 32u));
}

std::vector<uint8_t> Digest(base::span<const uint8_t> content) {
  const std::array<uint8_t, kDigestBytes> digest =
      crypto::hash::Sha256(content);
  return std::vector<uint8_t>(digest.begin(), digest.end());
}

uint32_t Crc32(base::span<const uint8_t> content) {
  uint32_t crc = 0xffffffffu;
  for (uint8_t byte : content) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1u) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

std::optional<std::vector<StoredZipMember>> ParseStoredZip(
    base::span<const uint8_t> content) {
  if (content.size() < kZipEndBytes + 4u ||
      !HasTag(content, 0u, std::string_view("PK\x03\x04", 4u))) {
    return std::nullopt;
  }
  const size_t end = content.size() - kZipEndBytes;
  const auto disk = ReadU16(content, end + 4u);
  const auto directory_disk = ReadU16(content, end + 6u);
  const std::optional<uint16_t> disk_entries = ReadU16(content, end + 8u);
  const std::optional<uint16_t> total_entries = ReadU16(content, end + 10u);
  const std::optional<uint32_t> directory_bytes = ReadU32(content, end + 12u);
  const std::optional<uint32_t> directory_offset = ReadU32(content, end + 16u);
  const auto comment_bytes = ReadU16(content, end + 20u);
  if (!HasTag(content, end, std::string_view("PK\x05\x06", 4u)) || !disk ||
      !directory_disk || !disk_entries || !total_entries || !directory_bytes ||
      !directory_offset || !comment_bytes || *disk != 0u ||
      *directory_disk != 0u || *disk_entries == 0u || *disk_entries > 20u ||
      *disk_entries != *total_entries || *comment_bytes != 0u) {
    return std::nullopt;
  }
  const uint64_t directory_end = static_cast<uint64_t>(*directory_offset) +
                                 static_cast<uint64_t>(*directory_bytes);
  if (directory_end != end) {
    return std::nullopt;
  }

  size_t cursor = *directory_offset;
  size_t expected_local_offset = 0u;
  std::vector<StoredZipMember> members;
  members.reserve(*total_entries);
  for (uint16_t index = 0u; index < *total_entries; ++index) {
    constexpr size_t kCentralHeaderBytes = 46u;
    if (!HasTag(content, cursor, std::string_view("PK\x01\x02", 4u)) ||
        cursor > end || kCentralHeaderBytes > end - cursor) {
      return std::nullopt;
    }
    const auto flags = ReadU16(content, cursor + 8u);
    const auto compression = ReadU16(content, cursor + 10u);
    const auto crc = ReadU32(content, cursor + 16u);
    const auto compressed = ReadU32(content, cursor + 20u);
    const auto uncompressed = ReadU32(content, cursor + 24u);
    const auto name_bytes = ReadU16(content, cursor + 28u);
    const auto extra_bytes = ReadU16(content, cursor + 30u);
    const auto member_comment = ReadU16(content, cursor + 32u);
    const auto member_disk = ReadU16(content, cursor + 34u);
    const auto local_offset = ReadU32(content, cursor + 42u);
    if (!flags || !compression || !crc || !compressed || !uncompressed ||
        !name_bytes || !extra_bytes || !member_comment || !member_disk ||
        !local_offset || *flags != 0u || *compression != 0u ||
        *compressed != *uncompressed || *name_bytes == 0u ||
        *name_bytes > 96u || *extra_bytes != 0u || *member_comment != 0u ||
        *member_disk != 0u || *local_offset != expected_local_offset) {
      return std::nullopt;
    }
    const size_t central_size = kCentralHeaderBytes + *name_bytes;
    if (central_size > end - cursor) {
      return std::nullopt;
    }
    std::string name;
    name.reserve(*name_bytes);
    for (uint8_t byte :
         content.subspan(cursor + kCentralHeaderBytes, *name_bytes)) {
      if (byte < 0x20u || byte > 0x7eu) {
        return std::nullopt;
      }
      name.push_back(static_cast<char>(byte));
    }
    if (name.front() == '/' || name.find("..") != std::string::npos ||
        name.find('\\') != std::string::npos ||
        std::any_of(members.begin(), members.end(),
                    [&](const auto& member) { return member.name == name; })) {
      return std::nullopt;
    }

    constexpr size_t kLocalHeaderBytes = 30u;
    const size_t local = *local_offset;
    if (!HasTag(content, local, std::string_view("PK\x03\x04", 4u)) ||
        local > *directory_offset ||
        kLocalHeaderBytes > *directory_offset - local ||
        ReadU16(content, local + 6u) != flags ||
        ReadU16(content, local + 8u) != compression ||
        ReadU32(content, local + 14u) != crc ||
        ReadU32(content, local + 18u) != compressed ||
        ReadU32(content, local + 22u) != uncompressed ||
        ReadU16(content, local + 26u) != name_bytes ||
        ReadU16(content, local + 28u) != 0u) {
      return std::nullopt;
    }
    const size_t data_offset = local + kLocalHeaderBytes + *name_bytes;
    if (data_offset > *directory_offset ||
        *compressed > *directory_offset - data_offset ||
        !std::equal(name.begin(), name.end(),
                    content.begin() + local + kLocalHeaderBytes) ||
        Crc32(content.subspan(data_offset, *compressed)) != *crc) {
      return std::nullopt;
    }
    expected_local_offset = data_offset + *compressed;
    members.push_back(
        StoredZipMember{std::move(name), data_offset, *compressed});
    cursor += central_size;
  }
  if (cursor != end || expected_local_offset != *directory_offset) {
    return std::nullopt;
  }
  return members;
}

bool IsWave(base::span<const uint8_t> content) {
  if (content.size() < 44u ||
      content.size() > std::numeric_limits<uint32_t>::max() ||
      !HasTag(content, 0u, "RIFF") || !HasTag(content, 8u, "WAVE") ||
      !HasTag(content, 12u, "fmt ") || !HasTag(content, 36u, "data")) {
    return false;
  }
  const auto riff_bytes = ReadU32(content, 4u);
  const auto fmt_bytes = ReadU32(content, 16u);
  const auto encoding = ReadU16(content, 20u);
  const auto channels = ReadU16(content, 22u);
  const auto sample_rate = ReadU32(content, 24u);
  const auto byte_rate = ReadU32(content, 28u);
  const auto block_align = ReadU16(content, 32u);
  const auto bits = ReadU16(content, 34u);
  const auto data_bytes = ReadU32(content, 40u);
  return riff_bytes && *riff_bytes == content.size() - 8u && fmt_bytes &&
         *fmt_bytes == 16u && encoding && *encoding == 1u && channels &&
         (*channels == 1u || *channels == 2u) && sample_rate &&
         *sample_rate != 0u && *sample_rate <= 48'000u && bits &&
         *bits == 16u && block_align && *block_align == *channels * 2u &&
         byte_rate && *byte_rate == *sample_rate * *block_align && data_bytes &&
         *data_bytes != 0u && *data_bytes == content.size() - 44u;
}

}  // namespace

std::optional<VerifiedToolOutput> ValidateBundledPythonOutput(
    std::string entrypoint,
    std::vector<uint8_t> content) {
  mojom::TaskArtifactKind kind;
  if (entrypoint == "document.build") {
    kind = mojom::TaskArtifactKind::kDocx;
  } else if (entrypoint == "spreadsheet.build") {
    kind = mojom::TaskArtifactKind::kXlsx;
  } else {
    return std::nullopt;
  }
  if (content.empty() || content.size() > 1024u * 1024u) {
    return std::nullopt;
  }
  const auto members = ParseStoredZip(content);
  if (!members) {
    return std::nullopt;
  }
  if (kind == mojom::TaskArtifactKind::kDocx) {
    if (members->size() != 3u ||
        members->at(0u).name != "[Content_Types].xml" ||
        members->at(1u).name != "_rels/.rels" ||
        members->at(2u).name != "word/document.xml") {
      return std::nullopt;
    }
  } else {
    constexpr size_t kWorkbookMemberCount = 4u;
    if (members->size() < kWorkbookMemberCount + 1u ||
        members->size() > kWorkbookMemberCount + 16u ||
        members->at(0u).name != "[Content_Types].xml" ||
        members->at(1u).name != "_rels/.rels" ||
        members->at(2u).name != "xl/workbook.xml" ||
        members->at(3u).name != "xl/_rels/workbook.xml.rels") {
      return std::nullopt;
    }
    for (size_t index = kWorkbookMemberCount; index < members->size();
         ++index) {
      const std::string expected =
          "xl/worksheets/sheet" + std::to_string(index - 3u) + ".xml";
      if (members->at(index).name != expected) {
        return std::nullopt;
      }
    }
  }
  return VerifiedToolOutput{kind, Digest(content), std::move(content)};
}

std::optional<VerifiedToolOutput> ValidateMediaOutput(
    mojom::TaskArtifactKind kind,
    uint32_t frame_count,
    std::vector<uint8_t> expected_digest,
    std::vector<uint8_t> content) {
  if (content.empty() || content.size() > 16u * 1024u * 1024u ||
      expected_digest.size() != kDigestBytes) {
    return std::nullopt;
  }
  const std::vector<uint8_t> digest = Digest(content);
  if (!std::equal(digest.begin(), digest.end(), expected_digest.begin())) {
    return std::nullopt;
  }
  if (kind == mojom::TaskArtifactKind::kWaveAudio) {
    if (frame_count != 0u || !IsWave(content)) {
      return std::nullopt;
    }
  } else if (kind == mojom::TaskArtifactKind::kFrameArchive) {
    const auto members = ParseStoredZip(content);
    if (frame_count == 0u || frame_count > 12u || !members ||
        members->size() != frame_count + 1u ||
        members->back().name != "manifest.json" ||
        members->back().data_size == 0u) {
      return std::nullopt;
    }
    constexpr uint8_t kPngSignature[] = {0x89u, 'P',   'N',   'G',
                                         0x0du, 0x0au, 0x1au, 0x0au};
    for (size_t index = 0u; index < frame_count; ++index) {
      const std::string expected =
          "frame-" +
          (index + 1u < 10u ? std::string("000") : std::string("00")) +
          std::to_string(index + 1u) + ".png";
      const StoredZipMember& member = members->at(index);
      if (member.name != expected ||
          member.data_size < std::size(kPngSignature) ||
          !std::equal(std::begin(kPngSignature), std::end(kPngSignature),
                      content.begin() + member.data_offset)) {
        return std::nullopt;
      }
    }
  } else {
    return std::nullopt;
  }
  return VerifiedToolOutput{kind, digest, std::move(content)};
}

VerifiedToolOutput EncodeMediaProbe(const mojom::MediaProbeResult& result) {
  std::vector<uint8_t> content;
  content.reserve(24u);
  AppendU64(&content, result.duration_ms);
  AppendU32(&content, result.audio_streams);
  AppendU32(&content, result.video_streams);
  AppendU32(&content, result.width);
  AppendU32(&content, result.height);
  return VerifiedToolOutput{mojom::TaskArtifactKind::kWaveAudio,
                            Digest(content), std::move(content)};
}

}  // namespace taffy
