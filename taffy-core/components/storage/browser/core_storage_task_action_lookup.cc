// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <optional>

#include "sql/statement.h"
#include "taffy/components/storage/browser/core_storage_backend.h"

namespace taffy {

std::optional<TaskActionJournalLookup>
CoreStorageBroker::Backend::ReadTaskActionJournal(DispatchId dispatch_id,
                                                  TaskId task_id,
                                                  ActionId action_id) {
  if (!dispatch_id.is_valid() || !task_id.is_valid() ||
      !action_id.is_valid() || !EnsureOpen()) {
    return std::nullopt;
  }
  sql::Statement row(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT result_code FROM core_task_action_journal "
      "WHERE dispatch_id=? AND task_id=? AND action_id=?"));
  row.BindString(0, dispatch_id.value);
  row.BindString(1, task_id.value);
  row.BindString(2, action_id.value);
  if (!row.Step()) {
    return row.Succeeded()
               ? std::make_optional(TaskActionJournalLookup{
                     .state = TaskActionJournalState::kMissing})
               : std::nullopt;
  }
  if (row.GetColumnType(0) == sql::ColumnType::kNull) {
    return TaskActionJournalLookup{
        .state = TaskActionJournalState::kIntentOnly};
  }
  const int wire_code = row.ColumnInt(0);
  if (wire_code < 0 || wire_code > std::numeric_limits<uint8_t>::max()) {
    return std::nullopt;
  }
  const auto result_code = static_cast<ActionResultCode>(wire_code);
  if (!IsKnownActionResultCode(result_code)) {
    return std::nullopt;
  }
  return TaskActionJournalLookup{
      .state = TaskActionJournalState::kTerminal,
      .result_code = result_code,
  };
}

}  // namespace taffy
