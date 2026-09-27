// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <vector>

#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_canonical_intent.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsNoneInput(const mojom::TaskActionInput& input) {
  return input.kind == mojom::TaskActionInputKind::kNone &&
         !input.supplied_value && !input.toggle_state;
}

bool HasExactSuppliedValue(const mojom::TaskActionInput& input) {
  return input.kind == mojom::TaskActionInputKind::kSuppliedValue &&
         input.supplied_value && !input.toggle_state &&
         !input.supplied_value->request_id.empty() &&
         input.supplied_value->index < mojom::kMaxTaskSuppliedValues;
}

bool HasExactToggleState(const mojom::TaskActionInput& input) {
  return input.kind == mojom::TaskActionInputKind::kToggleState &&
         !input.supplied_value && input.toggle_state;
}

}  // namespace

bool TaskActionInputMatchesOperationAndCanonical(
    const mojom::TaskActionInput* input,
    mojom::TaskActionOperationKind operation,
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::optional<std::string>& node_id) {
  if (!input) {
    return false;
  }
  switch (operation) {
    case mojom::TaskActionOperationKind::kDomClick:
      return node_id && IsNoneInput(*input) &&
             CanonicalDomActivationIntentMatches(canonical_intent, tab_id,
                                                 *node_id);
    case mojom::TaskActionOperationKind::kDomFocus:
      return node_id && IsNoneInput(*input) &&
             CanonicalDomFocusIntentMatches(canonical_intent, tab_id, *node_id);
    case mojom::TaskActionOperationKind::kFormFill:
      return node_id && HasExactSuppliedValue(*input) &&
             CanonicalFormSuppliedValueIntentMatches(
                 canonical_intent, 13u, tab_id, *node_id,
                 input->supplied_value->request_id,
                 input->supplied_value->index);
    case mojom::TaskActionOperationKind::kFormSelect:
      return node_id && HasExactSuppliedValue(*input) &&
             CanonicalFormSuppliedValueIntentMatches(
                 canonical_intent, 23u, tab_id, *node_id,
                 input->supplied_value->request_id,
                 input->supplied_value->index);
    case mojom::TaskActionOperationKind::kFormToggle:
      return node_id && HasExactToggleState(*input) &&
             CanonicalFormToggleIntentMatches(canonical_intent, tab_id,
                                              *node_id,
                                              input->toggle_state->checked);
    case mojom::TaskActionOperationKind::kFormSubmit:
      return node_id && IsNoneInput(*input) &&
             CanonicalFormSubmitIntentMatches(canonical_intent, tab_id,
                                              *node_id);
    case mojom::TaskActionOperationKind::kDomQuery:
      return !node_id && IsNoneInput(*input) &&
             CanonicalDomQueryIntentMatches(canonical_intent, tab_id);
    case mojom::TaskActionOperationKind::kLibrarySearch:
    case mojom::TaskActionOperationKind::kLibrarySave:
    case mojom::TaskActionOperationKind::kLibraryRemove:
      return !node_id && IsNoneInput(*input) &&
             CanonicalLibraryIntentMatches(
                 canonical_intent,
                 operation == mojom::TaskActionOperationKind::kLibrarySearch
                     ? 0u
                 : operation == mojom::TaskActionOperationKind::kLibrarySave
                     ? 1u
                     : 2u,
                 tab_id);
    case mojom::TaskActionOperationKind::kMemorySearch:
    case mojom::TaskActionOperationKind::kMemorySave:
    case mojom::TaskActionOperationKind::kMemoryUpdate:
    case mojom::TaskActionOperationKind::kMemoryDelete:
      return !node_id && IsNoneInput(*input) &&
             CanonicalMemoryIntentMatches(
                 canonical_intent,
                 operation == mojom::TaskActionOperationKind::kMemorySearch ? 0u
                 : operation == mojom::TaskActionOperationKind::kMemorySave ? 1u
                 : operation == mojom::TaskActionOperationKind::kMemoryUpdate
                     ? 2u
                     : 3u,
                 tab_id);
    case mojom::TaskActionOperationKind::kHistorySearch:
    case mojom::TaskActionOperationKind::kHistoryRecent:
    case mojom::TaskActionOperationKind::kBookmarksSearch:
    case mojom::TaskActionOperationKind::kBookmarksList:
    case mojom::TaskActionOperationKind::kOpenTabsList:
      return !node_id && IsNoneInput(*input) &&
             CanonicalStoreIntentMatches(
                 canonical_intent,
                 operation == mojom::TaskActionOperationKind::kHistorySearch
                     ? 0u
                 : operation == mojom::TaskActionOperationKind::kHistoryRecent
                     ? 1u
                 : operation == mojom::TaskActionOperationKind::kBookmarksSearch
                     ? 2u
                 : operation == mojom::TaskActionOperationKind::kBookmarksList
                     ? 3u
                     : 4u,
                 tab_id);
    case mojom::TaskActionOperationKind::kHistoryBack:
    case mojom::TaskActionOperationKind::kHistoryForward:
    case mojom::TaskActionOperationKind::kReload:
    case mojom::TaskActionOperationKind::kStopLoading:
      return !node_id && IsNoneInput(*input) &&
             CanonicalTabControlIntentMatches(
                 canonical_intent,
                 operation == mojom::TaskActionOperationKind::kHistoryBack ? 2u
                 : operation == mojom::TaskActionOperationKind::kHistoryForward
                     ? 3u
                 : operation == mojom::TaskActionOperationKind::kReload ? 28u
                                                                        : 29u,
                 tab_id);
    case mojom::TaskActionOperationKind::kNavigate:
    case mojom::TaskActionOperationKind::kSearch:
    case mojom::TaskActionOperationKind::kTabsOpen:
    case mojom::TaskActionOperationKind::kTabsList:
    case mojom::TaskActionOperationKind::kTabsActivate:
    case mojom::TaskActionOperationKind::kTabsClose:
    case mojom::TaskActionOperationKind::kDomRead:
    case mojom::TaskActionOperationKind::kDomScroll:
    case mojom::TaskActionOperationKind::kFormInspect:
    case mojom::TaskActionOperationKind::kDownloadStart:
    case mojom::TaskActionOperationKind::kDownloadList:
    case mojom::TaskActionOperationKind::kDownloadCancel:
    case mojom::TaskActionOperationKind::kSelectionRead:
    case mojom::TaskActionOperationKind::kImageDescribe:
    case mojom::TaskActionOperationKind::kImageReadText:
    case mojom::TaskActionOperationKind::kVideoInspect:
    case mojom::TaskActionOperationKind::kPdfInspect:
    case mojom::TaskActionOperationKind::kPageScreenshotInspect:
    case mojom::TaskActionOperationKind::kToolJob:
    case mojom::TaskActionOperationKind::kLinkOpen:
      return IsNoneInput(*input);
  }
  return false;
}

}  // namespace taffy
