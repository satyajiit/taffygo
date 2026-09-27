// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_CORE_DEADLINE_SCHEDULER_H_
#define TAFFY_SERVICES_CORE_CORE_DEADLINE_SCHEDULER_H_

#include <stdint.h>

#include "base/functional/callback.h"
#include "base/timer/timer.h"

namespace taffy {

// Owns the browser-clock half of the core deadline seam.
//
// Rust supplies one absolute monotonic value from its authoritative pending
// table. This module turns that value into one wake-up and hides replacement,
// cancellation, overdue arithmetic, and timer lifetime from the service.
class CoreDeadlineScheduler final {
 public:
  CoreDeadlineScheduler();
  CoreDeadlineScheduler(const CoreDeadlineScheduler&) = delete;
  CoreDeadlineScheduler& operator=(const CoreDeadlineScheduler&) = delete;
  ~CoreDeadlineScheduler();

  // Replaces the current deadline. Zero means the Rust table is empty and
  // leaves no timer running.
  void Replace(uint64_t deadline_monotonic_ms,
               uint64_t now_monotonic_ms,
               base::OnceClosure on_expired);
  void Cancel();

  bool IsScheduled() const;
  uint64_t scheduled_deadline_monotonic_ms() const;

 private:
  void OnExpired(base::OnceClosure callback);

  uint64_t scheduled_deadline_monotonic_ms_ = 0u;
  base::OneShotTimer timer_;
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_CORE_CORE_DEADLINE_SCHEDULER_H_
