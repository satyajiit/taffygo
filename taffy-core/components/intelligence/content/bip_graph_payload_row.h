// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_GRAPH_PAYLOAD_ROW_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_GRAPH_PAYLOAD_ROW_H_

#include <string>

#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/components/intelligence/content/bip_payload_writer.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// One row of the BIP graph framing, and every decision about what that row is
// allowed to say.
//
// This is the disclosure half of the encoder, separated from the framing half
// (bip_graph_payload.cc) because they answer different questions and only one
// of them is security significant. The framing decides what order the bytes go
// in. This file decides whether a name crosses at all, whether a state that
// *is* the value crosses, and what four bits a destination is reduced to — and
// it is the one place those rules exist, so the snapshot path and the delta
// path cannot disagree about them.

namespace taffy::bip_payload {

// Writes one semantic node. Returns false when the row could not be written,
// which is a refusal rather than a short row: a truncated node decodes cleanly
// into a node that is not the one the page had.
//
// `frame_id` is the browser's identity for the document and is written in place
// of whatever the renderer stamped on the node (decision 0051). `tally`, when
// supplied, receives what the browser's own rescan removed from a name that
// crossed. `display_id` is a fresh local name for the inspector projection and
// carries nothing of the renderer's identity; it is empty on the delta path,
// which builds no projection. Every original name, description, and run is
// charged before disclosure, so withheld input cannot evade the caller's text
// or one-message byte ceiling.
//
// `text_budget_exhausted`, when supplied, is set true exactly when the refusal
// was the caller's text budget rather than a malformed row. A row this refuses
// looks the same either way — false, and no bytes — but the two mean different
// things to whoever asked: a page too large for the budget it was observed
// under, or a renderer reply that does not conform. The byte ceiling reports
// itself through `Writer::overflowed`; this reports the other half.
[[nodiscard]] bool WriteNode(Writer& out,
                             const mojom::SemanticNode& node,
                             const FrameId& frame_id,
                             RescanTally* tally,
                             size_t& remaining_text_bytes,
                             std::string display_id,
                             InspectorNodeProjection* projection,
                             bool* text_budget_exhausted = nullptr);

// Writes one edge. An edge carries identity and structure only, so there is no
// disclosure decision to make and nothing to refuse.
[[nodiscard]] bool WriteEdge(Writer& out, const mojom::SemanticEdge& edge);

// The inspector's vocabulary for a relationship. Here rather than beside the
// encoder because it is the same kind of reduction the rest of this file makes:
// a generated protocol member mapped down to the smaller set a surface is
// allowed to see.
InspectorRelationship ToInspectorRelationship(
    mojom::RelationshipKind relationship);

}  // namespace taffy::bip_payload

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_GRAPH_PAYLOAD_ROW_H_
