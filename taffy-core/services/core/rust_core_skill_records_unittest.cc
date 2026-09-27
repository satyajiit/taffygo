// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_skill_records.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using core_service_internal::ToBridgeSkillRecords;

mojom::SkillRecordPtr Skill() {
  return mojom::SkillRecord::New(
      "saved-procedure", "https://example.test",
      mojom::SkillProvenance::kRecordedFromTask, mojom::SkillStatus::kDisabled,
      3u, std::vector<uint8_t>{1u, 2u, 3u}, 2u, 17u, 23u);
}

std::vector<mojom::SkillRecordPtr> Records() {
  std::vector<mojom::SkillRecordPtr> records;
  records.push_back(Skill());
  return records;
}

TEST(RustCoreSkillRecordsTest, PreservesEveryFieldWithoutAdmittingDefinition) {
  auto records = Records();
  auto projected = ToBridgeSkillRecords(records);
  ASSERT_TRUE(projected);
  ASSERT_EQ(projected->size(), 1u);
  const auto& record = (*projected)[0];
  EXPECT_EQ(std::string(record.skill_id), records[0]->skill_id);
  EXPECT_EQ(std::string(record.origin), records[0]->origin);
  EXPECT_EQ(record.provenance, static_cast<uint8_t>(records[0]->provenance));
  EXPECT_EQ(record.status, static_cast<uint8_t>(records[0]->status));
  EXPECT_EQ(record.active_version, 3u);
  EXPECT_EQ(
      std::vector<uint8_t>(record.definition.begin(), record.definition.end()),
      records[0]->definition);
  EXPECT_EQ(record.step_count, 2u);
  EXPECT_EQ(record.installed_at_utc_ms, 17u);
  EXPECT_EQ(record.updated_at_utc_ms, 23u);
  // These bounded bytes are deliberately not TFSK. Projection is not portable
  // language admission, and must never be used as permission to publish.
}

TEST(RustCoreSkillRecordsTest, EmptyIsValidAndNullOrOversizedSetsRefuse) {
  ASSERT_TRUE(ToBridgeSkillRecords({}));
  std::vector<mojom::SkillRecordPtr> null_record(1u);
  EXPECT_FALSE(ToBridgeSkillRecords(null_record));
  auto records = Records();
  while (records.size() < mojom::kMaxSkillsPerProfile) {
    records.push_back(Skill());
  }
  EXPECT_TRUE(ToBridgeSkillRecords(records));
  records.push_back(Skill());
  EXPECT_FALSE(ToBridgeSkillRecords(records));
}

TEST(RustCoreSkillRecordsTest, RefusesUnknownClosedValuesBeforeNarrowing) {
  auto records = Records();
  records[0]->provenance = static_cast<mojom::SkillProvenance>(256u);
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->status = static_cast<mojom::SkillStatus>(256u);
  EXPECT_FALSE(ToBridgeSkillRecords(records));
}

TEST(RustCoreSkillRecordsTest, RefusesUnboundedTextAndDefinition) {
  auto records = Records();
  records[0]->skill_id.assign(mojom::kMaxSkillIdBytes + 1u, 'a');
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->origin.assign(mojom::kMaxNormalizedOriginBytes + 1u, 'a');
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->origin = std::string(1u, '\xff');
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->definition.assign(mojom::kMaxSkillDefinitionBytes, 1u);
  EXPECT_TRUE(ToBridgeSkillRecords(records));
  records[0]->definition.push_back(1u);
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0]->definition.clear();
  EXPECT_FALSE(ToBridgeSkillRecords(records));
}

TEST(RustCoreSkillRecordsTest,
     RefusesZeroOrOversizedCountsAndUnsignedTimeOverflow) {
  auto records = Records();
  records[0]->active_version = 0u;
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->active_version = mojom::kMaxSkillVersionsPerSkill + 1u;
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->step_count = 0u;
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->step_count = mojom::kMaxSkillSteps + 1u;
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->installed_at_utc_ms = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(ToBridgeSkillRecords(records));
  records[0] = Skill();
  records[0]->updated_at_utc_ms = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(ToBridgeSkillRecords(records));
}

TEST(RustCoreSkillRecordsTest, MalformedLaterRecordRefusesTheWholeProjection) {
  auto records = Records();
  records.push_back(Skill());
  records.back()->definition.clear();
  EXPECT_FALSE(ToBridgeSkillRecords(records));
}

TEST(RustCoreSkillRecordsTest, StageVerificationProjectsCompleteSelectedRows) {
  auto operation =
      mojom::OperationEnvelope::New("verify", 7u, 0u, 1'000u, "verify-once");
  auto binding = mojom::BackupRestoreBinding::New(
      operation.Clone(), "source-profile",
      mojom::BackupRestoreTarget::New(
          mojom::BackupRestoreTargetKind::kNewRegularProfile, "target-profile"),
      "backup-1", std::vector<uint8_t>(32u, 1u), std::vector<uint8_t>(32u, 2u));
  auto request = mojom::BackupRestoreStageVerificationRequest::New(
      operation.Clone(),
      mojom::BackupRestoreStageAuthorization::New(std::move(binding),
                                                  operation.Clone()),
      std::vector<uint8_t>(32u, 1u), Records());
  auto projected =
      core_service_internal::ToBridgeBackupRestoreStageVerificationRequest(
          *request);
  ASSERT_TRUE(projected);
  ASSERT_EQ(projected->skills.size(), 1u);
  const auto& skill = projected->skills[0];
  EXPECT_EQ(std::string(skill.skill_id), request->skills[0]->skill_id);
  EXPECT_EQ(std::string(skill.origin), request->skills[0]->origin);
  EXPECT_EQ(skill.provenance,
            static_cast<uint8_t>(request->skills[0]->provenance));
  EXPECT_EQ(skill.status, static_cast<uint8_t>(request->skills[0]->status));
  EXPECT_EQ(skill.active_version, request->skills[0]->active_version);
  EXPECT_EQ(
      std::vector<uint8_t>(skill.definition.begin(), skill.definition.end()),
      request->skills[0]->definition);
  EXPECT_EQ(skill.step_count, request->skills[0]->step_count);
  EXPECT_EQ(skill.installed_at_utc_ms, request->skills[0]->installed_at_utc_ms);
  EXPECT_EQ(skill.updated_at_utc_ms, request->skills[0]->updated_at_utc_ms);
  request->skills[0].reset();
  EXPECT_FALSE(
      core_service_internal::ToBridgeBackupRestoreStageVerificationRequest(
          *request));
}

}  // namespace
}  // namespace taffy
