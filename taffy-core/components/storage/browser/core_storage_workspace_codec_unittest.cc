// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_workspace.h"

#include <stdint.h>

#include <string_view>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

constexpr char kWorkspaceId[] = "11111111111111111111111111111111";

void AppendU32(std::vector<uint8_t>* bytes, uint32_t value) {
  for (size_t index = 0u; index < 4u; ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendU64(std::vector<uint8_t>* bytes, uint64_t value) {
  for (size_t index = 0u; index < 8u; ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendString(std::vector<uint8_t>* bytes, std::string_view value) {
  AppendU32(bytes, static_cast<uint32_t>(value.size()));
  bytes->insert(bytes->end(), value.begin(), value.end());
}

std::vector<uint8_t> Snapshot(std::string_view workspace_id,
                              uint64_t revision,
                              std::string_view goal,
                              uint32_t schema_version,
                              uint8_t saved) {
  std::vector<uint8_t> bytes{'T', 'A', 'F', 'F', 'Y', 'W', 'S', '1'};
  AppendU32(&bytes, schema_version);
  AppendString(&bytes, workspace_id);
  AppendU64(&bytes, revision);
  AppendString(&bytes, goal);
  AppendString(&bytes, "Test workspace");
  bytes.push_back(0u);
  bytes.push_back(saved);
  AppendU64(&bytes, 1u);
  bytes.push_back(0u);
  AppendU32(&bytes, 0u);
  AppendU32(&bytes, 0u);
  return bytes;
}

std::vector<uint8_t> CurrentSnapshotWithEvidence(
    std::string_view locator,
    bool with_media = false,
    uint32_t confidence_ppm = 900'000u) {
  constexpr std::string_view kSourceId =
      "22222222222222222222222222222222";
  std::vector<uint8_t> bytes{'T', 'A', 'F', 'F', 'Y', 'W', 'S', '1'};
  AppendU32(&bytes, 5u);
  AppendString(&bytes, kWorkspaceId);
  AppendU64(&bytes, 1u);
  AppendString(&bytes, "build a source table");
  AppendString(&bytes, "Build a source table");
  bytes.push_back(0u);  // running
  bytes.push_back(0u);  // temporary
  AppendU64(&bytes, 1u);
  bytes.push_back(2u);  // build-source-table
  AppendU32(&bytes, 1u);
  AppendString(&bytes, kSourceId);
  AppendString(&bytes, "source.test");
  AppendString(&bytes, "source.test");
  bytes.push_back(1u);
  AppendString(&bytes, locator);
  AppendU64(&bytes, 1u);
  bytes.push_back(0u);
  AppendU32(&bytes, 1u);
  AppendString(&bytes, "33333333333333333333333333333333");
  AppendString(&bytes, "page");
  AppendString(&bytes, "Example title");
  bytes.push_back(0u);  // from-page
  AppendU32(&bytes, 1u);
  AppendString(&bytes, kSourceId);
  bytes.push_back(0u);  // no correction
  bytes.push_back(0u);  // no conflict
  bytes.push_back(with_media ? 1u : 0u);
  if (with_media) {
    bytes.push_back(0u);  // image
    bytes.push_back(5u);  // metadata
    bytes.push_back(0u);  // DOM evidence
    AppendString(&bytes, "image:dom/1");
    AppendU32(&bytes, 0u);
    AppendU32(&bytes, 10u);
    AppendU32(&bytes, 0u);
    AppendU64(&bytes, 0u);
    AppendU64(&bytes, 0u);
    AppendU32(&bytes, 0u);
    AppendU32(&bytes, confidence_ppm);
    bytes.push_back(0u);
  }
  return bytes;
}

TEST(CoreStorageWorkspaceCodecTest,
     SavedStateSurvivesEverySchemaThatCarriesIt) {
  WorkspaceSnapshotShape shape;
  for (uint32_t schema_version : {3u, 4u, 5u}) {
    EXPECT_TRUE(DecodeWorkspaceSnapshotShape(
        Snapshot(kWorkspaceId, 1u, "saved", schema_version, 1u), kWorkspaceId,
        1u, &shape));
    EXPECT_TRUE(DecodeWorkspaceSnapshotShape(
        Snapshot(kWorkspaceId, 1u, "temporary", schema_version, 0u),
        kWorkspaceId, 1u, &shape));
    EXPECT_FALSE(DecodeWorkspaceSnapshotShape(
        Snapshot(kWorkspaceId, 1u, "invalid", schema_version, 2u),
        kWorkspaceId, 1u, &shape));
  }
}

TEST(CoreStorageWorkspaceCodecTest,
     CurrentSnapshotAcceptsLocatorAndMediaProvenanceExactly) {
  WorkspaceSnapshotShape shape;
  EXPECT_TRUE(DecodeWorkspaceSnapshotShape(
      CurrentSnapshotWithEvidence("https://source.test/title1.html"),
      kWorkspaceId, 1u, &shape));
  EXPECT_TRUE(DecodeWorkspaceSnapshotShape(
      CurrentSnapshotWithEvidence("https://source.test/title1.html", true),
      kWorkspaceId, 1u, &shape));
  EXPECT_FALSE(DecodeWorkspaceSnapshotShape(
      CurrentSnapshotWithEvidence("https://source.test/title1.html?secret"),
      kWorkspaceId, 1u, &shape));
  EXPECT_FALSE(DecodeWorkspaceSnapshotShape(
      CurrentSnapshotWithEvidence("https://source.test/title1.html", true,
                                  1'000'001u),
      kWorkspaceId, 1u, &shape));
}

}  // namespace
}  // namespace taffy
