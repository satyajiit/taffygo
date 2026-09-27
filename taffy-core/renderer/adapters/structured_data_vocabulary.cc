// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/structured_data_vocabulary.h"

#include <array>
#include <vector>

#include "base/strings/string_util.h"

namespace taffy::structured_data_vocabulary {

namespace {

// The properties this adapter will report. An allowlist, not a filter: a
// property that is not named here does not reach the graph, so a page cannot
// publish arbitrary keys into a model's context by inventing schema terms.
constexpr auto kAllowedProperties = std::to_array<std::string_view>({
    "name",
    "headline",
    "description",
    "price",
    "priceCurrency",
    "availability",
    "ratingValue",
    "reviewCount",
    "author",
    "datePublished",
    "dateModified",
    "brand",
    "sku",
});


}  // namespace

bool IsAllowedProperty(std::string_view property) {
  return std::ranges::find(kAllowedProperties, property) !=
         std::ranges::end(kAllowedProperties);
}

SemanticRole RoleForProperty(std::string_view property) {
  if (property == "price" || property == "priceCurrency") {
    return SemanticRole::kPrice;
  }
  if (property == "ratingValue" || property == "reviewCount") {
    return SemanticRole::kRating;
  }
  if (property == "availability") {
    return SemanticRole::kAvailability;
  }
  if (property == "author" || property == "datePublished" ||
      property == "dateModified") {
    return SemanticRole::kCitation;
  }
  return SemanticRole::kLabelValuePair;
}

std::vector<std::string_view> PageLabelsForProperty(std::string_view property) {
  // One entry per word a page actually writes next to the value, in
  // comparison form. The schema name itself is not assumed to be one of them:
  // no page labels a figure "ratingValue".
  if (property == "price") {
    return {"price"};
  }
  if (property == "ratingValue") {
    return {"rating", "rating value"};
  }
  if (property == "reviewCount") {
    return {"reviews", "review count"};
  }
  // Every other allowed property, including "name", "sku" and "availability".
  // Their values are words rather than quantities, so a difference between
  // two renderings of them is not something this component can tell from a
  // disagreement, and it does not guess.
  return {};
}

bool IsMeasuredQuantity(std::string_view comparison_form) {
  bool seen_digit = false;
  bool seen_point = false;
  for (const char c : comparison_form) {
    if (base::IsAsciiDigit(c)) {
      seen_digit = true;
      continue;
    }
    if (c == '.' && !seen_point) {
      seen_point = true;
      continue;
    }
    // A space, a letter, a second decimal point: this is a phrase or an
    // identifier, not a quantity. "2 years", "l a 2600" and "2026 02 11" all
    // stop here, which is the point of the check.
    return false;
  }
  return seen_digit;
}

std::string Utf8(const blink::WebString& value) {
  return value.IsNull() ? std::string() : value.Utf8();
}

// A comparison form, used ONLY to decide whether the page shows the same
// thing. The emitted value is always the authored one: normalising what gets
// emitted would be fabricating a value to satisfy a schema, which protocol
// section 7.6 forbids outright. Normalising what gets COMPARED is a different
// question, and without it every currency symbol and every schema.org URL
// would read as a disagreement.
std::string ComparisonForm(std::string_view value) {
  std::string_view working = value;

  // schema.org enumerations are authored as URLs: "https://schema.org/InStock"
  // for what the page renders as "in stock". Take the last path segment and
  // split it on case boundaries.
  const size_t slash = working.rfind('/');
  if (slash != std::string_view::npos && working.find("schema.org") !=
                                             std::string_view::npos) {
    working = working.substr(slash + 1);
  }

  std::string out;
  bool previous_was_alnum = false;
  for (size_t i = 0; i < working.size(); ++i) {
    const char c = working[i];
    if (base::IsAsciiUpper(c) && previous_was_alnum && !out.empty() &&
        out.back() != ' ') {
      out.push_back(' ');
    }
    if (base::IsAsciiAlphaNumeric(c)) {
      out.push_back(base::ToLowerASCII(c));
      previous_was_alnum = true;
      continue;
    }
    if (c == '.' && previous_was_alnum) {
      // Keep the decimal point: "129.00" and "12900" are not the same price.
      out.push_back('.');
      continue;
    }
    if (!out.empty() && out.back() != ' ') {
      out.push_back(' ');
    }
    previous_was_alnum = false;
  }
  return std::string(base::TrimWhitespaceASCII(out, base::TRIM_ALL));
}

}  // namespace taffy::structured_data_vocabulary
