// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_SERVICE_BRIDGE_CXX_H_
#define TAFFY_SERVICES_CORE_SERVICE_BRIDGE_CXX_H_

#include <stdint.h>

#include "third_party/rust/cxx/v1/cxx.h"

namespace taffy::core_bridge {

// Narrow stateless digest primitive injected into the Rust service runtime.
// The bridge header deliberately contains no Mojo or browser-process type.
rust::Vec<uint8_t> ChromiumSha256(rust::Slice<const uint8_t> input);

// Writes one diagnostic line of a task's walk to the log, tagged
// `[taffy_trace]`. The bridge composes every line from compiled-in names and
// counts only; a line never carries page, prompt, reply or address text.
void TaskTrace(rust::Str line);

}  // namespace taffy::core_bridge

#endif  // TAFFY_SERVICES_CORE_SERVICE_BRIDGE_CXX_H_
