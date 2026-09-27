// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_SNAPSHOT_CAPABILITY_SIGNALS_H_
#define TAFFY_RENDERER_SNAPSHOT_CAPABILITY_SIGNALS_H_

// What this document can actually be observed for, and what a consumer is
// told when an adapter could not deliver.
//
// Its own translation unit because it answers a question the builder does not:
// the builder assembles a snapshot, and this decides what the snapshot is
// allowed to claim about its own completeness. An adapter that failed is named
// in a report of its own; it is never silently absent, because a consumer that
// cannot tell "no such evidence" from "the adapter did not run" will read the
// first as the second.

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
// AdapterCapability and AdapterStatus, which is what this header's one
// declaration takes. There is no renderer/extraction_context.h — an earlier
// revision of this file included that path and it has never existed;
// ExtractionContext lives in adapters/adapter.h, and this header does not need
// it.
#include "taffy/renderer/page_capabilities.h"
#include "taffy/renderer/semantic_graph.h"
#include "taffy/renderer/snapshot_builder.h"

namespace taffy::snapshot_capability_signals {

// The per-adapter provenance and health row for one adapter. Every adapter
// this endpoint runs has a wire member, so every adapter gets one of these -
// there is no second channel for an adapter the wire cannot name, because
// there is no such adapter.
mojom::AdapterReportPtr MakeReport(const AdapterCapability& capability,
                                   AdapterStatus status);

}  // namespace taffy::snapshot_capability_signals

#endif  // TAFFY_RENDERER_SNAPSHOT_CAPABILITY_SIGNALS_H_
