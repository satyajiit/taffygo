// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/hide_stylesheet.h"

#include <string>
#include <string_view>
#include <vector>

namespace taffy::filtering {
namespace {

bool SelectorIsSafe(std::string_view selector) {
  return selector.find('{') == std::string_view::npos &&
         selector.find('}') == std::string_view::npos &&
         selector.find("</") == std::string_view::npos;
}

}  // namespace

std::string BuildHideStylesheet(const std::vector<std::string>& selectors) {
  std::string css;
  bool first = true;
  for (const std::string& selector : selectors) {
    if (selector.empty() || !SelectorIsSafe(selector)) {
      continue;
    }
    if (!first) {
      css.append(",\n");
    }
    first = false;
    css.append(selector);
  }
  if (css.empty()) {
    return std::string();
  }
  css.append(" { display: none !important; }");
  return css;
}

}  // namespace taffy::filtering
