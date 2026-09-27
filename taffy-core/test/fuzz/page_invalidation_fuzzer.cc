// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include <optional>

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_observation.h"

// An invalidation tells a subscriber its handles are dead. A hostile one either
// says nothing died when something did, or claims something died in a document
// it has no business speaking about.
//
// The body converts the reason and checks the one derived fact every consumer
// depends on: whether the document itself is gone, as opposed to the stream
// having lost its place inside a document that is still there.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::PageInvalidationPtr invalidation;
  if (!taffy::mojom::PageInvalidation::Deserialize(data, size, &invalidation) ||
      !invalidation) {
    return 0;
  }

  const std::optional<taffy::InvalidationCode> code =
      taffy::FromMojom(invalidation->reason);
  if (!code) {
    // An unmapped reason is unsupported and fails closed. Nothing further may
    // be derived from it.
    return 0;
  }
  taffy::InvalidationRetiresDocument(*code);
  return 0;
}
