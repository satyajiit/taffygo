// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/task_benchmark_scenario.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "taffy/test/support/task_benchmark_audit_reader.h"
#include "taffy/test/support/task_benchmark_record.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

std::optional<std::vector<uint8_t>> ReadFrozenTransactionHex(
    const base::FilePath& relative_path) {
  base::FilePath root;
  std::string hex;
  if (!base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root) ||
      !base::ReadFileToString(root.Append(relative_path), &hex)) {
    return std::nullopt;
  }
  std::vector<uint8_t> bytes;
  if (!base::HexStringToBytes(base::TrimWhitespaceASCII(hex, base::TRIM_ALL),
                              &bytes)) {
    return std::nullopt;
  }
  return bytes;
}

TEST(TaskBenchmarkScenarioTest, LoadsTheExactJoinedScenarioBytes) {
  std::string error;
  const std::optional<TaskBenchmarkScenario> scenario =
      TaskBenchmarkScenario::Load("TB-101", &error);
  ASSERT_TRUE(scenario) << error;
  EXPECT_EQ("TB-101", scenario->id());
  EXPECT_EQ("49a583d945f929edfa99020055e431ad1dcfd9cb79143ae4f247e9bd3b0c7fcb",
            scenario->digest());
  EXPECT_EQ("comparison-source-a", scenario->starting_fixture());
  EXPECT_EQ("markdown-table", scenario->expected_artifact_kind());
  EXPECT_EQ("not-recorded", scenario->scripted_model_outputs_state());
  ASSERT_EQ(8u, scenario->steps().size());
  EXPECT_EQ(1u, scenario->steps().front().number);
  EXPECT_EQ("step:NAVIGATE", scenario->steps().front().kind);
  EXPECT_EQ(8u, scenario->steps().back().number);
  EXPECT_EQ("step:EXPORT", scenario->steps().back().kind);
}

TEST(TaskBenchmarkScenarioTest, CanonicalDigestPreservesUnicode) {
  std::string error;
  const std::optional<TaskBenchmarkScenario> scenario =
      TaskBenchmarkScenario::Load("TB-104", &error);
  ASSERT_TRUE(scenario) << error;
  EXPECT_EQ("73c7481a85186c712085bc433102be4046de40f61d29e37dcf22895143041e07",
            scenario->digest());
}

TEST(TaskBenchmarkScenarioTest, LoadsInjectedRecoveryEvent) {
  std::string error;
  const std::optional<TaskBenchmarkScenario> scenario =
      TaskBenchmarkScenario::Load("TB-601", &error);
  ASSERT_TRUE(scenario) << error;
  EXPECT_EQ("e2bd50a3ccf117be068c631f64bfd3c1564013067fdc7490f4ff58f3f4f10a12",
            scenario->digest());
  ASSERT_EQ(1u, scenario->injected_events().size());
  EXPECT_EQ(4u, scenario->injected_events().front().before_step);
  EXPECT_EQ("renderer-crash", scenario->injected_events().front().event);
}

TEST(TaskBenchmarkScenarioTest, RejectsUnknownScenario) {
  std::string error;
  EXPECT_FALSE(TaskBenchmarkScenario::Load("TB-999", &error));
  EXPECT_NE(std::string::npos, error.find("unknown Task Benchmark scenario"));
}

TEST(TaskBenchmarkScenarioTest, EncodesOneCanonicalLauncherRecord) {
  std::string error;
  const std::optional<TaskBenchmarkScenario> scenario =
      TaskBenchmarkScenario::Load("TB-101", &error);
  ASSERT_TRUE(scenario) << error;
  const base::DictValue record = NewTaskBenchmarkRecord(*scenario, "task");
  const std::optional<std::string> line = EncodeTaskBenchmarkRecord(record);
  ASSERT_TRUE(line);

  constexpr std::string_view marker = "TAFFY_BENCHMARK_RECORD_V1=";
  ASSERT_TRUE(line->starts_with(marker));
  ASSERT_TRUE(line->ends_with("\n"));
  EXPECT_EQ(1u, std::count(line->begin(), line->end(), '\n'));

  const std::string encoded =
      line->substr(marker.size(), line->size() - marker.size() - 1u);
  std::string json;
  ASSERT_TRUE(base::Base64Decode(encoded, &json));
  const std::optional<base::Value> decoded =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(decoded && decoded->is_dict());
  const base::DictValue& value = decoded->GetDict();
  EXPECT_EQ("taffy-benchmark-adapter-v1", *value.FindString("protocol"));
  EXPECT_EQ("task", *value.FindString("kind"));
  EXPECT_EQ("TB-101", *value.FindString("scenario_id"));
  EXPECT_EQ(scenario->digest(), *value.FindString("scenario_digest"));
  ASSERT_TRUE(value.FindList("facts"));
  ASSERT_TRUE(value.FindList("steps"));
  ASSERT_TRUE(value.FindList("forbidden_fact_checks"));
}

TEST(TaskBenchmarkScenarioTest, DecodesTheFrozenTransactionAuditProjection) {
  const std::optional<std::vector<uint8_t>> bytes =
      ReadFrozenTransactionHex(base::FilePath(FILE_PATH_LITERAL(
          "taffy/contracts/core-service/golden/task-transaction-v14.hex")));
  ASSERT_TRUE(bytes);
  std::string error;
  const std::optional<std::vector<TaskBenchmarkAuditRecord>> records =
      DecodeTaskBenchmarkAuditBatchForTesting(*bytes, &error);
  ASSERT_TRUE(records) << error;
  ASSERT_EQ(1u, records->size());
  EXPECT_EQ(4u, records->front().event_type_wire);
  EXPECT_EQ("TaskQueued", records->front().event_type);
  EXPECT_EQ("task-1", records->front().task_id);
  EXPECT_EQ(2u, records->front().sequence);
  EXPECT_FALSE(records->front().content_values_retained);
}

TEST(TaskBenchmarkScenarioTest, DecodesAValidBatchWithNoAuditRows) {
  const std::optional<std::vector<uint8_t>> bytes = ReadFrozenTransactionHex(
      base::FilePath(FILE_PATH_LITERAL("taffy/contracts/core-service/compat/"
                                       "transaction-current-batch.hex")));
  ASSERT_TRUE(bytes);
  std::string error;
  const std::optional<std::vector<TaskBenchmarkAuditRecord>> records =
      DecodeTaskBenchmarkAuditBatchForTesting(*bytes, &error);
  ASSERT_TRUE(records) << error;
  EXPECT_TRUE(records->empty());
}

TEST(TaskBenchmarkScenarioTest, RejectsCorruptedAndTrailingTransactions) {
  const std::optional<std::vector<uint8_t>> frozen =
      ReadFrozenTransactionHex(base::FilePath(FILE_PATH_LITERAL(
          "taffy/contracts/core-service/golden/task-transaction-v14.hex")));
  ASSERT_TRUE(frozen);
  std::vector<uint8_t> corrupted = *frozen;
  corrupted.front() ^= 0xffu;
  std::string error;
  EXPECT_FALSE(DecodeTaskBenchmarkAuditBatchForTesting(corrupted, &error));
  EXPECT_NE(std::string::npos, error.find("magic"));

  std::vector<uint8_t> trailing = *frozen;
  trailing.push_back(0u);
  error.clear();
  EXPECT_FALSE(DecodeTaskBenchmarkAuditBatchForTesting(trailing, &error));
  EXPECT_NE(std::string::npos, error.find("trailing"));
}

TEST(TaskBenchmarkScenarioTest, RejectsUnknownClosedAuditEvent) {
  const std::optional<std::vector<uint8_t>> frozen =
      ReadFrozenTransactionHex(base::FilePath(FILE_PATH_LITERAL(
          "taffy/contracts/core-service/golden/task-transaction-v14.hex")));
  ASSERT_TRUE(frozen);
  std::vector<uint8_t> unknown = *frozen;
  constexpr std::string_view kEventId = "audit-1";
  const auto id = std::search(unknown.begin(), unknown.end(), kEventId.begin(),
                              kEventId.end());
  ASSERT_NE(unknown.end(), id);
  const auto event_type = id + kEventId.size();
  ASSERT_LE(event_type + sizeof(uint32_t), unknown.end());
  std::fill(event_type, event_type + sizeof(uint32_t), 0xffu);

  std::string error;
  EXPECT_FALSE(DecodeTaskBenchmarkAuditBatchForTesting(unknown, &error));
  EXPECT_NE(std::string::npos, error.find("unknown enumeration value"));
}

}  // namespace
}  // namespace taffy::test
