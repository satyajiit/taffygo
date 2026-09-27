// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_BOUNDED_WEB_TEXT_H_
#define TAFFY_RENDERER_ADAPTERS_BOUNDED_WEB_TEXT_H_

#include <cstddef>
#include <string>

#include "base/functional/function_ref.h"

namespace blink {
class WebElement;
class WebString;
}  // namespace blink

namespace taffy {

class BudgetLedger;

// One Blink string after bounded UTF-8 conversion. `truncated` describes the
// source, not merely the returned byte string: it is true when an unconverted
// UTF-16 tail exists as well as when the byte ledger shortened the prefix.
struct BoundedWebText {
  std::string text;
  bool truncated = false;
};

// Converts at most one code unit beyond the remaining byte budget. UTF-8 uses
// at most three bytes per UTF-16 code unit (a surrogate pair is four bytes for
// two), so peak conversion memory is bounded by the budget instead of by an
// attacker-controlled DOM or selection string.
BoundedWebText BoundWebText(const blink::WebString& value,
                            BudgetLedger& ledger,
                            size_t own_ceiling);

// Defers an expensive Blink text read until the shared ledger proves that at
// least one byte may still cross. A page can make TextContent() or
// SelectionAsText() construct a very large string; invoking either after the
// output budget is exhausted spends attacker-controlled main-thread work for
// a result that is guaranteed to be discarded.
BoundedWebText BoundWebTextLazily(base::FunctionRef<blink::WebString()> read,
                                  BudgetLedger& ledger,
                                  size_t own_ceiling);

// Reads an element's descendant text through Blink's abridged API. The API
// stops the DOM walk after the requested UTF-16 prefix, so peak allocation is
// tied to the remaining byte budget instead of to the page's full subtree.
BoundedWebText BoundElementTextContent(const blink::WebElement& element,
                                       BudgetLedger& ledger,
                                       size_t own_ceiling);

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_BOUNDED_WEB_TEXT_H_
