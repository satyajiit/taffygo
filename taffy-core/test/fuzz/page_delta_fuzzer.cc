// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "base/check.h"
#include "base/check_op.h"

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_identity.h"

// A delta is applied against a projection the subscriber already holds, so a
// hostile one attacks the matching rule rather than the parser: an epoch that
// nearly matches, a sequence that is one out, a revision that goes backwards.
//
// The body runs the contract's own applicability decision over whatever the
// message claims, against a fixed cursor. The property is total: for every
// input, the decision returns, and it never returns "applicable" for a delta
// whose epoch differs from the cursor's.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::PageDeltaPtr delta;
  if (!taffy::mojom::PageDelta::Deserialize(data, size, &delta) || !delta) {
    return 0;
  }

  taffy::DeltaProjectionCursor cursor;
  cursor.page_epoch = taffy::PageEpoch{"fuzz-epoch"};
  cursor.revision = 1;
  cursor.event_sequence = 1;

  const taffy::DeltaRejectReason verdict = taffy::DeltaApplicability(
      cursor, taffy::PageEpoch{delta->page_epoch}, delta->from_revision,
      delta->to_revision, delta->event_sequence);

  // An epoch that is not the cursor's can never be applicable, whatever else
  // the message says. Asserted here rather than in a unit test because this is
  // where the inputs come from an adversary.
  if (delta->page_epoch != cursor.page_epoch.value) {
    CHECK_NE(taffy::DeltaRejectReason::kNone, verdict);
  }
  return 0;
}
