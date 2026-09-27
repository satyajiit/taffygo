// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/benchmark/task_benchmark_dispatch_gate.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"

namespace taffy::test {
namespace {

class OnceAppendCallback final : public TaskJournalAppendCallback {
 public:
  explicit OnceAppendCallback(base::OnceCallback<void(bool)> callback)
      : callback_(std::move(callback)) {}

  void Run(bool committed) override {
    CHECK(callback_);
    std::move(callback_).Run(committed);
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

TaskBenchmarkDispatchIdentity Identity(const DispatchIntentRecord& record) {
  return TaskBenchmarkDispatchIdentity{
      .dispatch_id = record.dispatch_id.value,
      .task_id = record.task_id.value,
      .action_id = record.action_id.value,
      .capability_reference = record.capability_reference.value,
      .actor_lease_id = record.actor_lease_id.value,
      .tab_id = record.tab_id.value,
      .opens_observed_link =
          record.command_type == BrowserCommandType::kOpenObservedLink,
  };
}

}  // namespace

TaskBenchmarkDispatchGate::TaskBenchmarkDispatchGate(
    TaskJournalSink* downstream)
    : downstream_(downstream) {
  CHECK(downstream_);
}

TaskBenchmarkDispatchGate::~TaskBenchmarkDispatchGate() {
  // An assertion failure must not leave the production core waiting on a
  // callback the fixture is about to destroy. This cleanup refuses the held
  // append; successful probes always release it through the durable writer.
  if (held_callback_) {
    held_record_.reset();
    std::move(held_callback_)->Run(false);
  }
}

void TaskBenchmarkDispatchGate::RecordDispatching(
    DispatchIntentRecord record,
    std::unique_ptr<TaskJournalAppendCallback> callback) {
  CHECK(callback);
  if (!held_record_ && !held_callback_ && !held_identity_ &&
      record.command_type == BrowserCommandType::kOpenObservedLink) {
    held_identity_ = Identity(record);
    held_record_ = std::move(record);
    held_callback_ = std::move(callback);
    return;
  }
  downstream_->RecordDispatching(std::move(record), std::move(callback));
}

void TaskBenchmarkDispatchGate::RecordTerminalResult(
    ActionResult result,
    std::unique_ptr<TaskJournalAppendCallback> callback) {
  CHECK(callback);
  if (held_identity_ &&
      result.dispatch_id.value == held_identity_->dispatch_id &&
      result.action_id.value == held_identity_->action_id) {
    matching_terminal_seen_ = true;
    matching_terminal_dispatched_ = result.dispatched;
    matching_terminal_code_wire_ = static_cast<uint32_t>(result.result_code);
    downstream_->RecordTerminalResult(
        std::move(result),
        std::make_unique<OnceAppendCallback>(base::BindOnce(
            [](base::WeakPtr<TaskBenchmarkDispatchGate> gate,
               std::unique_ptr<TaskJournalAppendCallback> original,
               bool committed) {
              if (gate) {
                gate->OnTerminalCommitted(committed);
              }
              original->Run(committed);
            },
            weak_factory_.GetWeakPtr(), std::move(callback))));
    return;
  }
  downstream_->RecordTerminalResult(std::move(result), std::move(callback));
}

void TaskBenchmarkDispatchGate::ReleaseHeldIntent() {
  CHECK(held_record_);
  CHECK(held_callback_);
  DispatchIntentRecord record = std::move(*held_record_);
  held_record_.reset();
  std::unique_ptr<TaskJournalAppendCallback> callback =
      std::move(held_callback_);
  downstream_->RecordDispatching(
      std::move(record),
      std::make_unique<OnceAppendCallback>(base::BindOnce(
          [](base::WeakPtr<TaskBenchmarkDispatchGate> gate,
             std::unique_ptr<TaskJournalAppendCallback> original,
             bool committed) {
            if (gate) {
              gate->OnIntentCommitted(committed);
            }
            original->Run(committed);
          },
          weak_factory_.GetWeakPtr(), std::move(callback))));
}

void TaskBenchmarkDispatchGate::OnIntentCommitted(bool committed) {
  release_completed_ = true;
  release_committed_ = committed;
}

void TaskBenchmarkDispatchGate::OnTerminalCommitted(bool committed) {
  matching_terminal_commit_completed_ = true;
  matching_terminal_committed_ = committed;
}

}  // namespace taffy::test
