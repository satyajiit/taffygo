// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/task_benchmark_record.h"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "taffy/test/support/task_benchmark_scenario.h"

namespace taffy::test {
namespace {

constexpr char kRecordMarker[] = "TAFFY_BENCHMARK_RECORD_V1=";
constexpr char kProtocol[] = "taffy-benchmark-adapter-v1";

}  // namespace

base::DictValue NewTaskBenchmarkRecord(const TaskBenchmarkScenario& scenario,
                                       std::string_view kind) {
  base::DictValue record;
  record.Set("protocol", kProtocol);
  record.Set("kind", kind);
  record.Set("scenario_id", scenario.id());
  record.Set("scenario_digest", scenario.digest());
  record.Set("facts", base::ListValue());
  record.Set("steps", base::ListValue());
  record.Set("forbidden_fact_checks", base::ListValue());
  return record;
}

std::optional<std::string> EncodeTaskBenchmarkRecord(
    const base::DictValue& record) {
  std::string json;
  if (!base::JSONWriter::Write(record, &json)) {
    return std::nullopt;
  }
  return std::string(kRecordMarker) + base::Base64Encode(json) + "\n";
}

bool EmitTaskBenchmarkRecord(const base::DictValue& record) {
  const std::optional<std::string> line = EncodeTaskBenchmarkRecord(record);
  if (!line) {
    return false;
  }
  std::cout << *line;
  std::cout.flush();
  return std::cout.good();
}

}  // namespace taffy::test
