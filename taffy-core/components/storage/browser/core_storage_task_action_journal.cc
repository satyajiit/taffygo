// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_backend.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

constexpr size_t kMaxTupleOriginChars = 512u;

bool FitsSqlInteger(uint64_t value) {
  return value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

bool IsKnown(ActionType value) {
  switch (value) {
    case ActionType::kActivate:
    case ActionType::kFocus:
    case ActionType::kScrollIntoView:
    case ActionType::kSetText:
    case ActionType::kSelectOption:
    case ActionType::kToggle:
    case ActionType::kSubmitForm:
      return true;
  }
  return false;
}

bool IsKnown(BrowserCommandType value) {
  return std::find(kAllBrowserCommandTypes.begin(),
                   kAllBrowserCommandTypes.end(), value) !=
         kAllBrowserCommandTypes.end();
}

bool IsKnown(IdempotencyPolicy value) {
  switch (value) {
    case IdempotencyPolicy::kPureRead:
    case IdempotencyPolicy::kIdempotentWrite:
    case IdempotencyPolicy::kConditionallyIdempotent:
    case IdempotencyPolicy::kNonIdempotent:
      return true;
  }
  return false;
}

bool IsKnown(ActionResultCode value) {
  return IsKnownActionResultCode(value);
}

bool IsKnown(PreconditionKind value) {
  switch (value) {
    case PreconditionKind::kExactPageEpoch:
    case PreconditionKind::kAcceptableGraphRevision:
    case PreconditionKind::kExactOrigin:
    case PreconditionKind::kAllowedRedirectSet:
    case PreconditionKind::kDocumentActive:
    case PreconditionKind::kNodeExists:
    case PreconditionKind::kNodeRoleUnchanged:
    case PreconditionKind::kNodeActionAvailable:
    case PreconditionKind::kNodeStateAsserted:
    case PreconditionKind::kNodeStateAbsent:
    case PreconditionKind::kExpectedDestination:
    case PreconditionKind::kExpectedValueDigest:
    case PreconditionKind::kNotSensitiveField:
    case PreconditionKind::kNoUserInteractionSinceLease:
    case PreconditionKind::kBudgetRemaining:
    case PreconditionKind::kDestinationClassAllowed:
    case PreconditionKind::kContentTrustAtLeast:
    case PreconditionKind::kPreparedEffectUnchanged:
    case PreconditionKind::kNoUndeclaredEgress:
      return true;
  }
  return false;
}

bool IsNormalizedOrigin(const Origin& origin) {
  switch (origin.kind) {
    case OriginKind::kTuple: {
      if (origin.serialization.empty() ||
          origin.serialization.size() > kMaxTupleOriginChars ||
          !origin.opaque_id.empty()) {
        return false;
      }
      const GURL parsed(origin.serialization);
      const url::Origin normalized = url::Origin::Create(parsed);
      return parsed.is_valid() && !normalized.opaque() &&
             normalized.Serialize() == origin.serialization;
    }
    case OriginKind::kOpaque:
      return origin.serialization.empty() && !origin.opaque_id.empty() &&
             origin.opaque_id.size() <= kMaxIdentifierChars;
  }
  return false;
}

// The closed table and the switch in bip_action.h are one rule stated twice.
// They are proved equal here, in the only translation unit that spends the
// answer, rather than in the header where they are declared: this file is what
// refuses an intent, and the durable journal's SQL is rendered from the same
// table by core_service_schema_validation.py.
constexpr size_t CountDocumentBoundBrowserCommands() {
  size_t count = 0;
  for (BrowserCommandType command : kAllBrowserCommandTypes) {
    if (BrowserCommandRequiresDocument(command)) {
      ++count;
    }
  }
  return count;
}

constexpr bool EveryDocumentBoundCommandIsNamed() {
  for (BrowserCommandType command : kDocumentBoundBrowserCommandTypes) {
    if (!BrowserCommandRequiresDocument(command)) {
      return false;
    }
  }
  return true;
}

static_assert(EveryDocumentBoundCommandIsNamed(),
              "every command in the table must answer true");
static_assert(kDocumentBoundBrowserCommandTypes.size() ==
                  CountDocumentBoundBrowserCommands(),
              "the table must name every command that answers true");

bool IsValidIntent(const DispatchIntentRecord& record) {
  if (!record.dispatch_id.is_valid() || !record.task_id.is_valid() ||
      !record.action_id.is_valid() || !record.capability_reference.is_valid() ||
      !record.actor_lease_id.is_valid() || !record.tab_id.is_valid() ||
      record.action_type.has_value() == record.command_type.has_value() ||
      (record.action_type && !IsKnown(*record.action_type)) ||
      (record.command_type && !IsKnown(*record.command_type)) ||
      !IsKnown(record.idempotency_policy) ||
      !FitsSqlInteger(record.graph_revision) ||
      !FitsSqlInteger(record.recorded_at_monotonic_ms)) {
    return false;
  }
  const bool needs_document =
      record.action_type.has_value() ||
      (record.command_type &&
       BrowserCommandRequiresDocument(*record.command_type));
  if (needs_document) {
    return record.frame_id.is_valid() && record.page_epoch.is_valid() &&
           record.graph_revision > 0u && IsNormalizedOrigin(record.origin);
  }
  return record.frame_id.value.empty() && record.page_epoch.value.empty() &&
         record.graph_revision == 0u && record.origin.serialization.empty() &&
         record.origin.opaque_id.empty();
}

bool IsValidResult(const ActionResult& result) {
  const bool has_observation = result.observed_page_epoch.has_value();
  return result.request_id.is_valid() && result.action_id.is_valid() &&
         result.dispatch_id.is_valid() && IsKnown(result.result_code) &&
         result.terminal && FitsSqlInteger(result.completed_at_monotonic_ms) &&
         has_observation == result.observed_graph_revision.has_value() &&
         (!result.observed_page_epoch ||
          result.observed_page_epoch->is_valid()) &&
         (!result.observed_graph_revision ||
          FitsSqlInteger(*result.observed_graph_revision)) &&
         (!result.failed_precondition ||
          IsKnown(*result.failed_precondition)) &&
         (result.dispatched || !result.repeat_may_duplicate_effect) &&
         (result.result_code != ActionResultCode::kVerified ||
          result.dispatched);
}

void BindOptionalInt(sql::Statement* statement,
                     int column,
                     std::optional<int> value) {
  if (value) {
    statement->BindInt(column, *value);
  } else {
    statement->BindNull(column);
  }
}

void BindOptionalString(sql::Statement* statement,
                        int column,
                        const std::optional<std::string>& value) {
  if (value) {
    statement->BindString(column, *value);
  } else {
    statement->BindNull(column);
  }
}

void BindOptionalInt64(sql::Statement* statement,
                       int column,
                       std::optional<uint64_t> value) {
  if (value) {
    statement->BindInt64(column, static_cast<int64_t>(*value));
  } else {
    statement->BindNull(column);
  }
}

bool OptionalStringMatches(sql::Statement* statement,
                           int column,
                           const std::optional<std::string>& expected) {
  const bool is_null =
      statement->GetColumnType(column) == sql::ColumnType::kNull;
  return expected ? !is_null && statement->ColumnString(column) == *expected
                  : is_null;
}

bool OptionalIntMatches(sql::Statement* statement,
                        int column,
                        const std::optional<int>& expected) {
  const bool is_null =
      statement->GetColumnType(column) == sql::ColumnType::kNull;
  return expected ? !is_null && statement->ColumnInt(column) == *expected
                  : is_null;
}

bool OptionalInt64Matches(sql::Statement* statement,
                          int column,
                          std::optional<uint64_t> expected) {
  const bool is_null =
      statement->GetColumnType(column) == sql::ColumnType::kNull;
  return expected ? !is_null && statement->ColumnInt64(column) >= 0 &&
                        static_cast<uint64_t>(statement->ColumnInt64(column)) ==
                            *expected
                  : is_null;
}

}  // namespace

bool CoreStorageBroker::Backend::AppendTaskActionIntent(
    DispatchIntentRecord record) {
  if (!IsValidIntent(record) || !EnsureOpen()) {
    return false;
  }
  sql::Transaction transaction(&database_);
  if (!transaction.Begin()) {
    return false;
  }
  sql::Statement insert(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO core_task_action_journal("
      "dispatch_id,task_id,action_id,capability_reference,actor_lease_id,"
      "tab_id,action_type,command_type,frame_id,page_epoch,graph_revision,"
      "origin_kind,origin_value,idempotency_policy,recorded_at_monotonic_ms) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  insert.BindString(0, record.dispatch_id.value);
  insert.BindString(1, record.task_id.value);
  insert.BindString(2, record.action_id.value);
  insert.BindString(3, record.capability_reference.value);
  insert.BindString(4, record.actor_lease_id.value);
  insert.BindString(5, record.tab_id.value);
  BindOptionalInt(
      &insert, 6,
      record.action_type
          ? std::optional<int>(static_cast<int>(*record.action_type))
          : std::nullopt);
  BindOptionalInt(
      &insert, 7,
      record.command_type
          ? std::optional<int>(static_cast<int>(*record.command_type))
          : std::nullopt);
  const bool has_document = record.frame_id.is_valid();
  BindOptionalString(&insert, 8,
                     has_document
                         ? std::optional<std::string>(record.frame_id.value)
                         : std::nullopt);
  BindOptionalString(&insert, 9,
                     has_document
                         ? std::optional<std::string>(record.page_epoch.value)
                         : std::nullopt);
  insert.BindInt64(10, static_cast<int64_t>(record.graph_revision));
  BindOptionalInt(&insert, 11,
                  has_document
                      ? std::optional<int>(static_cast<int>(record.origin.kind))
                      : std::nullopt);
  BindOptionalString(&insert, 12,
                     has_document ? std::optional<std::string>(
                                        record.origin.kind == OriginKind::kTuple
                                            ? record.origin.serialization
                                            : record.origin.opaque_id)
                                  : std::nullopt);
  insert.BindInt(13, static_cast<int>(record.idempotency_policy));
  insert.BindInt64(14, static_cast<int64_t>(record.recorded_at_monotonic_ms));
  // A dispatch identifier is a durable execution claim. Even an identical
  // replay is refused; differing facts under the same identity are refused by
  // the same UNIQUE edge, including after a browser restart.
  return insert.Run() && database_.GetLastChangeCount() == 1 &&
         transaction.Commit();
}

bool CoreStorageBroker::Backend::AppendTaskActionResult(ActionResult result) {
  if (!IsValidResult(result) || !EnsureOpen()) {
    return false;
  }
  sql::Transaction transaction(&database_);
  if (!transaction.Begin()) {
    return false;
  }
  sql::Statement update(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_task_action_journal SET request_id=?,result_code=?,"
      "dispatched=?,completed_at_monotonic_ms=?,"
      "repeat_may_duplicate_effect=?,observed_page_epoch=?,"
      "observed_graph_revision=?,failed_precondition=? "
      "WHERE dispatch_id=? AND action_id=? AND result_code IS NULL"));
  update.BindString(0, result.request_id.value);
  update.BindInt(1, static_cast<int>(result.result_code));
  update.BindInt(2, result.dispatched ? 1 : 0);
  update.BindInt64(3, static_cast<int64_t>(result.completed_at_monotonic_ms));
  update.BindInt(4, result.repeat_may_duplicate_effect ? 1 : 0);
  BindOptionalString(
      &update, 5,
      result.observed_page_epoch
          ? std::optional<std::string>(result.observed_page_epoch->value)
          : std::nullopt);
  BindOptionalInt64(&update, 6, result.observed_graph_revision);
  BindOptionalInt(
      &update, 7,
      result.failed_precondition
          ? std::optional<int>(static_cast<int>(*result.failed_precondition))
          : std::nullopt);
  update.BindString(8, result.dispatch_id.value);
  update.BindString(9, result.action_id.value);
  if (!update.Run()) {
    return false;
  }
  if (database_.GetLastChangeCount() == 1) {
    return transaction.Commit();
  }

  // Terminal delivery can repeat after an adapter race. The exact same facts
  // are idempotent; any changed fact is a conflicting history and fails.
  sql::Statement existing(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT request_id,result_code,dispatched,completed_at_monotonic_ms,"
      "repeat_may_duplicate_effect,observed_page_epoch,"
      "observed_graph_revision,failed_precondition "
      "FROM core_task_action_journal WHERE dispatch_id=? AND action_id=?"));
  existing.BindString(0, result.dispatch_id.value);
  existing.BindString(1, result.action_id.value);
  if (!existing.Step()) {
    return false;
  }
  const std::optional<std::string> observed_page_epoch =
      result.observed_page_epoch
          ? std::optional<std::string>(result.observed_page_epoch->value)
          : std::nullopt;
  const std::optional<int> failed_precondition =
      result.failed_precondition
          ? std::optional<int>(static_cast<int>(*result.failed_precondition))
          : std::nullopt;
  const bool identical =
      existing.ColumnString(0) == result.request_id.value &&
      existing.ColumnInt(1) == static_cast<int>(result.result_code) &&
      existing.ColumnInt(2) == (result.dispatched ? 1 : 0) &&
      existing.ColumnInt64(3) ==
          static_cast<int64_t>(result.completed_at_monotonic_ms) &&
      existing.ColumnInt(4) == (result.repeat_may_duplicate_effect ? 1 : 0) &&
      OptionalStringMatches(&existing, 5, observed_page_epoch) &&
      OptionalInt64Matches(&existing, 6, result.observed_graph_revision) &&
      OptionalIntMatches(&existing, 7, failed_precondition);
  return identical && transaction.Commit();
}

}  // namespace taffy
