// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_action_route.h"

namespace taffy {

namespace mojom = core_service::mojom;

TaskActionExecutor TaskActionExecutorForOperation(
    mojom::TaskActionOperationKind operation) {
  switch (operation) {
    case mojom::TaskActionOperationKind::kNavigate:
    case mojom::TaskActionOperationKind::kHistoryBack:
    case mojom::TaskActionOperationKind::kHistoryForward:
    case mojom::TaskActionOperationKind::kReload:
    case mojom::TaskActionOperationKind::kStopLoading:
    case mojom::TaskActionOperationKind::kSearch:
    case mojom::TaskActionOperationKind::kLinkOpen:
    case mojom::TaskActionOperationKind::kTabsOpen:
      return TaskActionExecutor::kNavigation;
    case mojom::TaskActionOperationKind::kTabsList:
    case mojom::TaskActionOperationKind::kTabsActivate:
    case mojom::TaskActionOperationKind::kTabsClose:
      return TaskActionExecutor::kTaskTabs;
    case mojom::TaskActionOperationKind::kDomQuery:
    case mojom::TaskActionOperationKind::kDomRead:
    case mojom::TaskActionOperationKind::kFormInspect:
    case mojom::TaskActionOperationKind::kSelectionRead:
    case mojom::TaskActionOperationKind::kImageDescribe:
    case mojom::TaskActionOperationKind::kImageReadText:
    case mojom::TaskActionOperationKind::kVideoInspect:
    case mojom::TaskActionOperationKind::kPdfInspect:
    case mojom::TaskActionOperationKind::kPageScreenshotInspect:
      return TaskActionExecutor::kPageObservation;
    case mojom::TaskActionOperationKind::kDomClick:
    case mojom::TaskActionOperationKind::kDomFocus:
    case mojom::TaskActionOperationKind::kDomScroll:
    case mojom::TaskActionOperationKind::kFormFill:
    case mojom::TaskActionOperationKind::kFormSelect:
    case mojom::TaskActionOperationKind::kFormToggle:
    case mojom::TaskActionOperationKind::kFormSubmit:
      return TaskActionExecutor::kPageAction;
    case mojom::TaskActionOperationKind::kDownloadStart:
    case mojom::TaskActionOperationKind::kDownloadList:
    case mojom::TaskActionOperationKind::kDownloadCancel:
      return TaskActionExecutor::kDownloads;
    case mojom::TaskActionOperationKind::kHistorySearch:
    case mojom::TaskActionOperationKind::kHistoryRecent:
    case mojom::TaskActionOperationKind::kBookmarksSearch:
    case mojom::TaskActionOperationKind::kBookmarksList:
    case mojom::TaskActionOperationKind::kOpenTabsList:
      return TaskActionExecutor::kProfileStores;
    case mojom::TaskActionOperationKind::kToolJob:
    case mojom::TaskActionOperationKind::kLibrarySearch:
    case mojom::TaskActionOperationKind::kLibrarySave:
    case mojom::TaskActionOperationKind::kLibraryRemove:
    case mojom::TaskActionOperationKind::kMemorySearch:
    case mojom::TaskActionOperationKind::kMemorySave:
    case mojom::TaskActionOperationKind::kMemoryUpdate:
    case mojom::TaskActionOperationKind::kMemoryDelete:
      return TaskActionExecutor::kNotDispatchedByBrowser;
  }
  return TaskActionExecutor::kNotDispatchedByBrowser;
}

}  // namespace taffy
