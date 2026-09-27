// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_DETERMINISTIC_CLOCK_H_
#define TAFFY_TEST_SUPPORT_DETERMINISTIC_CLOCK_H_

#include <memory>

#include "base/time/time.h"
#include "taffy/common/public/bip_identity.h"

namespace base {
class ScopedMockClockOverride;
}  // namespace base

// Deterministic time for a Taffy test, and the one definition of the monotonic
// millisecond reading the protocol carries.
//
// Two separate jobs, in one class because they must not disagree.
//
// **The reading.** BIP carries no wall-clock timestamps: every time value on
// the wire is a monotonic reading in milliseconds, and the browser produces it
// as the distance from the zero TimeTicks. A test that builds an authorized
// envelope has to produce capability expiries and absolute deadlines on that
// same scale, or the capability is already expired when the ledger looks at it
// and the whole action path answers kCapabilityExpired for a reason that has
// nothing to do with what the test meant to prove. MonotonicNowMs() is that
// scale, written once.
//
// **The determinism.** Installing an instance freezes time and hands the test
// the only advance button. That is what makes a deadline, an expiry, a
// coalescing window or a verifier timeout testable rather than raced.
//
// A browser test that installs one takes on an obligation: Chromium's timers
// read the same overridden clock, so nothing that waits on a timer will fire
// until the test advances time. That is usually what a deadline test wants and
// almost never what a navigation test wants. The suites in this directory
// install it only where the property under test is a bound; everywhere else
// they use the static reading and leave the clock alone. Said plainly so that
// the next person does not discover it as a hang.

namespace taffy::test {

class DeterministicClock {
 public:
  // The distance from the zero TimeTicks in milliseconds, which is exactly how
  // //taffy/browser produces every MonotonicMillis it emits. Safe to
  // call with or without an instance installed.
  static MonotonicMillis MonotonicNowMs();

  // A reading far enough ahead of now to outlive a test step, for a capability
  // expiry or an absolute deadline. Written as a helper because "now plus a
  // number I typed" is how a suite acquires a flake that only appears on a
  // loaded builder.
  static MonotonicMillis DeadlineFromNow(base::TimeDelta ahead);

  DeterministicClock();
  DeterministicClock(const DeterministicClock&) = delete;
  DeterministicClock& operator=(const DeterministicClock&) = delete;
  ~DeterministicClock();

  // Moves every clock forward. Task runners driven by the test environment see
  // the new time on their next run.
  void Advance(base::TimeDelta delta);

 private:
  std::unique_ptr<base::ScopedMockClockOverride> override_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_DETERMINISTIC_CLOCK_H_
