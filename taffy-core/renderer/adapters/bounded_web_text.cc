// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/bounded_web_text.h"

#include <algorithm>
#include <limits>

#include "taffy/renderer/adapters/adapter.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_element.h"

namespace taffy {

BoundedWebText BoundWebText(const blink::WebString& value,
                            BudgetLedger& ledger,
                            size_t own_ceiling) {
  BoundedWebText result;
  if (value.IsNull() || value.IsEmpty()) {
    return result;
  }

  const size_t remaining = ledger.RemainingTextBytes(own_ceiling);
  if (remaining == 0) {
    // A UTF-8 byte sequence is never shorter than the UTF-16 code-unit count.
    // That gives an honest lower bound without allocating the sequence.
    ledger.NoteOmittedTextBytes(value.length(), /*could_change_answer=*/true);
    result.truncated = true;
    return result;
  }

  const size_t converted_units = std::min(value.length(), remaining + 1u);
  const std::string prefix = value.Substring(0, converted_units).Utf8();
  result.text = ledger.BoundTextTo(prefix, own_ceiling);
  result.truncated =
      result.text.size() < prefix.size() || converted_units < value.length();

  if (result.truncated) {
    ledger.NoteOmittedTextBytes(0, /*could_change_answer=*/true);
  }
  if (converted_units < value.length()) {
    ledger.NoteOmittedTextBytes(value.length() - converted_units,
                                /*could_change_answer=*/true);
  }
  return result;
}

BoundedWebText BoundWebTextLazily(base::FunctionRef<blink::WebString()> read,
                                  BudgetLedger& ledger,
                                  size_t own_ceiling) {
  if (ledger.RemainingTextBytes(own_ceiling) == 0) {
    // The source length is deliberately not read just to improve this lower
    // bound. Zero means "an unknown non-empty tail may remain" throughout the
    // truncation report and keeps the expensive read out of this branch.
    ledger.NoteOmittedTextBytes(0, /*could_change_answer=*/true);
    BoundedWebText result;
    result.truncated = true;
    return result;
  }
  return BoundWebText(read(), ledger, own_ceiling);
}

BoundedWebText BoundElementTextContent(const blink::WebElement& element,
                                       BudgetLedger& ledger,
                                       size_t own_ceiling) {
  return BoundWebTextLazily(
      [&element, &ledger, own_ceiling]() {
        const size_t remaining = ledger.RemainingTextBytes(own_ceiling);
        const size_t requested = remaining < std::numeric_limits<size_t>::max()
                                     ? remaining + 1u
                                     : remaining;
        const unsigned int abridged_units = static_cast<unsigned int>(std::min(
            requested,
            static_cast<size_t>(std::numeric_limits<unsigned int>::max())));
        return element.TextContentAbridged(abridged_units);
      },
      ledger, own_ceiling);
}

}  // namespace taffy
