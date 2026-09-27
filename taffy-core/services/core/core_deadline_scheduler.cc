// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/core_deadline_scheduler.h"

#include <stdint.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/numerics/safe_conversions.h"
#include "base/time/time.h"

namespace taffy {

CoreDeadlineScheduler::CoreDeadlineScheduler() = default;
CoreDeadlineScheduler::~CoreDeadlineScheduler() = default;

void CoreDeadlineScheduler::Replace(uint64_t deadline_monotonic_ms,
                                    uint64_t now_monotonic_ms,
                                    base::OnceClosure on_expired) {
  if (deadline_monotonic_ms == 0u) {
    Cancel();
    return;
  }
  scheduled_deadline_monotonic_ms_ = deadline_monotonic_ms;
  const uint64_t remaining_ms = deadline_monotonic_ms > now_monotonic_ms
                                    ? deadline_monotonic_ms - now_monotonic_ms
                                    : 0u;
  timer_.Start(
      FROM_HERE,
      base::Milliseconds(base::saturated_cast<int64_t>(remaining_ms)),
      base::BindOnce(&CoreDeadlineScheduler::OnExpired,
                     base::Unretained(this), std::move(on_expired)));
}

void CoreDeadlineScheduler::Cancel() {
  timer_.Stop();
  scheduled_deadline_monotonic_ms_ = 0u;
}

bool CoreDeadlineScheduler::IsScheduled() const {
  return timer_.IsRunning();
}

uint64_t CoreDeadlineScheduler::scheduled_deadline_monotonic_ms() const {
  return scheduled_deadline_monotonic_ms_;
}

void CoreDeadlineScheduler::OnExpired(base::OnceClosure callback) {
  scheduled_deadline_monotonic_ms_ = 0u;
  std::move(callback).Run();
}

}  // namespace taffy
