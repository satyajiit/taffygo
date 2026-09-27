// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_TASK_BENCHMARK_AUDIT_READER_H_
#define TAFFY_TEST_SUPPORT_TASK_BENCHMARK_AUDIT_READER_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"

namespace taffy {
class CoreServiceManager;
}

namespace taffy::test {

// The only fields the benchmark scorer is permitted to learn from one
// durable audit row. The schema-driven decoder validates and skips every
// other field without projecting it out of the committed transaction batch.
struct TaskBenchmarkAuditRecord {
  uint32_t event_type_wire = 0u;
  std::string event_type;
  std::string task_id;
  uint64_t sequence = 0u;
  bool content_values_retained = true;
};

enum class TaskBenchmarkAuditReadState {
  kRead,
  kStorageUnavailable,
  kTaskAbsent,
  kMalformedBatch,
};

struct TaskBenchmarkAuditReadResult {
  TaskBenchmarkAuditReadState state =
      TaskBenchmarkAuditReadState::kStorageUnavailable;
  std::vector<TaskBenchmarkAuditRecord> records;
  // Diagnostics name framing/schema failures only. They never contain a
  // decoded transaction string or byte value.
  std::string error;
};

enum class TaskBenchmarkActionJournalReadState {
  kStorageUnavailable,
  kMissing,
  kIntentOnly,
  kTerminal,
};

struct TaskBenchmarkActionJournalReadResult {
  TaskBenchmarkActionJournalReadState state =
      TaskBenchmarkActionJournalReadState::kStorageUnavailable;
  // Meaningful only for kTerminal. Kept as the closed wire number so this
  // benchmark seam does not expose storage implementation types.
  uint32_t result_code_wire = 0u;
};

using TaskBenchmarkActionJournalReadCallback =
    base::OnceCallback<void(TaskBenchmarkActionJournalReadResult)>;

using TaskBenchmarkAuditReadCallback =
    base::OnceCallback<void(TaskBenchmarkAuditReadResult)>;

std::string_view TaskBenchmarkAuditReadStateName(
    TaskBenchmarkAuditReadState state);

// Reads the selected task through the profile's sequenced storage broker,
// then projects its audit rows through the committed transaction schema.
// The CoreServiceManager shim returns only this task's opaque batches; no
// account, workspace, page, or model content crosses this test seam.
void ReadTaskBenchmarkAudit(CoreServiceManager* manager,
                            std::string task_id,
                            TaskBenchmarkAuditReadCallback callback);

// Reads one exact browser action claim after the task probe has learned all
// three identifiers from the real pre-dispatch journal record.
void ReadTaskBenchmarkActionJournal(
    CoreServiceManager* manager,
    std::string dispatch_id,
    std::string task_id,
    std::string action_id,
    TaskBenchmarkActionJournalReadCallback callback);

// Public only so the deterministic unit target can prove parity against the
// contract's frozen transaction golden. Production callers use the async
// storage read above.
std::optional<std::vector<TaskBenchmarkAuditRecord>>
DecodeTaskBenchmarkAuditBatchForTesting(const std::vector<uint8_t>& bytes,
                                        std::string* error);

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_TASK_BENCHMARK_AUDIT_READER_H_
