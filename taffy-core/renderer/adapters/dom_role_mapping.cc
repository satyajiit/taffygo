// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/dom_role_mapping.h"

#include "base/strings/string_util.h"

namespace taffy::dom_role_mapping {

std::string Utf8(const blink::WebString& value) {
  return value.IsNull() ? std::string() : value.Utf8();
}

std::string LowerTagName(const blink::WebElement& element) {
  return base::ToLowerASCII(Utf8(element.TagName()));
}

std::string Attribute(const blink::WebElement& element, const char* name) {
  return Utf8(element.GetAttribute(blink::WebString::FromUtf8(name)));
}

// Tag to role. Only mappings a source states directly; nothing is inferred
// from styling, position, or class names. A tag with no entry becomes an
// unknown role rather than a guess, which is what keeps per-field evidence
// meaningful when the accessibility adapter disagrees.
SemanticRole RoleForTag(const std::string& tag, const blink::WebElement& el) {
  if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" ||
      tag == "h5" || tag == "h6") {
    return SemanticRole::kHeading;
  }
  if (tag == "p") {
    return SemanticRole::kParagraph;
  }
  if (tag == "ul" || tag == "ol") {
    return SemanticRole::kList;
  }
  if (tag == "li") {
    return SemanticRole::kListItem;
  }
  if (tag == "table") {
    return SemanticRole::kTable;
  }
  if (tag == "tr") {
    return SemanticRole::kTableRow;
  }
  if (tag == "td" || tag == "th") {
    return SemanticRole::kTableCell;
  }
  if (tag == "a") {
    // An anchor without href is not a link. It is a piece of content that
    // happens to use the anchor element, and calling it a link would offer an
    // ACTIVATE action for something with no destination.
    //
    // Confirmed at the 152 pin: WebElement::HasHTMLAttribute() is gone and
    // WebElement::HasAttribute() is the surviving spelling, routing to
    // Element::hasAttribute() which lowercases the name for an HTML element in
    // an HTML document - identical for a lowercase, unnamespaced "href".
    return el.HasAttribute(blink::WebString::FromUtf8("href"))
               ? SemanticRole::kLink
               : SemanticRole::kUnknownContent;
  }
  if (tag == "button") {
    return SemanticRole::kButton;
  }
  if (tag == "img") {
    return SemanticRole::kImage;
  }
  if (tag == "video" || tag == "audio") {
    return SemanticRole::kMedia;
  }
  if (tag == "main" || tag == "section" || tag == "article" ||
      tag == "nav" || tag == "aside" || tag == "header" || tag == "footer") {
    return SemanticRole::kRegion;
  }
  return SemanticRole::kUnknownContent;
}

bool IsTextBearing(SemanticRole role) {
  switch (role) {
    case SemanticRole::kHeading:
    case SemanticRole::kParagraph:
    case SemanticRole::kListItem:
    case SemanticRole::kTableCell:
    case SemanticRole::kLink:
    case SemanticRole::kButton:
      return true;
    default:
      // A container's TextContent() is the concatenation of every descendant,
      // so emitting it would ship the page twice and make byte budgets
      // meaningless.
      return false;
  }
}

}  // namespace taffy::dom_role_mapping
