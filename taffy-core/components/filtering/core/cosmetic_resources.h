// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_CORE_COSMETIC_RESOURCES_H_
#define TAFFY_COMPONENTS_FILTERING_CORE_COSMETIC_RESOURCES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace taffy::filtering {

// What the engine answered for one document URL. `injected_script` and
// `procedural_actions` are ignored: this build applies hide selectors only.
struct CosmeticResources {
  std::vector<std::string> hide_selectors;
  std::vector<std::string> exceptions;
  bool generichide = false;
};

// Parses the JSON object `url_cosmetic_resources` emits. Nullopt when the
// buffer is not an RFC object.
std::optional<CosmeticResources> ParseUrlCosmeticResourcesJson(
    std::string_view json);

// Parses the JSON array `hidden_class_id_selectors` emits. Nullopt when the
// buffer is not an RFC array.
std::optional<std::vector<std::string>> ParseSelectorListJson(
    std::string_view json);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_CORE_COSMETIC_RESOURCES_H_
