// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_FIELD_CHALLENGE_CAPTURE_H_
#define TAFFY_BROWSER_FIELD_CHALLENGE_CAPTURE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "taffy/common/public/bip_observation.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace taffy {

struct NormalizedFieldHighlight {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

// Browser-custodied presentation data for one structural challenge. It has no
// address, selector, page identity, response, or route toward the isolated
// core. The optional PNG exists only long enough to move into the visible
// platform request.
struct FieldChallengePresentation {
  std::vector<uint8_t> image_png;
  std::optional<NormalizedFieldHighlight> highlight;
};

// The capture's own refusal clauses, as compiled-in literals. `kOffScreen` is
// the one a caller acts on differently (decision 0215): the target was right
// and the picture is merely outside the part of the page in view, so the move
// is to scroll and ask again rather than to give up on that field. Every other
// clause is a refusal this run cannot recover from, and they are named
// individually in the log rather than here.
inline constexpr char kFieldChallengeRefusedOffScreen[] =
    "bounds-outside-viewport";

// `refused_at` is null when `presentation` has a value, and otherwise names
// the clause the run refused under — always a literal with static storage, so
// it carries nothing about the page, the person or the task.
using FieldChallengePresentationCallback = base::OnceCallback<void(
    std::optional<FieldChallengePresentation> presentation,
    const char* refused_at)>;

// Captures or locates the exact structural target that justified a challenge
// hint. The renderer supplies fresh geometry, never pixels; the trusted
// browser clips it to the visible viewport, copies the surface, and re-resolves
// the same node after the copy. A missing/stale/oversized result is nullopt.
//
// The returned closure cancels the run. Cancellation still settles the
// callback once, with nullopt and a null clause, so an owner can remove all
// pending state through one terminal path.
base::OnceClosure StartFieldChallengePresentation(
    content::BrowserContext* browser_context,
    std::string tab_id,
    std::string node_id,
    ResolvedNodeFacts expected,
    FieldChallengePresentationCallback completion);

}  // namespace taffy

#endif  // TAFFY_BROWSER_FIELD_CHALLENGE_CAPTURE_H_
