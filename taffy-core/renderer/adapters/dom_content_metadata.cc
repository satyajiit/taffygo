// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/dom_content_metadata.h"

#include <string>

#include "base/strings/string_util.h"
#include "taffy/renderer/adapters/dom_role_mapping.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_element.h"

namespace taffy::dom_content_metadata {

bool IsHiddenByStyle(blink::WebElement element) {
  if (element.HasAttribute(blink::WebString::FromUtf8("hidden"))) {
    return true;
  }
  const auto computed = [&element](const char* property) {
    return base::ToLowerASCII(dom_role_mapping::Utf8(
        element.GetComputedValue(blink::WebString::FromUtf8(property))));
  };
  const std::string display = computed("display");
  const std::string visibility = computed("visibility");
  const std::string content_visibility = computed("content-visibility");
  const std::string opacity = computed("opacity");
  return display == "none" || visibility == "hidden" ||
         visibility == "collapse" || content_visibility == "hidden" ||
         opacity == "0";
}

bool IsUserGeneratedRegion(const blink::WebElement& element) {
  const std::string marker = base::ToLowerASCII(
      dom_role_mapping::Attribute(element, "itemprop") + " " +
      dom_role_mapping::Attribute(element, "itemtype"));
  return marker.find("review") != std::string::npos ||
         marker.find("comment") != std::string::npos ||
         marker.find("userinteraction") != std::string::npos;
}

}  // namespace taffy::dom_content_metadata
