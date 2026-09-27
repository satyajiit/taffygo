// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// The reply to a dispatched action, and the message a compromised renderer has
// the most to gain from. It cannot say the effect happened — the type has no
// such member — so what it can do instead is describe a node that is not the
// one that was authorized, or claim a revision that would satisfy a verifier
// looking for an advance.
//
// The body converts the outcome and the re-read node facts, which is exactly
// the subset of the reply that browser-process code inspects field by field.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::RendererActionResultPtr result;
  if (!taffy::mojom::RendererActionResult::Deserialize(data, size, &result) ||
      !result) {
    return 0;
  }
  taffy::FromMojom(result->outcome);
  if (result->observed_state) {
    taffy::FromMojom(*result->observed_state);
  }
  return 0;
}
