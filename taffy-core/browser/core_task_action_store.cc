// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>

#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_canonical_intent.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

// The canonical kind byte of each store read, as task-engine freezes it.
std::optional<uint8_t> CanonicalStoreKind(
    mojom::TaskActionOperationKind operation) {
  switch (operation) {
    case mojom::TaskActionOperationKind::kHistorySearch:
      return 0u;
    case mojom::TaskActionOperationKind::kHistoryRecent:
      return 1u;
    case mojom::TaskActionOperationKind::kBookmarksSearch:
      return 2u;
    case mojom::TaskActionOperationKind::kBookmarksList:
      return 3u;
    case mojom::TaskActionOperationKind::kOpenTabsList:
      return 4u;
    default:
      return std::nullopt;
  }
}

}  // namespace

bool IsValidTaskStoreAction(const mojom::TaskActionEffect& action) {
  if (!action.document || !action.executable ||
      !action.executable->task_store || action.executable->task_tab ||
      action.executable->task_download || action.observation ||
      action.executable->node_id || action.executable->destination_origin ||
      action.executable->destination_address ||
      action.executable->transient_search_query ||
      action.document->graph_revision == 0u ||
      action.executable->action_class !=
          mojom::PolicyActionClass::kProfileStoreRead ||
      !TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                        action.executable->action_class,
                                        action.executable->tool_name) ||
      action.preconditions.size() != 2u ||
      action.preconditions[0] !=
          mojom::TaskActionPrecondition::kDocumentUnchanged ||
      action.preconditions[1] !=
          mojom::TaskActionPrecondition::kGraphRevisionAtLeast ||
      action.postcondition != mojom::TaskActionPostcondition::kStoreRowsListed) {
    return false;
  }
  const std::optional<uint8_t> kind =
      CanonicalStoreKind(action.executable->operation_kind);
  const mojom::TaskStoreActionBinding& store = *action.executable->task_store;
  if (!kind || store.limit == 0u || store.limit > mojom::kMaxTaskStoreResults) {
    return false;
  }
  if (IsTaskStoreSearchOperation(action.executable->operation_kind)) {
    // The words are the browser's copy of the resident operand; the frozen
    // intent names them by digest and by handle, and both must agree.
    return action.executable->operand_handle && store.query &&
           !store.query->empty() &&
           store.query->size() <= mojom::kMaxTaskStoreQueryBytes &&
           CanonicalStoreSearchIntentMatches(
               action.executable->canonical_intent, *kind,
               action.executable->tab_id, *action.executable->operand_handle,
               *store.query, store.limit);
  }
  return !action.executable->operand_handle && !store.query &&
         CanonicalStoreListIntentMatches(action.executable->canonical_intent,
                                         *kind, action.executable->tab_id,
                                         store.limit);
}

}  // namespace taffy
