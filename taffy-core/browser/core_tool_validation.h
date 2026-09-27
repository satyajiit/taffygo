// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TOOL_VALIDATION_H_
#define TAFFY_BROWSER_CORE_TOOL_VALIDATION_H_

#include <stddef.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

bool IsValidCoreToolJob(
    const core_service::mojom::ToolJobEffect& job,
    size_t max_identifier_bytes,
    size_t max_effect_bytes);

bool IsValidCoreToolResult(
    const core_service::mojom::ToolEffectResult& result,
    const core_service::mojom::ToolJobEffect& job,
    size_t max_identifier_bytes,
    size_t max_effect_bytes);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TOOL_VALIDATION_H_
