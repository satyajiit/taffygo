// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_ACTION_ROUTE_H_
#define TAFFY_BROWSER_CORE_TASK_ACTION_ROUTE_H_

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-shared.h"

namespace taffy {

// The one reviewed browser executor that owns a dispatched task operation.
// Core-owned aggregate tools and sandboxed jobs use their own effect families,
// so they deliberately have no DispatchAction executor here.
enum class TaskActionExecutor {
  kPageObservation,
  kPageAction,
  kNavigation,
  kTaskTabs,
  kDownloads,
  // A read of a store the start attached: History, Bookmarks or the person's
  // own open tabs (decision 0133).
  kProfileStores,
  kNotDispatchedByBrowser,
};

// Total over TaskActionOperationKind. Keeping this as the single routing table
// prevents a valid tool from compiling yet falling through to unavailable
// because one hand-written subset forgot its operation.
TaskActionExecutor TaskActionExecutorForOperation(
    core_service::mojom::TaskActionOperationKind operation);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_ACTION_ROUTE_H_
