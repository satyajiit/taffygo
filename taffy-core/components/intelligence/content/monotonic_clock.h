// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_MONOTONIC_CLOCK_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_MONOTONIC_CLOCK_H_

// The one monotonic clock the browser side of //taffy stamps
// records with.
//
// Every envelope, notice, and result the protocol carries has an
// observed_at_monotonic_ms, and consumers compare them across messages that
// were produced by different classes. Two readings of "now" that round
// differently would make a delta look older than the snapshot it followed, so
// there is exactly one reader and it lives here.
//
// Not a wall clock and never rendered: the value is a process-local duration
// since an arbitrary origin, which is what makes it safe to record.

#include "base/time/time.h"
#include "taffy/common/public/bip_identity.h"

namespace taffy {

// Milliseconds since this process's monotonic origin, clamped at zero.
MonotonicMillis NowMonotonicMs();

// The same value read back as a TimeTicks, for deadline comparisons.
base::TimeTicks FromMonotonicMs(MonotonicMillis value);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_MONOTONIC_CLOCK_H_
