// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_DIGEST_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_DIGEST_H_

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"

// The canonical digest of an authorized action (protocol section 11.3).
//
// The dispatcher recomputes the digest from the envelope it is about to act on
// and compares it with the digest the policy engine signed off. An envelope
// edited after authorization — a different node, a widened origin set, a longer
// deadline — hashes differently and is refused before a capability is consumed.
//
// Two properties make that check worth anything, and both live in the encoding
// rather than in the comparison:
//
//   * Every field is length prefixed. Without the prefix, moving a character
//     between two adjacent strings would leave the digest unchanged, and an
//     attacker who could do that could move a character between a node
//     identifier and an origin.
//   * The capability's own fields are excluded on purpose. The digest answers
//     "is this the action that was authorized". Including the capability would
//     make it answer "is this the capability that was issued", which the ledger
//     already answers by identity.
//
// Split out of action_dispatcher.cc because it is a self-contained pure
// function with a name, and because a reviewer checking the encoding should not
// have to read the dispatch path to do it.

namespace taffy {

ContentDigest ComputeActionDigest(const AuthorizedActionEnvelope& envelope);
ContentDigest ComputeBrowserCommandDigest(
    const AuthorizedBrowserCommand& command);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_DIGEST_H_
