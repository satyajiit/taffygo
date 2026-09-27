// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/account_session_expiry.h"

namespace taffy {

std::optional<uint64_t>
RestampAccountSessionExpiry(base::Time expires_at_utc, base::Time now_utc,
                            base::TimeTicks now_monotonic) {
  if (expires_at_utc.is_null() || expires_at_utc.is_inf() ||
      now_utc.is_null() || now_utc.is_inf() ||
      now_utc < base::Time::UnixEpoch() || now_monotonic.is_null() ||
      now_monotonic.is_inf() || expires_at_utc <= now_utc) {
    return std::nullopt;
  }

  const base::TimeDelta remaining = expires_at_utc - now_utc;
  if (!remaining.is_positive() || remaining.is_inf() ||
      remaining > base::TimeTicks::Max() - now_monotonic) {
    return std::nullopt;
  }
  const base::TimeTicks deadline = now_monotonic + remaining;
  if (deadline.is_null() || deadline.is_inf()) {
    return std::nullopt;
  }
  const int64_t deadline_millis = deadline.since_origin().InMilliseconds();
  if (deadline_millis <= 0) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(deadline_millis);
}

std::optional<base::Time>
PersistAccountSessionExpiry(uint64_t expires_at_monotonic_ms,
                            base::Time now_utc, base::TimeTicks now_monotonic) {
  if (expires_at_monotonic_ms == 0u || now_utc.is_null() || now_utc.is_inf() ||
      now_utc < base::Time::UnixEpoch() || now_monotonic.is_null() ||
      now_monotonic.is_inf()) {
    return std::nullopt;
  }
  const int64_t now_monotonic_ms =
      now_monotonic.since_origin().InMilliseconds();
  if (now_monotonic_ms <= 0 ||
      expires_at_monotonic_ms <= static_cast<uint64_t>(now_monotonic_ms)) {
    return std::nullopt;
  }
  const uint64_t remaining_ms =
      expires_at_monotonic_ms - static_cast<uint64_t>(now_monotonic_ms);
  if (remaining_ms >
      static_cast<uint64_t>(base::TimeDelta::Max().InMilliseconds())) {
    return std::nullopt;
  }
  const base::TimeDelta remaining =
      base::Milliseconds(static_cast<int64_t>(remaining_ms));
  if (!remaining.is_positive() || remaining.is_inf() ||
      remaining > base::Time::Max() - now_utc) {
    return std::nullopt;
  }
  const base::Time expiry = now_utc + remaining;
  if (expiry.is_null() || expiry.is_inf() || expiry <= now_utc) {
    return std::nullopt;
  }
  return expiry;
}

} // namespace taffy
