// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base/functional/bind.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {

// Deadline scheduling crosses one narrow seam. Rust's bounded pending table
// owns the absolute deadlines; this sequence owns the browser clock and timer.
// A table mutation requests a fresh earliest value, and zero leaves the
// process completely idle.

void CoreServiceImpl::RefreshDeadlineSchedule() {
  if (!ready_ || shutdown_started_ || !host_.is_bound()) {
    StopDeadlineSchedule();
    return;
  }
  if (deadline_read_in_flight_) {
    // Coalesce mutations without accepting a potentially stale answer. The
    // ordered Rust sequence will answer one more query after this one.
    deadline_refresh_requested_ = true;
    return;
  }
  deadline_read_in_flight_ = true;
  core_.AsyncCall(&RustCore::NextOperationDeadline)
      .Then(base::BindOnce(&CoreServiceImpl::OnDeadlineRead,
                           weak_factory_.GetWeakPtr()));
}

void CoreServiceImpl::StopDeadlineSchedule() {
  deadline_scheduler_.Cancel();
  deadline_refresh_requested_ = false;
}

void CoreServiceImpl::OnDeadlineRead(uint64_t deadline_monotonic_ms) {
  deadline_read_in_flight_ = false;
  if (!ready_ || shutdown_started_ || !host_.is_bound()) {
    StopDeadlineSchedule();
    return;
  }
  if (deadline_refresh_requested_) {
    deadline_refresh_requested_ = false;
    RefreshDeadlineSchedule();
    return;
  }
  // The active task effect's deadline is browser-owned too, and it is not in
  // Rust's table: the bridge records the effect and waits for the browser's
  // answer. The utility waits with it, so the timer covers whichever arrives
  // first. Once the utility has spoken for the effect its deadline is spent.
  if (active_task_effect_ && active_task_effect_->operation &&
      !task_effect_completion_synthesized_ && !active_source_read_revision_) {
    const uint64_t effect_deadline =
        active_task_effect_->operation->deadline_monotonic_ms;
    if (effect_deadline != 0u && (deadline_monotonic_ms == 0u ||
                                  effect_deadline < deadline_monotonic_ms)) {
      deadline_monotonic_ms = effect_deadline;
    }
  }
  deadline_scheduler_.Replace(
      SourceReadDeadline(deadline_monotonic_ms), NowMonotonicMillis(),
      base::BindOnce(&CoreServiceImpl::OnDeadlineExpired,
                     weak_factory_.GetWeakPtr()));
}

void CoreServiceImpl::OnDeadlineExpired() {
  if (!ready_ || shutdown_started_ || !host_.is_bound()) {
    StopDeadlineSchedule();
    return;
  }
  // An active effect whose deadline has arrived with no answer is completed
  // by the utility, once, with the status that says the browser cannot tell
  // — so the action settles, the walk continues, and a late answer is then
  // dropped rather than read. Left alone, the task sat forever behind an
  // answer that was never coming and nothing on any surface moved.
  const uint64_t now = NowMonotonicMillis();
  ExpireSourceReads(now);
  if (active_task_effect_ && active_task_effect_->operation &&
      !task_effect_completion_synthesized_ && !active_source_read_revision_ &&
      active_task_effect_->operation->deadline_monotonic_ms <= now) {
    SynthesizeTaskEffectCompletion(TaskEffectSynthesis::kDeadline);
  }
  // PublishBatch requests the next exact deadline after Rust removes every
  // operation that became due at this instant.
  core_.AsyncCall(&RustCore::ExpireDueOperations)
      .WithArgs(now)
      .Then(base::BindOnce(&CoreServiceImpl::PublishBatch,
                           weak_factory_.GetWeakPtr()));
}

}  // namespace taffy
