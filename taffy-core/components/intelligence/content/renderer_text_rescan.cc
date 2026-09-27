// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/renderer_text_rescan.h"

#include "taffy/components/intelligence/content/scrubbed_text.h"
#include "taffy/components/intelligence/content/scrubbing_serializer.h"

namespace taffy {

std::string RescanRendererText(std::string_view text, RescanTally* tally) {
  if (text.empty()) {
    return std::string();
  }

  // Delegated rather than reimplemented. ScrubbingSerializer is this
  // component's one scrubber: it owns the truncation bound, the span
  // replacement, and the rule that the canary count is taken from the input
  // rather than from the accepted matches so that a canary another rule
  // covered for is still reported. A second copy of that logic here would be
  // a second thing to keep correct, and the seeded-secret suite would only be
  // proving one of them.
  const ScrubbedText scrubbed = ScrubbingSerializer::Serialize(text);
  if (tally) {
    tally->redacted_span_count += scrubbed.report().redaction_count;
    tally->canary_hit_count += scrubbed.report().canary_hit_count;
  }
  return scrubbed.value();
}

}  // namespace taffy
