// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_GRAPH_PAYLOAD_ENCODER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_GRAPH_PAYLOAD_ENCODER_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/page_inspector_projection.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// Turns a validated renderer snapshot into the generated BIP graph payload
// carried inside the typed Core Service observation result.
//
// Mojo performs the only deserialization. Browser C++ then makes one bounded
// pass that frames the generated graph for Rust and derives a smaller UI-safe
// projection with fresh identities and coarse closed categories. It does not
// infer page meaning or retain renderer identities; semantic interpretation
// remains in Rust, and raw graph bytes never enter Core API or UI types.
//
// Two encoders exist. BipGraphPayloadEncoder below carries the graph and
// reports GraphPayloadEncoding::kBipContract; MetadataOnlyGraphPayloadEncoder
// carries nothing and reports kNone, which is what a lifecycle-only
// observation needs and what any endpoint with no encoder installed reports.
// The default is the metadata-only one, so an encoder that was never installed
// is visible as kNone rather than as a zero-length graph.

namespace taffy {

class GraphPayloadEncoder {
 public:
  virtual ~GraphPayloadEncoder() = default;

  struct Encoded {
    GraphPayloadEncoding encoding = GraphPayloadEncoding::kNone;
    std::vector<uint8_t> bytes;
    // True only when an encoder that was asked to carry a graph refused its
    // input. Distinct from the metadata-only encoder's deliberate kNone: an
    // invalid renderer graph must not leave an otherwise-kOk observation that
    // merely looks as though no encoder was installed.
    bool rejected = false;
    // Why it was refused, when it was: the graph did not fit under the byte
    // ceiling, rather than being malformed or authority-invalid. Only ever
    // true alongside `rejected`, and it exists because those two produce
    // different result codes — a page too large for its budget is answerable
    // by asking for less, and a renderer reply that does not conform is not.
    bool did_not_fit = false;
    // What the browser's own redaction pass removed while framing `bytes`
    // (renderer_text_rescan.h). Zero from an encoder that carries no graph,
    // because there was no renderer text to rescan.
    RescanTally rescan;
    // A second, smaller projection with fresh local identities and closed
    // coarse categories. It never contains raw BIP identifiers or values.
    std::optional<InspectorGraphProjection> inspector_projection;
  };

  // `snapshot` has already had its identity echo checked against
  // browser-owned state and its counts checked against the clamped budget.
  //
  // `frame_id` is the browser's identity for the document being encoded, and
  // it is required rather than optional: it replaces the renderer's frame
  // identity on every node row (bip_graph_payload.h explains why). Passing the
  // renderer's echo back in would defeat the whole point, so callers pass the
  // value they checked the echo against, never the echo.
  //
  // `max_bytes` and `max_text_bytes` are the already-clamped request ceilings.
  // The encoder applies both while constructing the byte vector, so untrusted
  // or withheld renderer input cannot spend the framing-wide emergency ceiling
  // before a downstream broker notices that the request asked for less.
  virtual Encoded Encode(const mojom::PageSnapshot& snapshot,
                         const FrameId& frame_id,
                         size_t max_bytes,
                         size_t max_text_bytes) = 0;

  virtual Encoded EncodeDelta(const mojom::PageDelta& delta,
                              const FrameId& frame_id,
                              size_t max_bytes) = 0;
};

// The default. Carries no graph and says so.
class MetadataOnlyGraphPayloadEncoder : public GraphPayloadEncoder {
 public:
  MetadataOnlyGraphPayloadEncoder();
  ~MetadataOnlyGraphPayloadEncoder() override;

  Encoded Encode(const mojom::PageSnapshot& snapshot,
                 const FrameId& frame_id,
                 size_t max_bytes,
                 size_t max_text_bytes) override;
  Encoded EncodeDelta(const mojom::PageDelta& delta,
                      const FrameId& frame_id,
                      size_t max_bytes) override;
};

std::unique_ptr<GraphPayloadEncoder> MakeMetadataOnlyGraphPayloadEncoder();

// The encoder that actually carries a graph. It reports
// GraphPayloadEncoding::kBipContract and emits the graph-body framing declared
// in bip_graph_payload.h. Observation metadata never enters this framing: it
// crosses through generated Core Service fields.
//
// It walks only the fields Mojo already deserialized, writes each as a bounded
// scalar or a length-prefixed string, withholds the name of any node the
// browser has not established is insensitive, and rescans names that do cross.
// The reading of the graph — what the page means and whether it may be acted
// on — stays in Rust.
class BipGraphPayloadEncoder : public GraphPayloadEncoder {
 public:
  BipGraphPayloadEncoder();
  ~BipGraphPayloadEncoder() override;

  Encoded Encode(const mojom::PageSnapshot& snapshot,
                 const FrameId& frame_id,
                 size_t max_bytes,
                 size_t max_text_bytes) override;
  Encoded EncodeDelta(const mojom::PageDelta& delta,
                      const FrameId& frame_id,
                      size_t max_bytes) override;
};

std::unique_ptr<GraphPayloadEncoder> MakeBipGraphPayloadEncoder();

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_GRAPH_PAYLOAD_ENCODER_H_
