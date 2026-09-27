// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/task_benchmark_audit_reader.h"

#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/notreached.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/components/storage/browser/core_storage_broker.h"

namespace taffy {

class CoreServiceManagerStorageTestPeer final {
 public:
  static void LoadBootstrap(
      CoreServiceManager& manager,
      CoreStorageBroker::BootstrapCallback callback) {
    if (!manager.storage_broker_) {
      std::move(callback).Run(nullptr);
      return;
    }
    manager.storage_broker_->LoadBootstrap(
        manager.service_generation_, manager.private_profile_,
        std::move(callback));
  }

  static void ReadTaskActionJournal(
      CoreServiceManager& manager,
      DispatchId dispatch_id,
      TaskId task_id,
      ActionId action_id,
      CoreStorageBroker::TaskActionLookupCallback callback) {
    if (!manager.storage_broker_) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    manager.storage_broker_->ReadTaskActionJournal(
        std::move(dispatch_id), std::move(task_id), std::move(action_id),
        std::move(callback));
  }
};

}  // namespace taffy

namespace taffy::test {
namespace {

namespace service = core_service::mojom;

void OnCommittedTaskRead(std::string expected_task_id,
                         TaskBenchmarkAuditReadCallback callback,
                         service::TaskRestoreRecordPtr task) {
  TaskBenchmarkAuditReadResult result;
  if (!task) {
    result.state = TaskBenchmarkAuditReadState::kTaskAbsent;
    result.error = "the exact committed task was absent or unreadable";
    std::move(callback).Run(std::move(result));
    return;
  }
  if (task->task_id != expected_task_id || task->batches.empty()) {
    result.state = TaskBenchmarkAuditReadState::kMalformedBatch;
    result.error = "the committed task identity or batch list is malformed";
    std::move(callback).Run(std::move(result));
    return;
  }

  std::vector<TaskBenchmarkAuditRecord> records;
  for (const service::CommittedTaskBatchPtr& batch : task->batches) {
    if (!batch) {
      result.state = TaskBenchmarkAuditReadState::kMalformedBatch;
      result.error = "the committed task contains a null batch";
      std::move(callback).Run(std::move(result));
      return;
    }
    std::string error;
    std::optional<std::vector<TaskBenchmarkAuditRecord>> decoded =
        DecodeTaskBenchmarkAuditBatchForTesting(batch->transaction_batch,
                                                &error);
    if (!decoded) {
      result.state = TaskBenchmarkAuditReadState::kMalformedBatch;
      result.error = std::move(error);
      std::move(callback).Run(std::move(result));
      return;
    }
    records.insert(records.end(), std::make_move_iterator(decoded->begin()),
                   std::make_move_iterator(decoded->end()));
  }

  uint64_t previous_sequence = 0u;
  for (const TaskBenchmarkAuditRecord& record : records) {
    if (record.task_id != expected_task_id ||
        record.sequence <= previous_sequence ||
        record.sequence >
            static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        record.event_type_wire >
            static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      result.state = TaskBenchmarkAuditReadState::kMalformedBatch;
      result.error =
          "the durable audit stream has a foreign or unordered record";
      std::move(callback).Run(std::move(result));
      return;
    }
    previous_sequence = record.sequence;
  }
  result.state = TaskBenchmarkAuditReadState::kRead;
  result.records = std::move(records);
  std::move(callback).Run(std::move(result));
}

void OnTaskBootstrapRead(std::string expected_task_id,
                         TaskBenchmarkAuditReadCallback callback,
                         service::CoreBootstrapPtr bootstrap) {
  service::TaskRestoreRecordPtr match;
  if (bootstrap) {
    for (auto& task : bootstrap->tasks) {
      if (!task || task->task_id != expected_task_id) {
        continue;
      }
      if (match) {
        OnCommittedTaskRead(std::move(expected_task_id), std::move(callback),
                            nullptr);
        return;
      }
      match = std::move(task);
    }
  }
  OnCommittedTaskRead(std::move(expected_task_id), std::move(callback),
                      std::move(match));
}

}  // namespace

std::string_view TaskBenchmarkAuditReadStateName(
    TaskBenchmarkAuditReadState state) {
  switch (state) {
    case TaskBenchmarkAuditReadState::kRead:
      return "durable-storage-read";
    case TaskBenchmarkAuditReadState::kStorageUnavailable:
      return "storage-unavailable";
    case TaskBenchmarkAuditReadState::kTaskAbsent:
      return "task-absent-or-unreadable";
    case TaskBenchmarkAuditReadState::kMalformedBatch:
      return "malformed-transaction-batch";
  }
  NOTREACHED();
}

void ReadTaskBenchmarkAudit(CoreServiceManager* manager,
                            std::string task_id,
                            TaskBenchmarkAuditReadCallback callback) {
  if (!manager || task_id.empty()) {
    TaskBenchmarkAuditReadResult result;
    result.state = TaskBenchmarkAuditReadState::kStorageUnavailable;
    result.error = "the profile core or exact task identity is unavailable";
    std::move(callback).Run(std::move(result));
    return;
  }
  const std::string expected_task_id = task_id;
  CoreServiceManagerStorageTestPeer::LoadBootstrap(
      *manager, base::BindOnce(&OnTaskBootstrapRead, expected_task_id,
                               std::move(callback)));
}

void ReadTaskBenchmarkActionJournal(
    CoreServiceManager* manager,
    std::string dispatch_id,
    std::string task_id,
    std::string action_id,
    TaskBenchmarkActionJournalReadCallback callback) {
  if (!manager || dispatch_id.empty() || task_id.empty() || action_id.empty()) {
    std::move(callback).Run(TaskBenchmarkActionJournalReadResult());
    return;
  }
  CoreServiceManagerStorageTestPeer::ReadTaskActionJournal(
      *manager, DispatchId{std::move(dispatch_id)}, TaskId{std::move(task_id)},
      ActionId{std::move(action_id)},
      base::BindOnce(
          [](TaskBenchmarkActionJournalReadCallback done,
             std::optional<TaskActionJournalLookup> lookup) {
            TaskBenchmarkActionJournalReadResult result;
            if (!lookup) {
              std::move(done).Run(result);
              return;
            }
            switch (lookup->state) {
              case TaskActionJournalState::kMissing:
                result.state = TaskBenchmarkActionJournalReadState::kMissing;
                break;
              case TaskActionJournalState::kIntentOnly:
                result.state =
                    TaskBenchmarkActionJournalReadState::kIntentOnly;
                break;
              case TaskActionJournalState::kTerminal:
                result.state = TaskBenchmarkActionJournalReadState::kTerminal;
                result.result_code_wire =
                    static_cast<uint32_t>(lookup->result_code);
                break;
            }
            std::move(done).Run(result);
          },
          std::move(callback)));
}

}  // namespace taffy::test
