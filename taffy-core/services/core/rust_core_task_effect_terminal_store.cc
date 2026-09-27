// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <string>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_terminal_internal.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bool SameOperation(const mojom::OperationEnvelope& left,
                   const mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

bool IsStoreOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kHistorySearch ||
         operation == mojom::TaskActionOperationKind::kHistoryRecent ||
         operation == mojom::TaskActionOperationKind::kBookmarksSearch ||
         operation == mojom::TaskActionOperationKind::kBookmarksList ||
         operation == mojom::TaskActionOperationKind::kOpenTabsList;
}

// The same rule the sandbox applies when it admits a row: bounded, trimmed
// and free of control characters. C1 controls arrive as the UTF-8 pair
// C2 80..C2 9F and are refused with the ASCII ones.
bool BoundedField(std::string_view value) {
  if (value.size() > mojom::kMaxTaskStoreRowFieldBytes) {
    return false;
  }
  if (!value.empty() && (value.front() == ' ' || value.back() == ' ')) {
    return false;
  }
  for (size_t index = 0; index < value.size(); ++index) {
    const unsigned char byte = static_cast<unsigned char>(value[index]);
    if (byte < 0x20u || byte == 0x7fu) {
      return false;
    }
    if (byte == 0xc2u && index + 1 < value.size()) {
      const unsigned char next = static_cast<unsigned char>(value[index + 1]);
      if (next >= 0x80u && next <= 0x9fu) {
        return false;
      }
    }
  }
  return true;
}

bool ValidRow(const mojom::TaskStoreRow& row) {
  return BoundedField(row.title) && BoundedField(row.host) &&
         !row.host.empty() && BoundedField(row.path) &&
         row.path.find('?') == std::string::npos &&
         row.path.find('#') == std::string::npos;
}

}  // namespace

bool CopyTaskStoreCompletion(const mojom::TaskEffectBinding& effect,
                             const mojom::TaskEffectCompletion& completion,
                             bridge::BridgeTaskTerminal& out) {
  const auto& executable = effect.action->executable;
  const auto operation = executable->operation_kind;
  if (!IsStoreOperation(operation) || !executable->task_store ||
      !completion.effect_result || !completion.effect_result->operation ||
      !SameOperation(*effect.operation, *completion.effect_result->operation) ||
      completion.effect_result->effect_id != effect.effect_id ||
      completion.effect_result->kind != mojom::EffectKind::kBrowserAction ||
      completion.effect_result->status != mojom::EffectStatus::kCompleted ||
      !completion.effect_result->browser_action ||
      completion.effect_result->browser_action->outcome !=
          mojom::BrowserActionOutcome::kCompleted ||
      !completion.effect_result->browser_action->dispatch_id ||
      *completion.effect_result->browser_action->dispatch_id !=
          effect.action->dispatch_id ||
      completion.effect_result->browser_action->discovered_source ||
      completion.effect_result->browser_action->discovery_tab_id ||
      completion.effect_result->browser_action->browser_session_id ||
      completion.effect_result->browser_action->task_tab ||
      completion.effect_result->browser_action->task_download ||
      !completion.effect_result->browser_action->task_store) {
    return false;
  }
  const auto& result = completion.effect_result->browser_action->task_store;
  if (result->operation_kind != operation ||
      result->rows.size() > mojom::kMaxTaskStoreResults ||
      result->rows.size() > executable->task_store->limit) {
    return false;
  }
  out.has_task_store_result = true;
  out.task_store_operation = static_cast<uint8_t>(result->operation_kind);
  out.task_store_omitted = result->omitted;
  for (const mojom::TaskStoreRowPtr& row : result->rows) {
    if (!row || !ValidRow(*row)) {
      return false;
    }
    bridge::BridgeTaskStoreRow projected;
    projected.title = row->title;
    projected.host = row->host;
    projected.path = row->path;
    projected.when_utc_ms = row->when_utc_ms;
    out.task_store_rows.push_back(std::move(projected));
  }
  return true;
}

}  // namespace taffy::core_service_internal
