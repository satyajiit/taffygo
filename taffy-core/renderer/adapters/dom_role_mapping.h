// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_DOM_ROLE_MAPPING_H_
#define TAFFY_RENDERER_ADAPTERS_DOM_ROLE_MAPPING_H_

// What an HTML element *is*, according to the document and nothing else.
//
// Its own translation unit because it is the adapter's one judgement call and
// it has to be reviewable on its own: only mappings a source states directly,
// nothing inferred from styling, position, or class names. A tag with no entry
// becomes an unknown role rather than a guess, which is what keeps per-field
// evidence meaningful when the accessibility adapter disagrees.
//
// The walk that uses these answers is in dom_adapter.cc.

#include <string>

#include "taffy/renderer/semantic_graph.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/platform/web_string.h"

namespace taffy::dom_role_mapping {

// A WebString as UTF-8, with null treated as empty.
std::string Utf8(const blink::WebString& value);

// The element's tag name, lowercased.
std::string LowerTagName(const blink::WebElement& element);

// One authored attribute, or empty when absent.
std::string Attribute(const blink::WebElement& element, const char* name);

// The role the tag states. Never inferred from anything but the tag and the
// attributes the tag's own semantics depend on.
SemanticRole RoleForTag(const std::string& tag, const blink::WebElement& el);

// Whether a role's own text is its content, rather than the concatenation of
// its descendants. A container's text would ship the page twice.
bool IsTextBearing(SemanticRole role);

}  // namespace taffy::dom_role_mapping

#endif  // TAFFY_RENDERER_ADAPTERS_DOM_ROLE_MAPPING_H_
