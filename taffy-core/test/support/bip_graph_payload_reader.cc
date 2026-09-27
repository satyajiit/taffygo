// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/bip_graph_payload_reader.h"

#include <string>
#include <utility>

#include "base/memory/raw_ref.h"
#include "base/strings/string_number_conversions.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/components/intelligence/content/bip_graph_payload.h"

namespace taffy::test {
namespace {

// A cursor that never reads past the end and never indexes without checking.
// The same discipline the Rust decoder follows, for the same reason: these
// bytes describe a page, and a page is not a trusted source.
class Cursor {
 public:
  explicit Cursor(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

  bool U8(uint8_t* out) {
    if (at_ >= bytes_->size()) {
      return false;
    }
    *out = (*bytes_)[at_++];
    return true;
  }

  bool U16(uint16_t* out) {
    uint8_t low = 0;
    uint8_t high = 0;
    if (!U8(&low) || !U8(&high)) {
      return false;
    }
    *out = static_cast<uint16_t>(low) |
           static_cast<uint16_t>(static_cast<uint16_t>(high) << 8);
    return true;
  }

  bool U32(uint32_t* out) {
    uint16_t low = 0;
    uint16_t high = 0;
    if (!U16(&low) || !U16(&high)) {
      return false;
    }
    *out = static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16);
    return true;
  }

  bool U64(uint64_t* out) {
    uint32_t low = 0;
    uint32_t high = 0;
    if (!U32(&low) || !U32(&high)) {
      return false;
    }
    *out = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
    return true;
  }

  bool Short(std::string* out) {
    uint16_t length = 0;
    if (!U16(&length)) {
      return false;
    }
    if (bytes_->size() - at_ < length) {
      return false;
    }
    out->assign(bytes_->begin() + static_cast<ptrdiff_t>(at_),
                bytes_->begin() + static_cast<ptrdiff_t>(at_ + length));
    at_ += length;
    return true;
  }

  bool AtEnd() const { return at_ == bytes_->size(); }

 private:
  const raw_ref<const std::vector<uint8_t>> bytes_;
  size_t at_ = 0;
};

bool ReadRendererContentTrust(Cursor* cursor, uint8_t* trust) {
  if (!cursor->U8(trust)) {
    return false;
  }
  // ContentTrust wire order: FIRST_PARTY_DOCUMENT, USER_GENERATED_CONTENT,
  // THIRD_PARTY_EMBEDDED and UNKNOWN_UNTRUSTED are the only members a live
  // page is permitted to supply. USER_AUTHORED, TAFFY_AUTHORED and
  // MODEL_AUTHORED belong to browser/core-owned provenance.
  return *trust == 2u || *trust == 3u || *trust == 4u || *trust == 6u;
}

bool ReadContentSignals(Cursor* cursor, std::vector<uint8_t>* signals) {
  uint32_t count = 0;
  if (!cursor->U32(&count) || count > kMaxBipContentSignals) {
    return false;
  }
  std::optional<uint8_t> previous;
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t signal = 0;
    // Seven closed members, encoded in strictly increasing order. This turns
    // the bounded array back into the set shape the producer accumulated.
    if (!cursor->U8(&signal) || signal > 6u ||
        (previous.has_value() && signal <= *previous)) {
      return false;
    }
    signals->push_back(signal);
    previous = signal;
  }
  return true;
}

}  // namespace

std::optional<GraphPayload> ReadGraphPayload(
    const std::vector<uint8_t>& bytes) {
  Cursor cursor(bytes);

  uint8_t framing = 0;
  if (!cursor.U8(&framing) || framing != kBipGraphPayloadFraming) {
    return std::nullopt;
  }
  std::string schema_version;
  if (!cursor.Short(&schema_version)) {
    return std::nullopt;
  }

  uint32_t node_count = 0;
  if (!cursor.U32(&node_count)) {
    return std::nullopt;
  }

  GraphPayload graph;
  // Not reserved from `node_count`: a count is a claim about the bytes, and a
  // claim of four billion must cost four bytes and one failure rather than a
  // reservation.
  for (uint32_t i = 0; i < node_count; ++i) {
    GraphPayloadNode node;
    uint8_t flags = 0;
    uint8_t value_kind = 0;
    uint32_t text_run_count = 0;
    uint64_t text_bytes = 0;
    if (!cursor.Short(&node.node_id) || !cursor.Short(&node.frame_id) ||
        !cursor.U16(&node.role) || !cursor.U8(&node.sensitivity) ||
        !cursor.U8(&flags) || !cursor.U8(&value_kind) ||
        !cursor.Short(&node.name) || !cursor.U32(&text_run_count) ||
        !cursor.U64(&text_bytes)) {
      return std::nullopt;
    }
    uint32_t action_count = 0;
    if (!cursor.U32(&action_count) || action_count > kMaxBipNodeActions) {
      return std::nullopt;
    }
    for (uint32_t action = 0; action < action_count; ++action) {
      uint16_t value = 0;
      if (!cursor.U16(&value)) {
        return std::nullopt;
      }
      node.actions.push_back(value);
    }

    uint32_t state_count = 0;
    if (!cursor.U32(&state_count) || state_count > kMaxBipNodeStates) {
      return std::nullopt;
    }
    for (uint32_t state = 0; state < state_count; ++state) {
      uint8_t value = 0;
      if (!cursor.U8(&value)) {
        return std::nullopt;
      }
      node.states.push_back(value);
    }

    if (!cursor.U8(&node.destination_flags)) {
      return std::nullopt;
    }
    if (!ReadRendererContentTrust(&cursor, &node.content_trust) ||
        !ReadContentSignals(&cursor, &node.content_signals)) {
      return std::nullopt;
    }

    uint32_t emitted_runs = 0;
    if (!cursor.U32(&emitted_runs) || emitted_runs > text_run_count) {
      return std::nullopt;
    }
    for (uint32_t run = 0; run < emitted_runs; ++run) {
      GraphPayloadTextRun text_run;
      if (!cursor.Short(&text_run.text) || !cursor.U8(&text_run.source_kind) ||
          !cursor.U8(&text_run.sensitivity) || !cursor.U8(&text_run.flags) ||
          !ReadRendererContentTrust(&cursor, &text_run.content_trust) ||
          !ReadContentSignals(&cursor, &text_run.content_signals)) {
        return std::nullopt;
      }
      node.texts.push_back(text_run.text);
      node.text_runs.push_back(std::move(text_run));
    }

    node.text_withheld =
        (flags & static_cast<uint8_t>(BipNodeFlag::kTextWithheld)) != 0;
    node.name_withheld =
        (flags & static_cast<uint8_t>(BipNodeFlag::kNameWithheld)) != 0;
    node.value_states_withheld =
        (flags & static_cast<uint8_t>(BipNodeFlag::kValueStatesWithheld)) != 0;
    graph.nodes.push_back(std::move(node));
  }

  uint32_t edge_count = 0;
  if (!cursor.U32(&edge_count)) {
    return std::nullopt;
  }
  for (uint32_t i = 0; i < edge_count; ++i) {
    GraphPayloadEdge edge;
    uint8_t inferred = 0;
    if (!cursor.Short(&edge.from_node_id) || !cursor.Short(&edge.to_node_id) ||
        !cursor.U16(&edge.relationship) || edge.relationship > 8u ||
        !cursor.U8(&inferred) || inferred > 1u) {
      return std::nullopt;
    }
    edge.inferred = inferred != 0u;
    graph.edges.push_back(std::move(edge));
  }

  if (!cursor.AtEnd()) {
    return std::nullopt;
  }
  return graph;
}

std::optional<std::vector<GraphPayloadNode>> ReadGraphPayloadNodes(
    const std::vector<uint8_t>& bytes) {
  std::optional<GraphPayload> graph = ReadGraphPayload(bytes);
  if (!graph.has_value()) {
    return std::nullopt;
  }
  return std::move(graph->nodes);
}

std::string DescribeNodeStates(const std::vector<uint8_t>& states) {
  if (states.empty()) {
    return "none asserted (the adapter reached no determination)";
  }
  std::string described;
  for (const uint8_t state : states) {
    if (!described.empty()) {
      described += ", ";
    }
    switch (static_cast<NodeState>(state)) {
      case NodeState::kVisible: described += "kVisible"; break;
      case NodeState::kNotVisible: described += "kNotVisible"; break;
      case NodeState::kOffscreen: described += "kOffscreen"; break;
      case NodeState::kObscured: described += "kObscured"; break;
      case NodeState::kEnabled: described += "kEnabled"; break;
      case NodeState::kDisabled: described += "kDisabled"; break;
      case NodeState::kEditable: described += "kEditable"; break;
      case NodeState::kReadOnly: described += "kReadOnly"; break;
      case NodeState::kRequired: described += "kRequired"; break;
      case NodeState::kInvalid: described += "kInvalid"; break;
      case NodeState::kChecked: described += "kChecked"; break;
      case NodeState::kUnchecked: described += "kUnchecked"; break;
      case NodeState::kMixed: described += "kMixed"; break;
      case NodeState::kSelected: described += "kSelected"; break;
      case NodeState::kExpanded: described += "kExpanded"; break;
      case NodeState::kCollapsed: described += "kCollapsed"; break;
      case NodeState::kFocused: described += "kFocused"; break;
      case NodeState::kBusy: described += "kBusy"; break;
      default:
        described += "unknown(" + base::NumberToString(state) + ")";
        break;
    }
  }
  return described;
}

}  // namespace taffy::test
