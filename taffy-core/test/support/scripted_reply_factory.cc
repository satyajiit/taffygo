// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/scripted_reply_factory.h"

#include <utility>

namespace taffy::test {

// static
mojom::OriginPtr ScriptedReplyFactory::Origin(
    const std::string& serialization) {
  auto origin = mojom::Origin::New();
  if (serialization.empty()) {
    origin->kind = mojom::OriginKind::kOpaque;
    origin->opaque_id = "scripted-opaque";
    return origin;
  }
  origin->kind = mojom::OriginKind::kTuple;
  origin->serialization = serialization;
  return origin;
}

// static
mojom::UrlMetadataPtr ScriptedReplyFactory::OriginOnlyUrl(
    const std::string& serialization) {
  auto metadata = mojom::UrlMetadata::New();
  metadata->origin = Origin(serialization);
  metadata->disclosure = mojom::UrlDisclosure::kOriginOnly;
  metadata->has_query = false;
  metadata->has_fragment = false;
  return metadata;
}

// static
mojom::OriginMetadataPtr ScriptedReplyFactory::OriginMetadata(
    const std::string& serialization) {
  auto metadata = mojom::OriginMetadata::New();
  metadata->origin = Origin(serialization);
  metadata->is_potentially_trustworthy = false;
  metadata->is_incognito = false;
  return metadata;
}

// static
mojom::TruncationPtr ScriptedReplyFactory::NoTruncation() {
  auto truncation = mojom::Truncation::New();
  truncation->truncated = false;
  truncation->omitted_node_count = 0;
  truncation->omitted_text_bytes = 0;
  truncation->omitted_frame_count = 0;
  truncation->may_change_answer = false;
  return truncation;
}

// static
mojom::RedactionSummaryPtr ScriptedReplyFactory::NoRedaction() {
  auto summary = mojom::RedactionSummary::New();
  summary->redacted_field_count = 0;
  summary->suppressed_secret_value_count = 0;
  summary->sensitive_zone_count = 0;
  summary->policy_filtered_frame_count = 0;
  return summary;
}

// static
mojom::SemanticNodePtr ScriptedReplyFactory::Node(const std::string& node_id,
                                                  const std::string& frame_id,
                                                  const std::string& text) {
  auto node = mojom::SemanticNode::New();
  node->node_id = node_id;
  node->frame_id = frame_id;
  node->role = mojom::SemanticRole::kParagraph;
  node->name = text;

  auto run = mojom::TextRun::New();
  run->text = text;
  run->source_kind = mojom::SourceKind::kDom;
  run->sensitivity = mojom::Sensitivity::kNotSensitive;
  run->truncated = false;
  node->text_runs.push_back(std::move(run));

  node->sensitivity = mojom::Sensitivity::kNotSensitive;
  node->sources.push_back(mojom::SourceKind::kDom);
  node->confidence = 1.0;
  return node;
}

// static
mojom::FrameDescriptorPtr ScriptedReplyFactory::MainFrame(
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& origin) {
  auto frame = mojom::FrameDescriptor::New();
  frame->frame_id = frame_id;
  frame->is_main_frame = true;
  frame->is_out_of_process = false;
  frame->is_cross_origin_to_parent = false;
  frame->origin_metadata = OriginMetadata(origin);
  frame->page_epoch = page_epoch;
  frame->graph_revision = graph_revision;
  frame->lifecycle_state = mojom::DocumentLifecycleState::kActive;
  frame->included = true;
  return frame;
}

}  // namespace taffy::test
