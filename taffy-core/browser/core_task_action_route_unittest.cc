// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_action_route.h"

#include <array>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using RouteCase = std::pair<mojom::TaskActionOperationKind, TaskActionExecutor>;

TEST(CoreTaskActionRouteTest, EveryClosedOperationHasOneReviewedExecutor) {
  constexpr auto kCases = std::to_array<RouteCase>({
      {mojom::TaskActionOperationKind::kNavigate,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kHistoryBack,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kHistoryForward,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kReload,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kStopLoading,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kSearch,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kLinkOpen,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kTabsOpen,
       TaskActionExecutor::kNavigation},
      {mojom::TaskActionOperationKind::kTabsList,
       TaskActionExecutor::kTaskTabs},
      {mojom::TaskActionOperationKind::kTabsActivate,
       TaskActionExecutor::kTaskTabs},
      {mojom::TaskActionOperationKind::kTabsClose,
       TaskActionExecutor::kTaskTabs},
      {mojom::TaskActionOperationKind::kDomQuery,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kDomRead,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kDomClick,
       TaskActionExecutor::kPageAction},
      {mojom::TaskActionOperationKind::kDomFocus,
       TaskActionExecutor::kPageAction},
      {mojom::TaskActionOperationKind::kDomScroll,
       TaskActionExecutor::kPageAction},
      {mojom::TaskActionOperationKind::kFormInspect,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kFormFill,
       TaskActionExecutor::kPageAction},
      {mojom::TaskActionOperationKind::kFormSelect,
       TaskActionExecutor::kPageAction},
      {mojom::TaskActionOperationKind::kFormToggle,
       TaskActionExecutor::kPageAction},
      {mojom::TaskActionOperationKind::kFormSubmit,
       TaskActionExecutor::kPageAction},
      {mojom::TaskActionOperationKind::kSelectionRead,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kImageDescribe,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kImageReadText,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kVideoInspect,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kPdfInspect,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kPageScreenshotInspect,
       TaskActionExecutor::kPageObservation},
      {mojom::TaskActionOperationKind::kDownloadStart,
       TaskActionExecutor::kDownloads},
      {mojom::TaskActionOperationKind::kDownloadList,
       TaskActionExecutor::kDownloads},
      {mojom::TaskActionOperationKind::kDownloadCancel,
       TaskActionExecutor::kDownloads},
      {mojom::TaskActionOperationKind::kToolJob,
       TaskActionExecutor::kNotDispatchedByBrowser},
      {mojom::TaskActionOperationKind::kLibrarySearch,
       TaskActionExecutor::kNotDispatchedByBrowser},
      {mojom::TaskActionOperationKind::kLibrarySave,
       TaskActionExecutor::kNotDispatchedByBrowser},
      {mojom::TaskActionOperationKind::kLibraryRemove,
       TaskActionExecutor::kNotDispatchedByBrowser},
      {mojom::TaskActionOperationKind::kMemorySearch,
       TaskActionExecutor::kNotDispatchedByBrowser},
      {mojom::TaskActionOperationKind::kMemorySave,
       TaskActionExecutor::kNotDispatchedByBrowser},
      {mojom::TaskActionOperationKind::kMemoryUpdate,
       TaskActionExecutor::kNotDispatchedByBrowser},
      {mojom::TaskActionOperationKind::kHistorySearch,
       TaskActionExecutor::kProfileStores},
      {mojom::TaskActionOperationKind::kHistoryRecent,
       TaskActionExecutor::kProfileStores},
      {mojom::TaskActionOperationKind::kBookmarksSearch,
       TaskActionExecutor::kProfileStores},
      {mojom::TaskActionOperationKind::kBookmarksList,
       TaskActionExecutor::kProfileStores},
      {mojom::TaskActionOperationKind::kOpenTabsList,
       TaskActionExecutor::kProfileStores},
      {mojom::TaskActionOperationKind::kMemoryDelete,
       TaskActionExecutor::kNotDispatchedByBrowser},
  });

  static_assert(kCases.size() == 43u);
  for (const auto& [operation, expected] : kCases) {
    EXPECT_EQ(expected, TaskActionExecutorForOperation(operation));
  }
}

}  // namespace
}  // namespace taffy
