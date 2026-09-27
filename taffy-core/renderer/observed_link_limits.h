// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_OBSERVED_LINK_LIMITS_H_
#define TAFFY_RENDERER_OBSERVED_LINK_LIMITS_H_

#include <cstddef>

namespace taffy::renderer {

// BIP snapshot.schema.json owns the matching Destination URL maxLength. This
// renderer-local spelling keeps the untrusted renderer from depending on the
// browser-side common structs while bounding conversion and IPC allocation.
inline constexpr size_t kMaxObservedLinkDestinationBytes = 4096u;

}  // namespace taffy::renderer

#endif  // TAFFY_RENDERER_OBSERVED_LINK_LIMITS_H_
