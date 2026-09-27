// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_SNAPSHOT_BUILDER_H_
#define TAFFY_RENDERER_SNAPSHOT_BUILDER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/page_capabilities.h"
#include "taffy/renderer/semantic_graph_store.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

// Turns one SnapshotRequest into one PageSnapshot: run the adapters in
// registry order, merge what the annotators said about what the producers
// found, narrow to the requested scope, and translate.
//
// It is separate from the endpoint because it is the part with the
// interesting failure modes, and the endpoint is the part with the authority
// boundary. Neither is easy to review while it is wearing the other.
//
// Three rules the builder enforces that no single adapter can:
//
//   * ONE BUDGET. Every adapter charges the same ledger, so the total is a
//     real total rather than a per-adapter bound multiplied by seven.
//
//   * A REQUIRED ADAPTER THAT CANNOT RUN MAKES THE WHOLE REQUEST
//     UNSUPPORTED. Protocol section 15: a missing required adapter returns
//     UNSUPPORTED or INCOMPLETE and prevents a result from being labelled
//     complete. A smaller snapshot that looks whole is the failure the rule
//     exists to prevent.
//
//   * SCOPE IS APPLIED AFTER ANNOTATION, NOT BEFORE. A viewport-scoped
//     request cannot be served by an adapter that has not been told what is
//     in the viewport, and visibility is decided by the layout adapter, which
//     runs last. Narrowing earlier would mean guessing.

// The document facts that decide what can run, read out of Blink. Separate
// from PageCapabilities so that the capability logic stays host-testable and
// only this function needs a renderer.
DocumentCapabilitySignals CollectCapabilitySignals(
    blink::WebLocalFrame* frame,
    const BrowserSuppliedFacts& browser_facts);

class SnapshotBuilder {
 public:
  struct Input {
    Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    ~Input();

    raw_ptr<blink::WebLocalFrame> frame = nullptr;
    raw_ptr<SemanticGraphStore> store = nullptr;
    raw_ptr<const mojom::SnapshotRequest> request = nullptr;
    BrowserSuppliedFacts browser_facts;

    // Envelope values the endpoint owns. They are passed in rather than
    // generated here because both are per-endpoint counters and there must be
    // exactly one of each: a gap in the event sequence is what tells the
    // broker to invalidate, so a second source for it would be a second way
    // to invent one.
    std::string snapshot_id;
    uint64_t event_sequence = 0;
    // The browser's statement about this document, which the caller must
    // have. There is no default: a builder that picked one would be a
    // renderer deciding a browser-owned fact, and Build() refuses instead.
    std::optional<mojom::DocumentLifecycleState> lifecycle_state;
  };

  struct Output {
    Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&);
    Output& operator=(Output&&);
    ~Output();

    mojom::ObservationResultCode code =
        mojom::ObservationResultCode::kInternalError;
    // Present if and only if `code` is kOk, kIncomplete, or kConflicted.
    mojom::PageSnapshotPtr snapshot;
    // Empty whenever `snapshot` is present: the contract declares `warnings`
    // on the snapshot object, so a build that produced one hands its warnings
    // over with it. What is left here is the reason for a code that has no
    // snapshot to carry it, which the endpoint puts on the reply instead.
    std::vector<mojom::SnapshotWarningPtr> warnings;
    // The capability report as the run left it: what the document could
    // support, lowered by what each adapter actually managed (CAP-PI-008).
    PageCapabilities capabilities;
  };

  // `capabilities` is what CollectCapabilitySignals plus
  // PageCapabilities::ForDocument decided before anything ran. The builder
  // never raises it.
  static Output Build(const Input& input, PageCapabilities capabilities);
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_SNAPSHOT_BUILDER_H_
