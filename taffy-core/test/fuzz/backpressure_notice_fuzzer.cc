// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_delta.h"

// A backpressure notice is the one message where a renderer legitimately
// reports that it lost something. That makes it the message a hostile renderer
// would use to claim a protected class was dropped without asking for the
// resnapshot that would make the loss recoverable.
//
// The body converts every dropped category and the action, which is where an
// unmapped member has to fail closed rather than be coerced to the least
// restrictive one.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::BackpressureNoticePtr notice;
  if (!taffy::mojom::BackpressureNotice::Deserialize(data, size, &notice) ||
      !notice) {
    return 0;
  }
  taffy::FromMojom(notice->action);
  for (taffy::mojom::DeltaCategory category : notice->dropped_categories) {
    taffy::FromMojom(category);
  }
  return 0;
}
