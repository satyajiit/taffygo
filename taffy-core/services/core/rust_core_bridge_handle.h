// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_BRIDGE_HANDLE_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_BRIDGE_HANDLE_H_

#include <stdint.h>

#include <utility>

#include "base/time/time.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy {

// The owned Rust composition, defined here rather than in one .cc file because
// `RustCore`'s methods are split across several: a method that cannot see this
// definition cannot reach the runtime at all.
class RustCore::Bridge {
public:
  explicit Bridge(core_bridge::BridgeBootstrap bootstrap);
  ~Bridge();

  rust::Box<core_bridge::ServiceBridge> &runtime() { return runtime_; }

private:
  rust::Box<core_bridge::ServiceBridge> runtime_;
};

// The wall clock, read on the utility main sequence and handed across. This
// class still owns no timer: every entry point that needs UTC reads it here,
// at the call, and passes the value in.
inline uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

} // namespace taffy

#endif // TAFFY_SERVICES_CORE_RUST_CORE_BRIDGE_HANDLE_H_
