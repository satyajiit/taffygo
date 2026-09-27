// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// The reply that opens a delta stream. Small, and worth its own target because
// the base revision it reports is what every later delta is matched against: a
// subscription that opened with a nonsense base revision would make the first
// delta unapplyable in a way that looks like a renderer bug rather than a
// hostile reply.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::SubscriptionResultPtr result;
  if (!taffy::mojom::SubscriptionResult::Deserialize(data, size, &result)) {
    return 0;
  }
  return 0;
}
