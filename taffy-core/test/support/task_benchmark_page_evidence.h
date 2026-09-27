// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_TASK_BENCHMARK_PAGE_EVIDENCE_H_
#define TAFFY_TEST_SUPPORT_TASK_BENCHMARK_PAGE_EVIDENCE_H_

#include <optional>
#include <string>
#include <string_view>

#include "taffy/test/support/bip_graph_payload_reader.h"
#include "taffy/test/support/task_benchmark_scenario.h"

namespace taffy::test {

// Content-free proof that one corpus-declared page field was present in the
// graph a live PageIntelligence observation returned. The hash is the only
// value written to benchmark evidence; callers never copy page text there.
struct TaskBenchmarkPageFieldEvidence {
  std::string expected_value_sha256;
};

// Returns evidence only when the fact belongs to `observed_fixture` and its
// corpus-declared value is present as one normalized graph segment. The
// corpus's closed ABSENT sentinel additionally requires a complete graph and
// an explicit absence statement naming the field. This deliberately declines
// fuzzy, cross-node, or inferred matches.
std::optional<TaskBenchmarkPageFieldEvidence>
ExtractTaskBenchmarkPageFieldEvidence(
    const TaskBenchmarkScenario::RequiredFact& fact,
    std::string_view observed_fixture,
    bool graph_complete,
    const GraphPayload& graph);

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_TASK_BENCHMARK_PAGE_EVIDENCE_H_
