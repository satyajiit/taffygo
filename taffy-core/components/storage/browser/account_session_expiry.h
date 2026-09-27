// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_ACCOUNT_SESSION_EXPIRY_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_ACCOUNT_SESSION_EXPIRY_H_

#include <stdint.h>

#include <optional>

#include "base/time/time.h"

namespace taffy {

// Converts a durable UTC expiry into the current process's monotonic clock
// domain. Invalid, expired, or unrepresentable values carry no authority.
std::optional<uint64_t>
RestampAccountSessionExpiry(base::Time expires_at_utc, base::Time now_utc,
                            base::TimeTicks now_monotonic);

// Converts a process-local monotonic expiry into durable UTC at the physical
// writer. Invalid, expired, or unrepresentable values are never persisted.
std::optional<base::Time>
PersistAccountSessionExpiry(uint64_t expires_at_monotonic_ms,
                            base::Time now_utc, base::TimeTicks now_monotonic);

} // namespace taffy

#endif // TAFFY_COMPONENTS_STORAGE_BROWSER_ACCOUNT_SESSION_EXPIRY_H_
