// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/monotonic_clock.h"

namespace taffy {

MonotonicMillis NowMonotonicMs() {
  const int64_t delta =
      (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds();
  return delta < 0 ? 0 : static_cast<MonotonicMillis>(delta);
}

base::TimeTicks FromMonotonicMs(MonotonicMillis value) {
  return base::TimeTicks() + base::Milliseconds(value);
}

}  // namespace taffy
