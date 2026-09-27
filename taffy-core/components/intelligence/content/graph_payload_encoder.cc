// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/graph_payload_encoder.h"

#include <memory>
#include <utility>

#include "taffy/components/intelligence/content/bip_graph_payload.h"

namespace taffy {

MetadataOnlyGraphPayloadEncoder::MetadataOnlyGraphPayloadEncoder() = default;
MetadataOnlyGraphPayloadEncoder::~MetadataOnlyGraphPayloadEncoder() = default;

GraphPayloadEncoder::Encoded MetadataOnlyGraphPayloadEncoder::Encode(
    const mojom::PageSnapshot& snapshot,
    const FrameId& frame_id,
    size_t max_bytes,
    size_t max_text_bytes) {
  // Deliberately empty. Reporting kNone is what lets a caller tell the
  // difference between "this page has no semantic content" and "no encoder is
  // installed", which a zero-length kBipContract payload would hide.
  return Encoded();
}

GraphPayloadEncoder::Encoded MetadataOnlyGraphPayloadEncoder::EncodeDelta(
    const mojom::PageDelta& delta,
    const FrameId& frame_id,
    size_t max_bytes) {
  return Encoded();
}

std::unique_ptr<GraphPayloadEncoder> MakeMetadataOnlyGraphPayloadEncoder() {
  return std::make_unique<MetadataOnlyGraphPayloadEncoder>();
}

BipGraphPayloadEncoder::BipGraphPayloadEncoder() = default;
BipGraphPayloadEncoder::~BipGraphPayloadEncoder() = default;

GraphPayloadEncoder::Encoded BipGraphPayloadEncoder::Encode(
    const mojom::PageSnapshot& snapshot,
    const FrameId& frame_id,
    size_t max_bytes,
    size_t max_text_bytes) {
  Encoded encoded;
  InspectorGraphProjection projection;
  encoded.bytes =
      EncodeGraphPayload(snapshot, frame_id, &encoded.rescan, &projection,
                         max_bytes, max_text_bytes, &encoded.did_not_fit);
  // A refusal reports kNone rather than kBipContract with no bytes, for the
  // same reason the metadata-only encoder does: "no graph" and "the graph
  // would not fit" must not decode to the same thing.
  if (!encoded.bytes.empty()) {
    encoded.encoding = GraphPayloadEncoding::kBipContract;
    if (projection.nodes.size() == snapshot.nodes.size() &&
        projection.edges.size() == snapshot.edges.size()) {
      encoded.inspector_projection = std::move(projection);
    }
  } else {
    encoded.rejected = true;
  }
  return encoded;
}

GraphPayloadEncoder::Encoded BipGraphPayloadEncoder::EncodeDelta(
    const mojom::PageDelta& delta,
    const FrameId& frame_id,
    size_t max_bytes) {
  Encoded encoded;
  encoded.bytes =
      EncodeDeltaPayload(delta, frame_id, &encoded.rescan, max_bytes);
  if (!encoded.bytes.empty()) {
    encoded.encoding = GraphPayloadEncoding::kBipContract;
  } else {
    encoded.rejected = true;
  }
  return encoded;
}

std::unique_ptr<GraphPayloadEncoder> MakeBipGraphPayloadEncoder() {
  return std::make_unique<BipGraphPayloadEncoder>();
}

}  // namespace taffy
