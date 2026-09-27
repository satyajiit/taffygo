// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_EFFECT_H_
#define TAFFY_BROWSER_CORE_TASK_EFFECT_H_

#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Validates one exact tagged reducer effect before the browser selects its
// owning executor. A valid tag has exactly one matching body; extra bodies are
// rejected rather than ignored.
bool IsStructurallyValidTaskEffectBinding(
    const core_service::mojom::TaskEffectBinding& effect,
    uint64_t service_generation,
    uint64_t task_revision,
    uint64_t now_monotonic_ms);

// Builds the one correlated browser terminal. The operation, effect, task and
// kind are copied from the binding; this helper never fabricates success.
core_service::mojom::TaskEffectCompletionPtr MakeTaskEffectCompletion(
    const core_service::mojom::TaskEffectBinding* effect,
    core_service::mojom::TaskEffectCompletionStatus status);

// The same terminal, refused, carrying the one code that says why.
//
// A bare refusal is not neutral: the bridge reads a refused effect that named
// no code as `DeniedByPolicy`, whose recovery is `DoNotRetry` — "this will be
// decided the same way again". So a document that had simply moved under a
// gate came back to the model as a standing prohibition, and an errand that
// could have looked again ended in a hand-back instead (decision 0207). Every
// gate that refuses a dispatched action states its code here.
core_service::mojom::TaskEffectCompletionPtr MakeRefusedTaskActionCompletion(
    const core_service::mojom::TaskEffectBinding& effect,
    core_service::mojom::TaskActionResultCode code);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_EFFECT_H_
