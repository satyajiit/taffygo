// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_recovery_policy.h"

#include <algorithm>

#include "base/check.h"

namespace taffy {

CoreServiceRecoveryPolicy::CoreServiceRecoveryPolicy() = default;
CoreServiceRecoveryPolicy::~CoreServiceRecoveryPolicy() = default;

CoreServiceRecoveryPolicy::Decision
CoreServiceRecoveryPolicy::RecordUnexpectedDisconnect(base::TimeTicks now) {
  Prune(now);
  unexpected_disconnects_.push_back(now);

  const size_t ordinal = unexpected_disconnects_.size();
  if (ordinal >= kCircuitThreshold) {
    circuit_open_ = true;
  }

  return Decision{
      .schedule_automatic_restart = !circuit_open_,
      .delay = DelayForOrdinal(ordinal),
  };
}

void CoreServiceRecoveryPolicy::RetryExplicitly() {
  if (terminal_open_) {
    return;
  }
  unexpected_disconnects_.clear();
  circuit_open_ = false;
}

void CoreServiceRecoveryPolicy::OpenCircuitForSession() {
  terminal_open_ = true;
  circuit_open_ = true;
}

size_t CoreServiceRecoveryPolicy::unexpected_disconnect_count_for_testing(
    base::TimeTicks now) {
  Prune(now);
  return unexpected_disconnects_.size();
}

void CoreServiceRecoveryPolicy::Prune(base::TimeTicks now) {
  while (!unexpected_disconnects_.empty() &&
         now - unexpected_disconnects_.front() > kRestartWindow) {
    unexpected_disconnects_.pop_front();
  }
  if (!terminal_open_ && unexpected_disconnects_.size() < kCircuitThreshold) {
    circuit_open_ = false;
  }
}

base::TimeDelta CoreServiceRecoveryPolicy::DelayForOrdinal(size_t ordinal) {
  CHECK_GT(ordinal, 0u);
  if (ordinal == 1u) {
    return base::Milliseconds(250);
  }
  if (ordinal == 2u) {
    return base::Seconds(1);
  }
  return base::Seconds(4);
}

}  // namespace taffy
