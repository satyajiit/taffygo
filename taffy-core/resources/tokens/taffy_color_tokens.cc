// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/resources/tokens/generated/cpp/taffy_color_tokens.h"

#include <cstddef>

namespace taffy::resources {

static_assert(static_cast<std::size_t>(ColorToken::kCount) ==
              kLightColorPalette.size());
static_assert(kLightColorPalette.size() == kDarkColorPalette.size());

}  // namespace taffy::resources
