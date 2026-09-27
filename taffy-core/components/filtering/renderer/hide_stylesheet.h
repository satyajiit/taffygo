// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_RENDERER_HIDE_STYLESHEET_H_
#define TAFFY_COMPONENTS_FILTERING_RENDERER_HIDE_STYLESHEET_H_

#include <string>
#include <vector>

namespace taffy::filtering {

// Joins hide selectors into a user-origin stylesheet. Empty selectors are
// skipped; a selector carrying `{`, `}`, or `</` is rejected rather than
// injected.
std::string BuildHideStylesheet(const std::vector<std::string>& selectors);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_RENDERER_HIDE_STYLESHEET_H_
