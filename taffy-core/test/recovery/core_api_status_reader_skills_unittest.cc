// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "taffy/contracts/core-api/generated/cpp/core_api_enums.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/core_api_status_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test::internal {
namespace {

// The payload version the committed generated full-status-v1.hex speaks. The
// contract emits that version beside the types so a literal here cannot go
// stale the next time the state payload changes layout — which it did, at 31.
constexpr uint32_t kGoldenSchemaVersion =
    core_api::wire::kStatePayloadSchemaVersion;

std::vector<uint8_t> GeneratedFullStatus() {
  base::FilePath source_root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  std::string hex;
  CHECK(base::ReadFileToString(
      source_root.AppendASCII(
          "taffy/contracts/core-api/golden/full-status-v1.hex"),
      &hex));
  base::RemoveChars(hex, base::kWhitespaceASCII, &hex);
  std::vector<uint8_t> bytes;
  CHECK(base::HexStringToBytes(hex, &bytes));
  return bytes;
}

size_t OffsetOf(const std::vector<uint8_t>& bytes, std::string_view text) {
  const auto position =
      std::search(bytes.begin(), bytes.end(), text.begin(), text.end());
  CHECK(position != bytes.end());
  return static_cast<size_t>(position - bytes.begin());
}

TEST(CoreStatusWireReaderSkillsTest, GeneratedReviewSurvivesPrivateSavedData) {
  const auto bytes = GeneratedFullStatus();
  CoreStatusWireReader reader(bytes);
  const auto payload = reader.Read(kGoldenSchemaVersion);
  ASSERT_TRUE(payload);
  EXPECT_TRUE(payload->site_skills_complete);
  ASSERT_EQ(payload->site_skills.size(), 2u);
  EXPECT_FALSE(payload->site_skills.front().review_complete);
  const auto& skill = payload->site_skills.back();
  EXPECT_EQ(skill.skill_id, "learned-document");
  EXPECT_EQ(skill.origin, "https://example.test");
  EXPECT_EQ(skill.provenance, api::SiteSkillProvenanceView::kRecordedFromTask);
  EXPECT_EQ(skill.status, api::SiteSkillStatusView::kDraft);
  EXPECT_EQ(skill.active_version, 1u);
  EXPECT_EQ(skill.step_count, 3u);
  EXPECT_EQ(skill.installed_at_epoch_ms, 1700000003000u);
  EXPECT_EQ(skill.updated_at_epoch_ms, 1700000003000u);
  EXPECT_EQ(skill.recorded_from_task_id, "task-completed-1");
  EXPECT_TRUE(skill.review_complete);
  EXPECT_EQ(skill.starting_address, "https://example.test/entry");
  ASSERT_EQ(skill.reviewed_steps.size(), 3u);
  const auto& navigate = skill.reviewed_steps[0];
  EXPECT_EQ(navigate.verb, "browser.navigate");
  EXPECT_EQ(navigate.postcondition, 1u);
  EXPECT_FALSE(navigate.has_fill);
  EXPECT_EQ(navigate.fill_purpose, 0u);
  ASSERT_EQ(navigate.arguments.size(), 1u);
  EXPECT_EQ(navigate.arguments[0].kind,
            api::SiteSkillArgumentKind::kPublicAddress);
  EXPECT_EQ(navigate.arguments[0].public_address, skill.starting_address);
  EXPECT_FALSE(navigate.arguments[0].semantic_target);
  const auto& handover = skill.reviewed_steps[1];
  EXPECT_EQ(handover.verb, "user.handover");
  ASSERT_EQ(handover.arguments.size(), 1u);
  EXPECT_EQ(handover.arguments[0].kind, api::SiteSkillArgumentKind::kChoice);
  EXPECT_EQ(handover.arguments[0].parameter, 0u);
  EXPECT_EQ(handover.arguments[0].value, 1u);
  EXPECT_EQ(handover.arguments[0].purpose, 0u);
  EXPECT_FALSE(handover.arguments[0].public_address);
  const auto& download = skill.reviewed_steps[2];
  EXPECT_EQ(download.verb, "browser.download.from_link");
  EXPECT_EQ(download.postcondition, 7u);
  ASSERT_EQ(download.arguments.size(), 1u);
  EXPECT_EQ(download.arguments[0].kind,
            api::SiteSkillArgumentKind::kSemanticTarget);
  ASSERT_TRUE(download.arguments[0].semantic_target);
  EXPECT_EQ(download.arguments[0].semantic_target->role, 9u);
  EXPECT_EQ(download.arguments[0].semantic_target->phrase, 10u);
  EXPECT_FALSE(download.arguments[0].public_address);
}

TEST(CoreStatusWireReaderSkillsTest, ObserverExposesPayloadlessWithdrawal) {
  CoreApiStatusObserver observer;
  api::TaffyProfileCoreApiObserver& endpoint = observer;
  const auto bytes = GeneratedFullStatus();
  CoreStatusWireReader reader(bytes);
  const auto payload = reader.Read(kGoldenSchemaVersion);
  ASSERT_TRUE(payload);
  endpoint.OnSnapshot(payload->availability, payload->generation, 7u,
                      kGoldenSchemaVersion, bytes);
  ASSERT_TRUE(observer.snapshot_seen());
  endpoint.OnSnapshot(api::CoreAvailability::kUnavailable, payload->generation,
                      0u, 0u, std::nullopt);
  EXPECT_EQ(api::CoreAvailability::kUnavailable, observer.availability());
  EXPECT_EQ(7u, observer.state_sequence());
  EXPECT_EQ(payload->generation, observer.service_generation());
  EXPECT_FALSE(observer.malformed_payload_seen());
  endpoint.OnSnapshot(api::CoreAvailability::kReady, payload->generation, 0u,
                      0u, std::nullopt);
  EXPECT_TRUE(observer.malformed_payload_seen());
  EXPECT_EQ(api::CoreAvailability::kUnavailable, observer.availability());
  EXPECT_EQ(7u, observer.state_sequence());
}

TEST(CoreStatusWireReaderSkillsTest, HandoverNeedsNoFieldValueRequest) {
  auto bytes = GeneratedFullStatus();
  CoreStatusWireReader approval_reader(bytes);
  auto approval = approval_reader.Read(kGoldenSchemaVersion);
  ASSERT_TRUE(approval);
  ASSERT_EQ(1u, approval->tasks.size());
  EXPECT_FALSE(approval->tasks.front().waiting_for_handover);
  constexpr std::string_view approval_message = "task.waiting_for_approval";
  constexpr std::string_view handover_message = "task.waiting_for_handover";
  static_assert(approval_message.size() == handover_message.size());
  const auto offset = OffsetOf(bytes, approval_message);
  std::copy(handover_message.begin(), handover_message.end(),
            bytes.begin() + offset);
  CoreStatusWireReader handover_reader(bytes);
  auto handover = handover_reader.Read(kGoldenSchemaVersion);
  ASSERT_TRUE(handover);
  ASSERT_EQ(1u, handover->tasks.size());
  EXPECT_TRUE(handover->tasks.front().waiting_for_handover);
}

TEST(CoreStatusWireReaderSkillsTest, RetainsClosedTaskFailureCode) {
  auto bytes = GeneratedFullStatus();
  constexpr std::string_view message = "task.waiting_for_approval";
  const size_t failure_offset = OffsetOf(bytes, message) + message.size();
  ASSERT_EQ(bytes[failure_offset], 0u);
  const uint32_t code = static_cast<uint32_t>(api::CoreFailureCode::kSourcesUnavailable);
  const std::vector<uint8_t> failure = {
      static_cast<uint8_t>(code), static_cast<uint8_t>(code >> 8u),
      static_cast<uint8_t>(code >> 16u), static_cast<uint8_t>(code >> 24u),
      1u, 0u};  // retryable, absent message key.
  bytes[failure_offset] = 1u;
  bytes.insert(bytes.begin() + failure_offset + 1u,
               failure.begin(), failure.end());
  CoreStatusWireReader reader(bytes);
  auto payload = reader.Read(kGoldenSchemaVersion);
  ASSERT_TRUE(payload);
  ASSERT_EQ(1u, payload->tasks.size());
  EXPECT_EQ(payload->tasks.front().failure_code,
            api::CoreFailureCode::kSourcesUnavailable);
  EXPECT_TRUE(payload->site_skills_complete);
  bytes[failure_offset + 1u] = 0xffu;
  CoreStatusWireReader invalid(bytes);
  EXPECT_FALSE(invalid.Read(kGoldenSchemaVersion));
}

TEST(CoreStatusWireReaderSkillsTest,
     RejectsTruncatedReviewTailAndTrailingBytes) {
  const auto full = GeneratedFullStatus();
  auto cut_review = full;
  cut_review.resize(OffsetOf(full, "browser.download.from_link") + 3u);
  CoreStatusWireReader review_reader(cut_review);
  EXPECT_FALSE(review_reader.Read(kGoldenSchemaVersion));
  auto cut_tail = full;
  cut_tail.pop_back();
  CoreStatusWireReader tail_reader(cut_tail);
  EXPECT_FALSE(tail_reader.Read(kGoldenSchemaVersion));
  auto trailing = full;
  trailing.push_back(0u);
  CoreStatusWireReader trailing_reader(trailing);
  EXPECT_FALSE(trailing_reader.Read(kGoldenSchemaVersion));
}

TEST(CoreStatusWireReaderSkillsTest,
     RejectsInvalidUtf8InSkippedAndRetainedFields) {
  for (std::string_view field :
       {"42 Example Road", "https://example.test/entry"}) {
    auto bytes = GeneratedFullStatus();
    bytes[OffsetOf(bytes, field)] = 0xffu;
    CoreStatusWireReader reader(bytes);
    EXPECT_FALSE(reader.Read(kGoldenSchemaVersion));
  }
}

}  // namespace
}  // namespace taffy::test::internal
