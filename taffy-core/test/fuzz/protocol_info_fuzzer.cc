// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// Negotiation is the first message the browser accepts from an endpoint, so it
// is the first place a compromised one can lie. The limits it reports are the
// interesting part: they are numbers the browser reads and then clamps, and a
// clamp that overflowed on an absurd input would raise the ceiling rather than
// lower it.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::ProtocolInfoPtr info;
  if (!taffy::mojom::ProtocolInfo::Deserialize(data, size, &info) || !info) {
    return 0;
  }
  if (info->limits) {
    taffy::FromMojom(*info->limits);
  }
  for (taffy::mojom::AdapterKind adapter : info->supported_adapters) {
    taffy::FromMojom(adapter);
  }
  for (taffy::mojom::ObservationScope scope : info->supported_scopes) {
    taffy::FromMojom(scope);
  }
  for (taffy::mojom::ActionType type : info->supported_action_types) {
    taffy::FromMojom(type);
  }
  return 0;
}
