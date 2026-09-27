// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/direct_observation_digest.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

DirectObservationContext Context() {
  DirectObservationContext context;
  context.tab_id = "tab-1";
  context.frame_id = "frame-1";
  context.page_epoch = "epoch-1";
  context.origin = "https://example.test";
  return context;
}

std::string Digest(const DirectObservationDigestOperands& operands) {
  return ComputeDirectObservationDigest(Context(), "profile-1",
                                        "direct-intent-1", "operation-1",
                                        "idempotency-1", 7u, operands);
}

TEST(DirectObservationDigestTest, BindsDomainAndEveryObservationOperand) {
  const DirectObservationDigestOperands approved =
      FixedDirectObservationDigestOperands();
  EXPECT_EQ("TAFFY_DIRECT_OBSERVATION_DIGEST_V3", approved.domain);
  const std::string expected = Digest(approved);

  DirectObservationDigestOperands changed = approved;
  // The earlier projection did not request destination metadata. Its digest
  // must never identify the expanded direct read, even with identical limits.
  changed.domain = "TAFFY_DIRECT_OBSERVATION_DIGEST_V2";
  EXPECT_NE(expected, Digest(changed));
  changed = approved;
  changed.scope = static_cast<DirectObservationDocumentScope>(1u);
  EXPECT_NE(expected, Digest(changed));
  changed = approved;
  changed.include_child_frames = true;
  EXPECT_NE(expected, Digest(changed));
  changed = approved;
  ++changed.max_nodes;
  EXPECT_NE(expected, Digest(changed));
  changed = approved;
  ++changed.max_text_bytes;
  EXPECT_NE(expected, Digest(changed));
  changed = approved;
  ++changed.max_total_bytes;
  EXPECT_NE(expected, Digest(changed));
  changed = approved;
  ++changed.max_frames;
  EXPECT_NE(expected, Digest(changed));
  changed = approved;
  ++changed.deadline_ms;
  EXPECT_NE(expected, Digest(changed));
}

}  // namespace
}  // namespace taffy
