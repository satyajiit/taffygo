// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_STORAGE_EFFECT_VALIDATION_H_
#define TAFFY_BROWSER_CORE_STORAGE_EFFECT_VALIDATION_H_

#include <cstddef>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// Validates the exact typed body of one storage effect. This is only shape
// validation; the profile database owns compare-and-set and durability.
bool IsValidCoreStorageCommit(
    const core_service::mojom::StorageCommitEffect& body,
    size_t max_identifier_bytes,
    size_t max_effect_bytes);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_STORAGE_EFFECT_VALIDATION_H_
