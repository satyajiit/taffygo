// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_EFFECT_ACTION_H_
#define TAFFY_BROWSER_CORE_TASK_EFFECT_ACTION_H_

#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Validates the one typed browser-action body after the outer effect envelope
// has proved its operation and deadline. Kept apart from effect-family shape
// validation so adding one browser tool cannot enlarge that outer dispatcher.
bool IsValidTaskActionEffect(
    const core_service::mojom::TaskActionEffect& action,
    const core_service::mojom::TaskEffectBinding& binding,
    uint64_t now_monotonic_ms);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_EFFECT_ACTION_H_
