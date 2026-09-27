// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_TASK_BENCHMARK_RECORD_H_
#define TAFFY_TEST_SUPPORT_TASK_BENCHMARK_RECORD_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/values.h"

namespace taffy::test {

class TaskBenchmarkScenario;

// Starts a scorer-compatible record with the immutable scenario identity and
// all three proof lists present and empty. Each adapter fills only evidence it
// actually obtained before passing the record to EncodeTaskBenchmarkRecord().
base::DictValue NewTaskBenchmarkRecord(const TaskBenchmarkScenario& scenario,
                                       std::string_view kind);

// Returns the one canonical stdout line consumed by launcher.py, including
// its trailing newline. The separate encoder makes the wire deterministic and
// unit-testable without redirecting process-global stdout.
std::optional<std::string> EncodeTaskBenchmarkRecord(
    const base::DictValue& record);

bool EmitTaskBenchmarkRecord(const base::DictValue& record);

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_TASK_BENCHMARK_RECORD_H_
