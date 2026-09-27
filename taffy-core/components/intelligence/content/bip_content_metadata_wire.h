// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_CONTENT_METADATA_WIRE_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_CONTENT_METADATA_WIRE_H_

#include <stdint.h>

#include <optional>
#include <vector>

#include "taffy/components/intelligence/content/bip_payload_writer.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

namespace taffy::bip_payload {

// Normalizes absent authorship to unknown and refuses labels only trusted
// product layers may mint.
std::optional<uint8_t> RendererAuthorship(
    std::optional<mojom::ContentTrust> trust);

// The renderer serializer emits a sorted set. This browser-side boundary
// independently verifies the bound, membership, ordering, and uniqueness.
bool ContentSignalsAreCanonical(
    const std::vector<mojom::ContentSignal>& signals);

// Writes a list already accepted by ContentSignalsAreCanonical.
void WriteContentSignals(Writer& out,
                         const std::vector<mojom::ContentSignal>& signals);

}  // namespace taffy::bip_payload

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_CONTENT_METADATA_WIRE_H_
