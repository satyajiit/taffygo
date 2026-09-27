// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_RECOVERY_POLICY_H_
#define TAFFY_BROWSER_CORE_SERVICE_RECOVERY_POLICY_H_

#include <stddef.h>

#include "base/containers/circular_deque.h"
#include "base/time/time.h"

namespace taffy {

// Pure restart accounting for one profile's isolated core service.
//
// Only unexpected disconnects enter this object. A clean profile shutdown or
// deliberate idle teardown is classified by CoreServiceManager before it gets
// here, so those events cannot consume the restart budget accidentally.
class CoreServiceRecoveryPolicy {
 public:
  struct Decision {
    // False means the profile circuit is open. Only RetryExplicitly() or a new
    // profile service lifetime may launch another process.
    bool schedule_automatic_restart = false;

    // The delay for this restart ordinal. On the third disconnect this is the
    // four-second cool-down an explicit retry observes; automatic restart is
    // disabled because the circuit has opened.
    base::TimeDelta delay;
  };

  CoreServiceRecoveryPolicy();
  CoreServiceRecoveryPolicy(const CoreServiceRecoveryPolicy&) = delete;
  CoreServiceRecoveryPolicy& operator=(const CoreServiceRecoveryPolicy&) =
      delete;
  ~CoreServiceRecoveryPolicy();

  // Records one crash or otherwise unexpected loss of the primordial Mojo
  // pipe. Three such events in the rolling window open the circuit.
  Decision RecordUnexpectedDisconnect(base::TimeTicks now);

  // Explicit user intent is the only in-session operation that closes an open
  // circuit. It starts a fresh audit window; it does not disguise the earlier
  // disconnects as a clean teardown.
  void RetryExplicitly();

  // Fail-closed terminal condition for generation exhaustion. Unlike crash
  // accounting this does not manufacture disconnect history.
  void OpenCircuitForSession();

  bool circuit_open() const { return circuit_open_; }
  size_t unexpected_disconnect_count_for_testing(base::TimeTicks now);

  static constexpr base::TimeDelta kRestartWindow = base::Minutes(5);
  static constexpr size_t kCircuitThreshold = 3;

 private:
  void Prune(base::TimeTicks now);
  static base::TimeDelta DelayForOrdinal(size_t ordinal);

  base::circular_deque<base::TimeTicks> unexpected_disconnects_;
  bool circuit_open_ = false;
  bool terminal_open_ = false;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_RECOVERY_POLICY_H_
