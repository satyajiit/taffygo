// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_MEMORY_VALIDATION_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_MEMORY_VALIDATION_H_

#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// Validates one complete Memory record against its exact predecessor revision.
bool IsValidMemoryRecord(const core_service::mojom::MemoryRecord& record,
                         uint64_t expected_record_revision);

// Validates the closed typed body for either Memory storage operation.
bool IsValidMemoryStorageCommitBody(
    const core_service::mojom::StorageCommitEffect& body);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_MEMORY_VALIDATION_H_
