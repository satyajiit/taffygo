// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_DIRECT_OBSERVATION_DIGEST_H_
#define TAFFY_BROWSER_DIRECT_OBSERVATION_DIGEST_H_

#include <stdint.h>

#include <string>
#include <string_view>

#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

enum class DirectObservationDocumentScope : uint8_t {
  kCurrentDocument = 0,
};

// Every fixed operand that policy-engine authorizes for a taskless page read.
// Keeping these values in the digest input makes omissions visible in tests:
// changing any operand must change the digest the browser asks Rust to sign.
struct DirectObservationDigestOperands {
  std::string domain;
  DirectObservationDocumentScope scope =
      DirectObservationDocumentScope::kCurrentDocument;
  bool include_child_frames = false;
  uint32_t max_nodes = 0;
  uint32_t max_text_bytes = 0;
  uint32_t max_total_bytes = 0;
  uint32_t max_frames = 0;
  uint32_t deadline_ms = 0;
};

DirectObservationDigestOperands FixedDirectObservationDigestOperands();

std::string ComputeDirectObservationDigest(
    const DirectObservationContext &context, std::string_view profile_id,
    std::string_view direct_intent_id, std::string_view operation_id,
    std::string_view idempotency_key, uint64_t generation,
    const DirectObservationDigestOperands &operands);

} // namespace taffy

#endif // TAFFY_BROWSER_DIRECT_OBSERVATION_DIGEST_H_
