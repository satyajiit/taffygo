// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/deterministic_clock.h"

#include "base/test/scoped_mock_clock_override.h"

namespace taffy::test {

// static
MonotonicMillis DeterministicClock::MonotonicNowMs() {
  return static_cast<MonotonicMillis>(
      (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds());
}

// static
MonotonicMillis DeterministicClock::DeadlineFromNow(base::TimeDelta ahead) {
  return MonotonicNowMs() + static_cast<MonotonicMillis>(ahead.InMilliseconds());
}

DeterministicClock::DeterministicClock()
    // VERIFY AT SP-01: base::ScopedMockClockOverride overrides Time::Now,
    // TimeTicks::Now and ThreadTicks::Now for the process while it exists.
    // Upstream file to read: base/test/scoped_mock_clock_override.h. If the
    // pinned milestone splits the three, this constructor installs all of
    // them and nothing else in this directory changes.
    : override_(std::make_unique<base::ScopedMockClockOverride>()) {}

DeterministicClock::~DeterministicClock() = default;

void DeterministicClock::Advance(base::TimeDelta delta) {
  override_->Advance(delta);
}

}  // namespace taffy::test
