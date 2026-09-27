// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_SCRIPTED_REPLY_FACTORY_H_
#define TAFFY_TEST_SUPPORT_SCRIPTED_REPLY_FACTORY_H_

#include <string>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// The small, well-formed wire pieces a scripted renderer endpoint assembles its
// replies from.
//
// Split out of ScriptedRendererEndpoint because the two are read at different
// times and for different reasons. The endpoint is read by someone asking "what
// does a hostile renderer do", and these are read by someone asking "is this
// reply well formed at all" — which has to be true before a hostile variation
// means anything. A reply that a hostile endpoint produced by accident, because
// a required field was missing, would be refused for the wrong reason and would
// look exactly like the defence working.
//
// Nothing here is hostile. Every piece is the honest shape, and the endpoint is
// where a deliberate deviation is introduced and named.

namespace taffy::test {

class ScriptedReplyFactory {
 public:
  ScriptedReplyFactory() = delete;

  // An ordinary tuple origin. Passing an empty serialization produces an
  // opaque origin with a stable identifier, because an opaque origin with no
  // identifier is not a valid origin and would be refused before any test
  // could observe anything about it.
  static mojom::OriginPtr Origin(const std::string& serialization);

  static mojom::UrlMetadataPtr OriginOnlyUrl(const std::string& serialization);
  static mojom::OriginMetadataPtr OriginMetadata(
      const std::string& serialization);

  // An untruncated truncation summary and an empty redaction summary. Both
  // fields are required on the wire, so "nothing happened" still has to be
  // said rather than omitted.
  static mojom::TruncationPtr NoTruncation();
  static mojom::RedactionSummaryPtr NoRedaction();

  // One ordinary paragraph node carrying `text`.
  static mojom::SemanticNodePtr Node(const std::string& node_id,
                                     const std::string& frame_id,
                                     const std::string& text);

  // One included main-frame descriptor.
  static mojom::FrameDescriptorPtr MainFrame(const std::string& frame_id,
                                             const std::string& page_epoch,
                                             uint64_t graph_revision,
                                             const std::string& origin);

};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_SCRIPTED_REPLY_FACTORY_H_
