// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/bip_graph_payload.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "taffy/components/intelligence/content/bip_graph_payload_row.h"
#include "taffy/components/intelligence/content/bip_payload_writer.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// The framing: what goes in the payload and in what order.
//
// What any one node is allowed to disclose is decided in
// bip_graph_payload_row.h and nowhere else. The two halves are separate files
// because they answer different questions -- this one could be got wrong and
// produce a payload the core refuses, while that one could be got wrong and
// produce a payload the core accepts and should not have been given.

namespace taffy {

using bip_payload::ToInspectorRelationship;
using bip_payload::WriteEdge;
using bip_payload::WriteNode;
using bip_payload::Writer;

std::vector<uint8_t> EncodeGraphPayload(const mojom::PageSnapshot& snapshot,
                                        const FrameId& frame_id,
                                        RescanTally* tally,
                                        InspectorGraphProjection* projection,
                                        size_t max_bytes,
                                        size_t max_text_bytes,
                                        bool* did_not_fit) {
  if (did_not_fit) {
    *did_not_fit = false;
  }
  // The text budget is enforced per row rather than by the writer, so a page
  // large enough to exhaust it never touches the byte ceiling and the writer
  // has nothing to report. That is the budget an oversized document actually
  // hits, and reading only `Writer::overflowed` called it a malformed reply.
  bool text_budget_exhausted = false;
  Writer out(std::min(max_bytes, kMaxBipGraphPayloadBytes));
  if (projection) {
    projection->nodes.clear();
    projection->edges.clear();
  }
  bool build_projection = projection && snapshot.nodes.size() <= 128u &&
                          snapshot.edges.size() <= 256u;
  // Keyed on the renderer's own (frame, node) pair rather than on the identity
  // written to the wire above. This map never leaves the function: it exists
  // only to give an edge's endpoints the same display name its nodes got, and
  // the two sides of that lookup have to agree with each other rather than
  // with the browser. Rewriting the key here would break the edge lookup for
  // no gain, because a display id is already a fresh local identity that
  // carries none of the renderer's.
  std::map<std::pair<std::string, std::string>, std::string> display_ids;
  if (snapshot.schema_version.empty() ||
      snapshot.schema_version.size() > kMaxBipIdentityBytes ||
      frame_id.value.empty() || frame_id.value.size() > kMaxBipIdentityBytes) {
    return {};
  }
  out.U8(kBipGraphPayloadFraming);
  out.Short(snapshot.schema_version);

  size_t remaining_text_bytes = max_text_bytes;
  out.Count(snapshot.nodes.size());
  for (size_t index = 0; index < snapshot.nodes.size(); ++index) {
    const auto& node = snapshot.nodes[index];
    const std::string display_id = "item-" + std::to_string(index + 1u);
    InspectorNodeProjection projected_node;
    if (!node ||
        !WriteNode(out, *node, frame_id, tally, remaining_text_bytes,
                   display_id, build_projection ? &projected_node : nullptr,
                   &text_budget_exhausted)) {
      if (projection) {
        *projection = InspectorGraphProjection();
      }
      if (did_not_fit) {
        *did_not_fit = out.overflowed() || text_budget_exhausted;
      }
      return {};
    }
    if (build_projection) {
      const auto [unused, inserted] = display_ids.emplace(
          std::make_pair(node->frame_id, node->node_id), display_id);
      static_cast<void>(unused);
      if (!inserted) {
        build_projection = false;
        projection->nodes.clear();
      } else {
        projection->nodes.push_back(std::move(projected_node));
      }
    }
  }

  out.Count(snapshot.edges.size());
  for (const auto& edge : snapshot.edges) {
    if (!edge) {
      if (projection) {
        *projection = InspectorGraphProjection();
      }
      return {};
    }
    if (!WriteEdge(out, *edge)) {
      if (projection) {
        *projection = InspectorGraphProjection();
      }
      if (did_not_fit) {
        *did_not_fit = out.overflowed() || text_budget_exhausted;
      }
      return {};
    }
    if (build_projection) {
      const auto from = display_ids.find(
          std::make_pair(edge->from_frame_id, edge->from_node_id));
      const auto to =
          display_ids.find(std::make_pair(edge->to_frame_id, edge->to_node_id));
      if (from == display_ids.end() || to == display_ids.end()) {
        build_projection = false;
        projection->nodes.clear();
        projection->edges.clear();
      } else {
        projection->edges.push_back(InspectorEdgeProjection{
            .from_display_id = from->second,
            .to_display_id = to->second,
            .relationship = ToInspectorRelationship(edge->relationship),
            .inferred = edge->inferred,
        });
      }
    }
  }
  std::vector<uint8_t> bytes = out.Take();
  if (bytes.empty() || !build_projection) {
    if (projection) {
      *projection = InspectorGraphProjection();
    }
  }
  // Reported from the writer rather than inferred from the empty vector: the
  // early returns above are malformed input and never touch the ceiling, and
  // an encoder that guessed from emptiness would call every one of them a
  // budget problem.
  if (did_not_fit && bytes.empty()) {
    *did_not_fit = out.overflowed() || text_budget_exhausted;
  }
  return bytes;
}

std::vector<uint8_t> EncodeDeltaPayload(const mojom::PageDelta& delta,
                                        const FrameId& frame_id,
                                        RescanTally* tally,
                                        size_t max_bytes) {
  Writer out(std::min(max_bytes, kMaxBipGraphPayloadBytes));
  if (delta.schema_version.empty() ||
      delta.schema_version.size() > kMaxBipIdentityBytes ||
      delta.subscription_id.empty() ||
      delta.subscription_id.size() > kMaxBipIdentityBytes ||
      frame_id.value.empty() || frame_id.value.size() > kMaxBipIdentityBytes ||
      delta.page_epoch.empty() ||
      delta.page_epoch.size() > kMaxBipIdentityBytes) {
    return {};
  }
  out.U8(kBipDeltaPayloadFraming);
  out.Short(delta.schema_version);
  out.Short(delta.subscription_id);
  // The browser's identity again, for the same reason and on the same terms as
  // the node rows below. The stream's own check has already refused a delta
  // whose echoed frame is not the subscription's, so this write agrees with
  // that echo today; it is written from the browser's record anyway, so the
  // agreement is a property of the code rather than of a renderer.
  out.Short(frame_id.value);
  out.Short(delta.page_epoch);
  out.U64(delta.from_revision);
  out.U64(delta.to_revision);
  out.U64(delta.event_sequence);
  out.U32(delta.coalesced_mutation_count);

  // A delta has no separate text axis. Its one-message ceiling therefore also
  // bounds original text, including sensitive runs that produce no payload
  // bytes and could otherwise buy unbounded browser-process work.
  size_t remaining_text_bytes = std::min(max_bytes, kMaxBipGraphPayloadBytes);
  out.Count(delta.added_nodes.size());
  for (const auto& node : delta.added_nodes) {
    if (!node ||
        !WriteNode(out, *node, frame_id, tally, remaining_text_bytes,
                   std::string(), nullptr)) {
      return {};
    }
  }

  // A changed node's identity and its new sensitivity are what invalidate a
  // held handle; its body is not carried, because a consumer that needs the
  // body asks for a fresh snapshot and a consumer that does not would be
  // holding page content it never used.
  out.Count(delta.changed_nodes.size());

  out.Count(delta.removed_node_ids.size());
  for (const std::string& node_id : delta.removed_node_ids) {
    if (node_id.empty() || node_id.size() > kMaxBipIdentityBytes) {
      return {};
    }
    out.Short(node_id);
    if (!out.ok()) {
      return {};
    }
  }

  out.Count(delta.changed_edges.size());
  for (const auto& edge : delta.changed_edges) {
    if (!edge) {
      return {};
    }
    if (!WriteEdge(out, *edge)) {
      return {};
    }
  }

  out.U8(delta.truncation && delta.truncation->truncated ? 1u : 0u);
  return out.Take();
}

}  // namespace taffy
