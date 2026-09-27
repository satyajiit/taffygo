// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVATION_RESULT_BUILDER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVATION_RESULT_BUILDER_H_

#include <vector>

#include "taffy/components/intelligence/content/adapter_requirement_check.h"
#include "taffy/components/intelligence/content/frame_inclusion_policy.h"
#include "taffy/components/intelligence/content/graph_payload_encoder.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"

// Turning one renderer reply into one observation envelope
// (protocol sections 7.2 and 15).
//
// Split out of the service implementation because it is the half with rules
// rather than the half with wiring, and the rules are the part worth reading
// on its own:
//
//   * The renderer's identity echo is checked, never adopted. A reply that
//     names a different tab, frame or epoch describes a document the broker
//     did not ask about.
//   * Committed URL, origin, incognito state and the frame tree come from
//     Chromium, and they overwrite whatever the renderer echoed.
//   * Truncation names which budget was reached. "Some of this is missing" is
//     not a usable answer; "the node budget was reached and 40 nodes were
//     omitted, and that could change the answer" is (protocol section 15).
//   * The semantic graph itself is never walked here. Mojo deserialized it,
//     this code reads bounded scalars, and the graph crosses to the Rust
//     isolated core as an opaque payload.
//
// Free functions over value types plus one mojom struct, so everything except
// the encoder call is provable without a browser.

namespace taffy {

// Browser-owned facts, gathered by the caller from Chromium and from the
// broker. Everything here overrides the renderer's echo.
struct BrowserOwnedObservationFacts {
  ProfileId profile_id;
  BrowserWindowId browser_window_id;
  PageEpoch page_epoch;
  Origin origin;
  UrlMetadata committed_url_metadata;
  bool is_potentially_trustworthy = false;
  bool is_incognito = false;
  // Assembled from Chromium's frame topology, before the inclusion policy has
  // been applied to it.
  std::vector<FrameSummary> frames;
};

// Checks the parts of a reply that the browser can contradict: the identity
// echo and the counts against the clamped budget. Returns kOk when nothing is
// wrong, so that adapter and truncation handling can then decide whether the
// result is complete.
ObservationResultCode ValidateSnapshotEcho(const ObservationRequest& clamped,
                                           const PageEpoch& live_epoch,
                                           const mojom::PageSnapshot& snapshot);

// Fills `envelope` from the snapshot's scalars, the browser-owned facts, the
// frame inclusion policy and the adapter requirement check, and encodes the
// graph. `base_code` is the code the reply already deserved; the final code is
// the stricter of it and whatever the adapters and the frame policy imply.
void FillObservationEnvelope(const ObservationRequest& clamped,
                             const mojom::PageSnapshot& snapshot,
                             BrowserOwnedObservationFacts facts,
                             const FrameInclusionInputs& frame_inputs,
                             GraphPayloadEncoder* encoder,
                             ObservationResultCode base_code,
                             ObservationEnvelope* envelope);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVATION_RESULT_BUILDER_H_
