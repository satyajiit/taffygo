// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <utility>

#include "taffy/browser/core_task_action.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using SettlesCase = std::pair<mojom::TaskActionOperationKind, bool>;

// The set is a contract with the core's terminal decoder, not a local
// convenience. `decode_discovered_source` in
// //taffy/services/core/service_bridge_task_effect/action_terminal.rs admits a
// discovered source on exactly these operations plus `kTabsOpen`, which the
// browser handles on its own branch; a terminal that names a source on any
// other operation is refused whole, which ends the core rather than the
// action. So this table is exhaustive over the closed enumeration on purpose:
// an operation added to the contract has to be classified here deliberately,
// and the compiler cannot say that for it.
TEST(CoreTaskActionOperationsTest, OnlyAMoveThatLandsItsOwnTabMaySettleIt) {
  constexpr auto kCases = std::to_array<SettlesCase>({
      {mojom::TaskActionOperationKind::kNavigate, true},
      {mojom::TaskActionOperationKind::kSearch, true},
      {mojom::TaskActionOperationKind::kHistoryBack, true},
      {mojom::TaskActionOperationKind::kHistoryForward, true},
      {mojom::TaskActionOperationKind::kReload, true},
      {mojom::TaskActionOperationKind::kLinkOpen, true},
      {mojom::TaskActionOperationKind::kFormSubmit, true},
      // A new tab is a source for a tab that did not exist; it is offered on
      // its own branch, against the action rather than the tab.
      {mojom::TaskActionOperationKind::kTabsOpen, false},
      // Stopping a load lands nowhere new.
      {mojom::TaskActionOperationKind::kStopLoading, false},
      // Reads and queries. The case that ended the core: a read noticed that
      // its tab had moved, the browser offered a source for it, and the
      // terminal could not carry one.
      {mojom::TaskActionOperationKind::kDomQuery, false},
      {mojom::TaskActionOperationKind::kDomRead, false},
      {mojom::TaskActionOperationKind::kFormInspect, false},
      {mojom::TaskActionOperationKind::kSelectionRead, false},
      {mojom::TaskActionOperationKind::kImageDescribe, false},
      {mojom::TaskActionOperationKind::kImageReadText, false},
      {mojom::TaskActionOperationKind::kVideoInspect, false},
      {mojom::TaskActionOperationKind::kPdfInspect, false},
      {mojom::TaskActionOperationKind::kPageScreenshotInspect, false},
      // Page acts. A press may well move the tab, and it is still not a move
      // that declared a destination anyone can check.
      {mojom::TaskActionOperationKind::kDomClick, false},
      {mojom::TaskActionOperationKind::kDomFocus, false},
      {mojom::TaskActionOperationKind::kDomScroll, false},
      {mojom::TaskActionOperationKind::kFormFill, false},
      {mojom::TaskActionOperationKind::kFormSelect, false},
      {mojom::TaskActionOperationKind::kFormToggle, false},
      // Tab control, downloads, tools and the typed stores.
      {mojom::TaskActionOperationKind::kTabsList, false},
      {mojom::TaskActionOperationKind::kTabsActivate, false},
      {mojom::TaskActionOperationKind::kTabsClose, false},
      {mojom::TaskActionOperationKind::kOpenTabsList, false},
      {mojom::TaskActionOperationKind::kDownloadStart, false},
      {mojom::TaskActionOperationKind::kDownloadList, false},
      {mojom::TaskActionOperationKind::kDownloadCancel, false},
      {mojom::TaskActionOperationKind::kToolJob, false},
      {mojom::TaskActionOperationKind::kLibrarySearch, false},
      {mojom::TaskActionOperationKind::kLibrarySave, false},
      {mojom::TaskActionOperationKind::kLibraryRemove, false},
      {mojom::TaskActionOperationKind::kMemorySearch, false},
      {mojom::TaskActionOperationKind::kMemorySave, false},
      {mojom::TaskActionOperationKind::kMemoryUpdate, false},
      {mojom::TaskActionOperationKind::kMemoryDelete, false},
      {mojom::TaskActionOperationKind::kHistorySearch, false},
      {mojom::TaskActionOperationKind::kHistoryRecent, false},
      {mojom::TaskActionOperationKind::kBookmarksSearch, false},
      {mojom::TaskActionOperationKind::kBookmarksList, false},
  });
  static_assert(kCases.size() ==
                    static_cast<size_t>(
                        mojom::TaskActionOperationKind::kMaxValue) +
                        1u,
                "every operation is classified");
  for (const auto& [operation, settles] : kCases) {
    EXPECT_EQ(TaskActionOperationSettlesItsOwnTab(operation), settles)
        << "operation " << static_cast<int>(operation);
  }
}

}  // namespace
}  // namespace taffy
