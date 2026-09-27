// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_STORE_ROWS_H_
#define TAFFY_BROWSER_CORE_TASK_STORE_ROWS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/time/time.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace taffy {

// One visit, bookmark or open tab as the browser holds it, before it is
// reduced to the reference a store row may carry (decision 0133 section 3).
struct TaskStoreEntry {
  std::u16string title;
  GURL url;
  base::Time when;
};

// Reduces one entry to a row: the title collapsed, trimmed, stripped of
// control characters and bounded; the host; the path with no query and no
// fragment; the time in milliseconds since the Unix epoch. Null when the
// address is not http(s) or a field cannot be bounded without changing what
// it names, in which case the entry is left out and counted as omitted.
core_service::mojom::TaskStoreRowPtr ProjectTaskStoreRow(
    const TaskStoreEntry& entry);

// The typed result of one store read: the first `limit` rows the entries
// project to, in the order given, and the count of entries left out.
core_service::mojom::TaskStoreActionResultPtr BuildTaskStoreResult(
    core_service::mojom::TaskActionOperationKind operation,
    const std::vector<TaskStoreEntry>& entries,
    uint32_t limit);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_STORE_ROWS_H_
