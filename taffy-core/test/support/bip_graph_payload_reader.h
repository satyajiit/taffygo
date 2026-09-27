// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_BIP_GRAPH_PAYLOAD_READER_H_
#define TAFFY_TEST_SUPPORT_BIP_GRAPH_PAYLOAD_READER_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

// Test-only decoder for what EncodeGraphPayload wrote, so a test can name a
// node the renderer actually produced.
//
// # Why this exists rather than a field on ObservationEnvelope
//
// The envelope deliberately carries counts and an opaque graph payload, and
// nothing else about the graph. That is the boundary decision 0004 makes:
// browser-process C++ never walks an untrusted semantic graph field by field,
// because parsing bounded-but-attacker-shaped structure is the Rule of Two
// case and belongs in Rust. So the browser cannot tell a test which node is a
// link — it does not look.
//
// A test binary is not the browser process's authority boundary. It is the one
// place that may open the envelope, and it has to be able to: without it every
// action test in the tree targets a fabricated node identifier, every one of
// them is refused because the node does not exist, and the success path of the
// dispatcher has no end-to-end coverage at all. That was the state of this
// directory before this file.
//
// It is deliberately a separate reader rather than a call into the Rust
// decoder. Two independent readers of one format is what makes a golden vector
// mean something: an encoder bug that the Rust decoder happens to mirror still
// fails here.

namespace taffy::test {

struct GraphPayloadTextRun {
  std::string text;
  uint8_t source_kind = 0;
  uint8_t sensitivity = 0;
  uint8_t flags = 0;

  // --- framing version 4 -----------------------------------------------
  uint8_t content_trust = 0;
  std::vector<uint8_t> content_signals;
};

// One node row, as the framing carries it. Text is summarised rather than
// interpreted: the reader retains the bounded runs but assigns them no page
// meaning and grants them no authority.
struct GraphPayloadNode {
  std::string node_id;
  std::string frame_id;
  uint16_t role = 0;
  uint8_t sensitivity = 0;
  bool name_withheld = false;
  // Empty when the node has no name, and also empty when it has one this
  // framing withheld. `name_withheld` is what tells those apart.
  std::string name;

  // --- framing version 2 -----------------------------------------------
  //
  // What the node can be asked to do, and what state it is in. Both are the
  // raw wire values rather than the enumerations, because this reader's job
  // is to prove what the encoder wrote, and translating on the way would let
  // a wrong value read as a right one.
  std::vector<uint16_t> actions;
  std::vector<uint8_t> states;
  // Empty here means the browser withheld them, not that the node has none.
  bool value_states_withheld = false;
  bool text_withheld = false;
  std::vector<std::string> texts;
  // The four destination bits, or zero for a node with no destination. No URL
  // is in this framing to read.
  uint8_t destination_flags = 0;

  // --- framing version 4 -----------------------------------------------
  //
  // Raw closed-enumeration ordinals. This independent reader validates the
  // renderer-safe subset and canonical signal order before returning them.
  uint8_t content_trust = 0;
  std::vector<uint8_t> content_signals;
  std::vector<GraphPayloadTextRun> text_runs;
};

struct GraphPayloadEdge {
  std::string from_node_id;
  std::string to_node_id;
  uint16_t relationship = 0;
  bool inferred = false;
};

struct GraphPayload {
  std::vector<GraphPayloadNode> nodes;
  std::vector<GraphPayloadEdge> edges;
};

// The complete bounded graph. Tests that need to bind evidence from two
// adapter identities use the typed relationship rather than guessing that
// nearby rows describe the same element.
std::optional<GraphPayload> ReadGraphPayload(const std::vector<uint8_t>& bytes);

// `std::nullopt` when the bytes are not a graph payload this build
// understands: wrong framing version, a truncated row, or trailing bytes. A
// caller must not treat that as an empty graph.
std::optional<std::vector<GraphPayloadNode>> ReadGraphPayloadNodes(
    const std::vector<uint8_t>& bytes);

// `GraphPayloadNode::states` spelled out, for a failure message. A refusal that
// turns on a required state says only that one was not asserted, and the ways
// that happens have different causes — the node was determined off-screen, or
// no determination was reached at all and nothing was asserted. The observed
// list is what tells them apart, so a test that asserts on a state should say
// what it saw when it fails. An unrecognized ordinal is printed as its number
// rather than dropped: this reader exists to report what the encoder wrote.
std::string DescribeNodeStates(const std::vector<uint8_t>& states);

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_BIP_GRAPH_PAYLOAD_READER_H_
