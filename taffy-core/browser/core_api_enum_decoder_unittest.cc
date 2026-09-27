// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>

#include "taffy/contracts/core-api/generated/cpp/core_api_enums.h"
#include "testing/gtest/include/gtest/gtest.h"

// The Core API contract's generated C++ decoders, for the same reason the Tool
// Runtime ones have a file of their own under
// //taffy/services/tool-runtime/supervisor: the browser is this contract's C++
// consumer, but every value it handles today arrives already typed by Mojo
// deserialization, so nothing here decodes a raw integer and the generated
// header reached no compiler. A generated file that is checked against its
// generator and never built is the defect the generator exists to end, not a
// smaller version of it.
//
// The browser is where a Core API integer will first arrive unvalidated — a
// WebUI or desktop surface sending one over a pipe this process trusts less
// than it trusts Mojo's own typing — so the decoders belong in this binary
// before that day rather than after it.
namespace taffy {
namespace {

namespace mojom = core_api::mojom;
namespace wire = core_api::wire;

TEST(CoreApiEnumDecoderTest, EveryDeclaredAvailabilityDecodesToItself) {
  EXPECT_EQ(wire::CoreAvailabilityFromWire(0u),
            mojom::CoreAvailability::kStarting);
  EXPECT_EQ(wire::CoreAvailabilityFromWire(1u),
            mojom::CoreAvailability::kReady);
  EXPECT_EQ(wire::CoreAvailabilityFromWire(2u),
            mojom::CoreAvailability::kUnavailable);
  EXPECT_EQ(wire::CoreAvailabilityFromWire(3u),
            mojom::CoreAvailability::kCircuitOpen);
}

TEST(CoreApiEnumDecoderTest, OnePastTheLastMemberIsRefused) {
  // One past the last declared member, which is what a peer built against a
  // later contract minor would send. Appending a member turns this case red
  // rather than quietly turning a refusal into an acceptance.
  EXPECT_EQ(wire::CoreAvailabilityFromWire(4u), std::nullopt);
  EXPECT_EQ(wire::TaskPhaseFromWire(0xFFFFFFFFu), std::nullopt);
  EXPECT_EQ(wire::CoreCommandKindFromWire(0xFFFFFFFFu), std::nullopt);
  EXPECT_EQ(wire::CoreFailureCodeFromWire(0xFFFFFFFFu), std::nullopt);
}

TEST(CoreApiEnumDecoderTest, TheDecoderIsUsableInAConstantExpression) {
  static_assert(wire::CoreAvailabilityFromWire(1u) ==
                mojom::CoreAvailability::kReady);
  static_assert(wire::CoreAvailabilityFromWire(9u) == std::nullopt);
  SUCCEED();
}

}  // namespace
}  // namespace taffy
