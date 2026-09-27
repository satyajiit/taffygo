// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>

#include "taffy/contracts/tool-runtime/generated/cpp/tool_runtime_enums.h"
#include "testing/gtest/include/gtest/gtest.h"

// The Tool Runtime contract's generated C++ decoders. No production C++ path
// decodes a Tool Runtime wire integer yet — the browser supervisor builds a
// ToolJob rather than reading one, and no worker is implemented — so without
// this file the generator's C++ output for this contract would be committed,
// checked byte-for-byte against the generator, and never handed to a compiler.
// That is the state the other three languages were found in, and it is the
// state the generator exists to end.
//
// So these cases do two jobs. They compile the header, which makes the
// //taffy/contracts/tool-runtime:tool_runtime_enums target something a binary
// builds rather than a name in the graph; and they hold the property the
// decoder exists for, which is that an unknown value is refused rather than
// cast into an enumerator no switch matches.
namespace taffy {
namespace {

namespace mojom = tool_runtime::mojom;
namespace wire = tool_runtime::wire;

TEST(ToolRuntimeEnumDecoderTest, EveryDeclaredRuntimeKindDecodesToItself) {
  EXPECT_EQ(wire::ToolRuntimeKindFromWire(0u), mojom::ToolRuntimeKind::kPython);
  EXPECT_EQ(wire::ToolRuntimeKindFromWire(1u),
            mojom::ToolRuntimeKind::kLocalModel);
  EXPECT_EQ(wire::ToolRuntimeKindFromWire(2u), mojom::ToolRuntimeKind::kMedia);
  EXPECT_EQ(wire::ToolRuntimeKindFromWire(3u), mojom::ToolRuntimeKind::kWasm);
}

TEST(ToolRuntimeEnumDecoderTest, OnePastTheLastMemberIsRefused) {
  // One past the last declared member, which is what a peer built against a
  // later contract minor would send. Appending a member turns this case red
  // rather than quietly turning a refusal into an acceptance.
  EXPECT_EQ(wire::ToolRuntimeKindFromWire(4u), std::nullopt);
  EXPECT_EQ(wire::ToolOperationFromWire(0xFFFFFFFFu), std::nullopt);
  EXPECT_EQ(wire::ToolTerminalStatusFromWire(0xFFFFFFFFu), std::nullopt);
  EXPECT_EQ(wire::ToolAdmissionStatusFromWire(0xFFFFFFFFu), std::nullopt);
}

TEST(ToolRuntimeEnumDecoderTest, TheDecoderIsUsableInAConstantExpression) {
  // constexpr is not decoration here: a decoder that cannot be evaluated at
  // compile time cannot be used to build a lookup table or a static_assert,
  // and the generator promises one that can.
  static_assert(wire::ToolRuntimeKindFromWire(0u) ==
                mojom::ToolRuntimeKind::kPython);
  static_assert(wire::ToolRuntimeKindFromWire(9u) == std::nullopt);
  SUCCEED();
}

}  // namespace
}  // namespace taffy
