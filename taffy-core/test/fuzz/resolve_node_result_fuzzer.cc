// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// Node resolution is the browser re-reading the small set of scalars it needs
// before it allows an action. It is deliberately the narrowest renderer reply
// the browser walks, and narrow is not the same as safe: every field here feeds
// a precondition check.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::ResolveNodeResultPtr result;
  if (!taffy::mojom::ResolveNodeResult::Deserialize(data, size, &result) ||
      !result) {
    return 0;
  }
  taffy::FromMojom(result->code);
  if (result->node) {
    const taffy::ResolvedNodeFacts facts = taffy::FromMojom(*result->node);
    // Both predicates walk the decoded lists, so they are part of the surface.
    facts.HasState(taffy::NodeState::kVisible);
    facts.SupportsAction(taffy::ActionType::kActivate);
  }
  return 0;
}
