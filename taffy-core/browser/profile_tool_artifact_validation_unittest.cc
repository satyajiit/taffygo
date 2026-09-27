// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_artifact_validation.h"

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "crypto/hash.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

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
  for (uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

struct Part {
  std::string name;
  std::vector<uint8_t> content;
};

std::vector<uint8_t> StoredZip(std::vector<Part> parts) {
  struct DirectoryEntry {
    std::string name;
    uint32_t crc = 0u;
    uint32_t size = 0u;
    uint32_t offset = 0u;
  };
  std::vector<uint8_t> out;
  std::vector<DirectoryEntry> entries;
  for (const Part& part : parts) {
    DirectoryEntry entry{part.name, Crc32(part.content),
                         static_cast<uint32_t>(part.content.size()),
                         static_cast<uint32_t>(out.size())};
    AppendU32(&out, 0x04034b50u);
    AppendU16(&out, 20u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU32(&out, entry.crc);
    AppendU32(&out, entry.size);
    AppendU32(&out, entry.size);
    AppendU16(&out, static_cast<uint16_t>(entry.name.size()));
    AppendU16(&out, 0u);
    out.insert(out.end(), entry.name.begin(), entry.name.end());
    out.insert(out.end(), part.content.begin(), part.content.end());
    entries.push_back(std::move(entry));
  }
  const uint32_t directory_offset = static_cast<uint32_t>(out.size());
  for (const DirectoryEntry& entry : entries) {
    AppendU32(&out, 0x02014b50u);
    AppendU16(&out, 20u);
    AppendU16(&out, 20u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU32(&out, entry.crc);
    AppendU32(&out, entry.size);
    AppendU32(&out, entry.size);
    AppendU16(&out, static_cast<uint16_t>(entry.name.size()));
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU16(&out, 0u);
    AppendU32(&out, 0u);
    AppendU32(&out, entry.offset);
    out.insert(out.end(), entry.name.begin(), entry.name.end());
  }
  const uint32_t directory_size =
      static_cast<uint32_t>(out.size()) - directory_offset;
  AppendU32(&out, 0x06054b50u);
  AppendU16(&out, 0u);
  AppendU16(&out, 0u);
  AppendU16(&out, static_cast<uint16_t>(entries.size()));
  AppendU16(&out, static_cast<uint16_t>(entries.size()));
  AppendU32(&out, directory_size);
  AppendU32(&out, directory_offset);
  AppendU16(&out, 0u);
  return out;
}

std::vector<uint8_t> Document() {
  return StoredZip({{"[Content_Types].xml", {'t'}},
                    {"_rels/.rels", {'r'}},
                    {"word/document.xml", {'d'}}});
}

std::vector<uint8_t> Wave() {
  std::vector<uint8_t> wave;
  const auto tag = [&](std::string_view value) {
    wave.insert(wave.end(), value.begin(), value.end());
  };
  tag("RIFF");
  AppendU32(&wave, 38u);
  tag("WAVE");
  tag("fmt ");
  AppendU32(&wave, 16u);
  AppendU16(&wave, 1u);
  AppendU16(&wave, 1u);
  AppendU32(&wave, 8000u);
  AppendU32(&wave, 16000u);
  AppendU16(&wave, 2u);
  AppendU16(&wave, 16u);
  tag("data");
  AppendU32(&wave, 2u);
  wave.push_back(0u);
  wave.push_back(0u);
  return wave;
}

TEST(ProfileToolArtifactValidationTest, AcceptsOnlyExactDocumentMembers) {
  const std::vector<uint8_t> document = Document();
  const auto accepted =
      ValidateBundledPythonOutput("document.build", document);
  ASSERT_TRUE(accepted);
  EXPECT_EQ(core_service::mojom::TaskArtifactKind::kDocx, accepted->kind);

  EXPECT_FALSE(ValidateBundledPythonOutput(
      "document.build",
      StoredZip({{"[Content_Types].xml", {'t'}},
                 {"_rels/.rels", {'r'}},
                 {"word/document.xml", {'d'}},
                 {"forged.bin", {'x'}}})));
  EXPECT_FALSE(
      ValidateBundledPythonOutput("unknown.build", document));
}

TEST(ProfileToolArtifactValidationTest, EveryTruncatedZipFailsClosed) {
  const std::vector<uint8_t> document = Document();
  for (size_t length = 0u; length < document.size(); ++length) {
    const auto truncated = base::span(document).first(length);
    EXPECT_FALSE(ValidateBundledPythonOutput(
        "document.build",
        std::vector<uint8_t>(truncated.begin(), truncated.end())))
        << length;
  }
}

TEST(ProfileToolArtifactValidationTest, CrcMismatchFailsClosed) {
  std::vector<uint8_t> document = Document();
  document.at(30u + std::string("[Content_Types].xml").size()) ^= 0x01u;
  EXPECT_FALSE(ValidateBundledPythonOutput("document.build", document));
}

TEST(ProfileToolArtifactValidationTest, AcceptsBoundedWorkbookSheetSet) {
  const std::vector<uint8_t> workbook = StoredZip(
      {{"[Content_Types].xml", {'t'}},
       {"_rels/.rels", {'r'}},
       {"xl/workbook.xml", {'w'}},
       {"xl/_rels/workbook.xml.rels", {'l'}},
       {"xl/worksheets/sheet1.xml", {'s'}}});
  const auto accepted =
      ValidateBundledPythonOutput("spreadsheet.build", workbook);
  ASSERT_TRUE(accepted);
  EXPECT_EQ(core_service::mojom::TaskArtifactKind::kXlsx, accepted->kind);
}

TEST(ProfileToolArtifactValidationTest, WaveNeedsExactSizeAndDigest) {
  const std::vector<uint8_t> wave = Wave();
  const auto digest = crypto::hash::Sha256(wave);
  EXPECT_TRUE(ValidateMediaOutput(
      core_service::mojom::TaskArtifactKind::kWaveAudio, 0u,
      std::vector<uint8_t>(digest.begin(), digest.end()), wave));
  std::vector<uint8_t> wrong_digest(digest.begin(), digest.end());
  wrong_digest.front() ^= 1u;
  EXPECT_FALSE(ValidateMediaOutput(
      core_service::mojom::TaskArtifactKind::kWaveAudio, 0u, wrong_digest,
      wave));
}

TEST(ProfileToolArtifactValidationTest, FrameArchiveChecksNamesPngAndCount) {
  const std::vector<uint8_t> png = {0x89u, 'P', 'N', 'G',
                                    0x0du, 0x0au, 0x1au, 0x0au};
  const std::vector<uint8_t> archive =
      StoredZip({{"frame-0001.png", png}, {"manifest.json", {'{', '}'}}});
  const auto digest = crypto::hash::Sha256(archive);
  EXPECT_TRUE(ValidateMediaOutput(
      core_service::mojom::TaskArtifactKind::kFrameArchive, 1u,
      std::vector<uint8_t>(digest.begin(), digest.end()), archive));
  EXPECT_FALSE(ValidateMediaOutput(
      core_service::mojom::TaskArtifactKind::kFrameArchive, 2u,
      std::vector<uint8_t>(digest.begin(), digest.end()), archive));
}

}  // namespace
}  // namespace taffy
