// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_VOCABULARY_H_
#define TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_VOCABULARY_H_

// Which structured-data properties this product will report, and what each of
// them means.
//
// An allowlist, not a filter: a property that is not named here does not reach
// the graph, so a page cannot publish arbitrary keys into a model's context by
// inventing schema terms. That makes this file the one a reviewer reads to
// know what a page can say, which is why it is not buried in the walk that
// uses it.

#include <string>
#include <string_view>
#include <vector>

#include "taffy/renderer/semantic_graph.h"
#include "third_party/blink/public/platform/web_string.h"

namespace taffy::structured_data_vocabulary {

// Whether the property is one this adapter reports at all.
bool IsAllowedProperty(std::string_view property);

// The role an allowed property carries into the graph.
SemanticRole RoleForProperty(std::string_view property);

// A WebString as UTF-8, with null treated as empty.
std::string Utf8(const blink::WebString& value);

// A comparison form, used ONLY to decide whether the page shows the same
// thing. The emitted value is always the authored one: normalising what gets
// emitted would be fabricating a value to satisfy a schema, which protocol
// section 7.6 forbids outright. Normalising what gets COMPARED is a different
// question, and without it every currency symbol and every schema.org URL
// would read as a disagreement.
std::string ComparisonForm(std::string_view value);

// The words a page uses when it labels this property in its own visible text,
// already in comparison form.
//
// Deliberately short, and empty for most properties. It exists for one
// question - "does this part of the page claim to be talking about this
// property?" - and the answer has to be conservative, because a wrong yes
// turns an unrelated number somewhere on the page into a reported
// disagreement. A property with no conventional page label gets no labels
// here and simply never takes that path.
std::vector<std::string_view> PageLabelsForProperty(std::string_view property);

// Whether a comparison form is a bare measured quantity: one run of digits
// with at most one decimal point, and nothing else.
//
// This is the only value shape for which "different" can be asserted without
// interpreting the page. "in stock" and "ships today" may be two renderings of
// one fact; 119.00 and 129.00 cannot be. Everything that is not a bare
// quantity is left as uncorroborated rather than called a contradiction.
bool IsMeasuredQuantity(std::string_view comparison_form);

}  // namespace taffy::structured_data_vocabulary

#endif  // TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_VOCABULARY_H_
