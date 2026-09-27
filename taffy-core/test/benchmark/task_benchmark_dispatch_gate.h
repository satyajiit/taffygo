// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_DISPATCH_GATE_H_
#define TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_DISPATCH_GATE_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/common/public/page_intelligence_service.h"

namespace taffy::test {

// Content-free identity copied from the exact browser action whose durable
// dispatch-intent append is held. It is sufficient to prove task/lease/action
// ownership without putting a page address or model text in benchmark output.
struct TaskBenchmarkDispatchIdentity {
  std::string dispatch_id;
  std::string task_id;
  std::string action_id;
  std::string capability_reference;
  std::string actor_lease_id;
  std::string tab_id;
  bool opens_observed_link = false;
};

// A test-only decorator over the profile's real durable journal. It holds one
// observed-link intent at the last physical pre-dispatch boundary, then sends
// that exact row to the real sequenced writer when released. All other rows
// pass through unchanged.
class TaskBenchmarkDispatchGate final : public TaskJournalSink {
 public:
  explicit TaskBenchmarkDispatchGate(TaskJournalSink* downstream);
  TaskBenchmarkDispatchGate(const TaskBenchmarkDispatchGate&) = delete;
  TaskBenchmarkDispatchGate& operator=(const TaskBenchmarkDispatchGate&) =
      delete;
  ~TaskBenchmarkDispatchGate() override;

  void RecordDispatching(
      DispatchIntentRecord record,
      std::unique_ptr<TaskJournalAppendCallback> callback) override;
  void RecordTerminalResult(
      ActionResult result,
      std::unique_ptr<TaskJournalAppendCallback> callback) override;

  bool has_held_intent() const { return held_record_.has_value(); }
  const std::optional<TaskBenchmarkDispatchIdentity>& held_identity() const {
    return held_identity_;
  }
  void ReleaseHeldIntent();

  bool release_completed() const { return release_completed_; }
  bool release_committed() const { return release_committed_; }
  bool matching_terminal_seen() const { return matching_terminal_seen_; }
  bool matching_terminal_commit_completed() const {
    return matching_terminal_commit_completed_;
  }
  bool matching_terminal_committed() const {
    return matching_terminal_committed_;
  }
  bool matching_terminal_dispatched() const {
    return matching_terminal_dispatched_;
  }
  uint32_t matching_terminal_code_wire() const {
    return matching_terminal_code_wire_;
  }

 private:
  void OnIntentCommitted(bool committed);
  void OnTerminalCommitted(bool committed);

  const raw_ptr<TaskJournalSink> downstream_;
  std::optional<DispatchIntentRecord> held_record_;
  std::unique_ptr<TaskJournalAppendCallback> held_callback_;
  std::optional<TaskBenchmarkDispatchIdentity> held_identity_;
  bool release_completed_ = false;
  bool release_committed_ = false;
  bool matching_terminal_seen_ = false;
  bool matching_terminal_commit_completed_ = false;
  bool matching_terminal_committed_ = false;
  bool matching_terminal_dispatched_ = false;
  uint32_t matching_terminal_code_wire_ = 0u;
  base::WeakPtrFactory<TaskBenchmarkDispatchGate> weak_factory_{this};
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_DISPATCH_GATE_H_
