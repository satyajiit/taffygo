// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>

#include "taffy/browser/core_task_action.h"

// Which operations form which family, and the class a typed (page-free)
// operation carries. `TaskOperationMatchesClassAndTool` in core_task_action.cc
// binds an operation to its class and tool for an effect the core dispatched;
// these predicates are the same families read one at a time, for the sites
// that branch on a family before they have an effect to validate.

namespace taffy {

namespace mojom = core_service::mojom;

bool IsTaskObservationOperation(mojom::TaskActionOperationKind operation) {
  switch (operation) {
    case mojom::TaskActionOperationKind::kDomQuery:
    case mojom::TaskActionOperationKind::kDomRead:
    case mojom::TaskActionOperationKind::kFormInspect:
    case mojom::TaskActionOperationKind::kSelectionRead:
    case mojom::TaskActionOperationKind::kImageDescribe:
    case mojom::TaskActionOperationKind::kImageReadText:
    case mojom::TaskActionOperationKind::kVideoInspect:
    case mojom::TaskActionOperationKind::kPdfInspect:
    case mojom::TaskActionOperationKind::kPageScreenshotInspect:
      return true;
    default:
      return false;
  }
}

bool IsTaskHistoryOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kHistoryBack ||
         operation == mojom::TaskActionOperationKind::kHistoryForward;
}

bool IsTaskTabControlOperation(mojom::TaskActionOperationKind operation) {
  return IsTaskHistoryOperation(operation) ||
         operation == mojom::TaskActionOperationKind::kReload ||
         operation == mojom::TaskActionOperationKind::kStopLoading;
}

bool IsTaskNavigationOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kNavigate;
}

bool IsTaskSearchOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kSearch;
}

bool IsTaskTabOpenOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kTabsOpen;
}

// The moves whose terminal may name a source the browser discovered for the
// tab they ran in. It is the exact set the core's terminal decoder admits
// (`decode_discovered_source` in
// //taffy/services/core/service_bridge_task_effect/action_terminal.rs), and it
// is a family because of what these moves are, not because of what they are
// allowed to do: each of them lands the tab somewhere, so each of them has a
// destination the browser can state and a reader can check. A read, a query
// and a press do not — offering a source on one of those produces an envelope
// the core refuses whole, ending the task.
bool TaskActionOperationSettlesItsOwnTab(
    mojom::TaskActionOperationKind operation) {
  return IsTaskNavigationOperation(operation) ||
         IsTaskSearchOperation(operation) ||
         IsTaskHistoryOperation(operation) ||
         operation == mojom::TaskActionOperationKind::kReload ||
         IsTaskLinkOpenOperation(operation) ||
         operation == mojom::TaskActionOperationKind::kFormSubmit;
}

bool IsTaskTabOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kTabsList ||
         operation == mojom::TaskActionOperationKind::kTabsActivate ||
         operation == mojom::TaskActionOperationKind::kTabsClose;
}

bool IsTaskDownloadOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kDownloadStart ||
         operation == mojom::TaskActionOperationKind::kDownloadList ||
         operation == mojom::TaskActionOperationKind::kDownloadCancel;
}

bool IsTaskLibraryOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kLibrarySearch ||
         operation == mojom::TaskActionOperationKind::kLibrarySave ||
         operation == mojom::TaskActionOperationKind::kLibraryRemove;
}

bool IsTaskLinkOpenOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kLinkOpen;
}

bool IsTaskStoreOperation(mojom::TaskActionOperationKind operation) {
  return IsTaskStoreSearchOperation(operation) ||
         operation == mojom::TaskActionOperationKind::kHistoryRecent ||
         operation == mojom::TaskActionOperationKind::kBookmarksList ||
         operation == mojom::TaskActionOperationKind::kOpenTabsList;
}

bool IsTaskStoreSearchOperation(mojom::TaskActionOperationKind operation) {
  return operation == mojom::TaskActionOperationKind::kHistorySearch ||
         operation == mojom::TaskActionOperationKind::kBookmarksSearch;
}

std::optional<mojom::PolicyActionClass> TypedTaskOperationClass(
    mojom::TaskActionOperationKind operation) {
  switch (operation) {
    case mojom::TaskActionOperationKind::kTabsList:
      return mojom::PolicyActionClass::kObservePage;
    case mojom::TaskActionOperationKind::kTabsActivate:
      return mojom::PolicyActionClass::kMoveFocus;
    case mojom::TaskActionOperationKind::kTabsClose:
      return mojom::PolicyActionClass::kCreateTaskTab;
    case mojom::TaskActionOperationKind::kHistorySearch:
    case mojom::TaskActionOperationKind::kHistoryRecent:
    case mojom::TaskActionOperationKind::kBookmarksSearch:
    case mojom::TaskActionOperationKind::kBookmarksList:
    case mojom::TaskActionOperationKind::kOpenTabsList:
      return mojom::PolicyActionClass::kProfileStoreRead;
    case mojom::TaskActionOperationKind::kLibrarySearch:
      return mojom::PolicyActionClass::kLibraryRead;
    case mojom::TaskActionOperationKind::kLibrarySave:
    case mojom::TaskActionOperationKind::kLibraryRemove:
      return mojom::PolicyActionClass::kLibraryWrite;
    case mojom::TaskActionOperationKind::kMemorySearch:
      return mojom::PolicyActionClass::kMemoryRead;
    case mojom::TaskActionOperationKind::kMemorySave:
    case mojom::TaskActionOperationKind::kMemoryUpdate:
    case mojom::TaskActionOperationKind::kMemoryDelete:
      return mojom::PolicyActionClass::kMemoryWrite;
    default:
      return std::nullopt;
  }
}

}  // namespace taffy
