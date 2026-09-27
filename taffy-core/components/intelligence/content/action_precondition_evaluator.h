// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_PRECONDITION_EVALUATOR_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_PRECONDITION_EVALUATOR_H_

#include <optional>
#include <utility>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"

namespace taffy {

using ActionPreconditionFailure =
    std::pair<PreconditionKind, ActionResultCode>;

// Evaluates every node-action precondition against the just-resolved node and
// the fresh browser-owned lease witness. The caller must first establish the
// target handle's tab/frame/document/origin liveness; operand equality here
// proves that a declared condition names that same browser-checked handle.
// Whether a non-form action may reach a line of this sensitive class. One
// pair only: a scroll into view of a challenge's answer, which the task brings
// into view so the sheet can copy the challenge's picture (decisions 0215,
// 0240 and 0244). A scroll moves the page and changes nothing on it; every
// other sensitive line is still refused to a press, a focus and a scroll.
bool NonFormActionMayReach(ActionType action, Sensitivity sensitivity);

std::optional<ActionPreconditionFailure> EvaluateBrowserActionPreconditions(
    const AuthorizedActionEnvelope& envelope,
    const ResolvedNodeFacts& facts,
    bool actor_lease_still_valid);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_PRECONDITION_EVALUATOR_H_
