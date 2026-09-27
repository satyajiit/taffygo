// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_RENDERER_CLASS_ID_TOKENS_H_
#define TAFFY_COMPONENTS_FILTERING_RENDERER_CLASS_ID_TOKENS_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_set.h"

namespace taffy::filtering {

// Splits an HTML `class` attribute on ASCII whitespace (space, tab, LF, FF,
// CR). Empty tokens are dropped.
std::vector<std::string> SplitClassAttribute(std::string_view value);

// Returns the tokens not already in `seen`, and records them. Empty tokens
// are ignored.
std::vector<std::string> TakeUnseen(base::flat_set<std::string>& seen,
                                    std::vector<std::string> tokens);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_RENDERER_CLASS_ID_TOKENS_H_
