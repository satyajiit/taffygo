// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_LIBRARY_VALIDATION_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_LIBRARY_VALIDATION_H_

#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// Validates one complete Library entry against its exact predecessor revision.
bool IsValidLibraryEntry(const core_service::mojom::LibraryEntryRecord& entry,
                         uint64_t expected_entry_revision);

// Validates the closed typed body for either Library storage operation. The
// effect envelope and database compare-and-set remain their owning layers.
bool IsValidLibraryStorageCommitBody(
    const core_service::mojom::StorageCommitEffect& body);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_LIBRARY_VALIDATION_H_
