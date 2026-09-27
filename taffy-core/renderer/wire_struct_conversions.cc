// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "base/check.h"
#include "taffy/renderer/content_metadata.h"
#include "taffy/renderer/field_redaction.h"
#include "taffy/renderer/observed_link_limits.h"
#include "taffy/renderer/wire_conversions.h"
#include "url/gurl.h"

// The struct half of the wire translation.
//
// Two rules live here and nowhere else, which is why it is its own file:
//
//   * The last redaction gate before serialization. It asks the same three
//     questions MayEmitText() asks everywhere else - a value, a control
//     label, and page content are different questions about the same
//     sensitivity - so a node that reached this point carrying text it should
//     not have loses it, whatever the adapter believed it was doing.
//
//   * An ordinary link's normalized destination may cross only to the trusted
//     browser's transient handle registry. The browser's BIP graph encoder
//     deliberately emits only destination flags, so neither the isolated core
//     nor a model receives the address.

namespace taffy::wire {
namespace {

void AppendContentSignals(ContentSignalMask signals,
                          std::vector<mojom::ContentSignal>* out) {
  CHECK_EQ(signals & ~content_metadata::KnownSignalMask(), 0u);
  for (ContentSignal signal : content_metadata::kCanonicalSignals) {
    if (content_metadata::HasSignal(signals, signal)) {
      out->push_back(ToMojom(signal));
    }
  }
}

}  // namespace

mojom::SnapshotWarningPtr ToMojom(const ObservationWarning& warning) {
  auto out = mojom::SnapshotWarning::New();
  out->code = ToMojom(warning.code);
  if (warning.node_id.has_value()) {
    out->node_id = warning.node_id->value();
  }
  out->detail_code = warning.detail_code;
  return out;
}

mojom::FieldEvidencePtr ToMojom(const FieldEvidence& evidence) {
  auto out = mojom::FieldEvidence::New();
  out->field = ToMojom(evidence.field);
  out->source_kind = ToMojom(evidence.source_kind);
  out->source_locator = evidence.source_locator;
  out->extraction_rule_version = evidence.extraction_rule_version;
  out->transformation = ToMojom(evidence.transformation);
  out->confidence = evidence.confidence;
  return out;
}

// Full link destinations cross this one renderer-to-browser seam so the
// browser can resolve a later opaque observed handle. They are omitted by the
// browser's graph framing and never cross to Rust or the model. Disclosure to
// a model therefore remains impossible by shape, while navigation policy and
// liveness remain browser-owned decisions.
mojom::DestinationPtr ToMojom(const Destination& destination) {
  const GURL url(destination.url);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
      url.has_password() || url.spec().empty() ||
      url.spec().size() > renderer::kMaxObservedLinkDestinationBytes) {
    // Unparsable, or a scheme that is not an ordinary web navigation. Handing
    // the browser a javascript: URL to reason about is not a favour.
    return nullptr;
  }

  auto out = mojom::Destination::New();
  out->url_metadata = mojom::UrlMetadata::New();
  out->url_metadata->origin = mojom::Origin::New();
  out->url_metadata->origin->kind = mojom::OriginKind::kTuple;
  out->url_metadata->origin->serialization =
      url.DeprecatedGetOriginAsURL().spec();
  out->url_metadata->disclosure = mojom::UrlDisclosure::kFullUrl;
  out->url_metadata->path = url.path();
  out->url_metadata->url = url.spec();
  out->url_metadata->has_query = url.has_query();
  out->url_metadata->has_fragment = url.has_ref();
  out->is_cross_origin = destination.is_cross_origin;
  out->opens_new_tab = destination.opens_new_tab;
  out->is_download = destination.is_download;
  return out;
}

mojom::SemanticNodePtr ToMojom(const SemanticNode& node) {
  auto out = mojom::SemanticNode::New();
  out->node_id = node.node_id.value();
  out->frame_id = node.frame_id.value();
  out->role = ToMojom(node.role);
  out->sensitivity = ToMojom(node.sensitivity);
  out->challenge_kind = ToMojom(node.challenge_kind);
  out->confidence = node.confidence;
  out->content_trust = ToMojom(node.content_trust);
  AppendContentSignals(node.content_signals, &out->content_signals);

  // The last redaction gate before serialization, and the last chance to
  // catch an adapter that forgot. It asks the same three questions
  // MayEmitText() asks everywhere else - a value, a control label, and page
  // content are different questions about the same sensitivity - so a node
  // that reached here with text it should not have loses it, whatever the
  // adapter believed it was doing (protocol section 9).
  //
  // A node with a withheld secret is treated as a control throughout: its
  // label and its value are both refused, and it carries neither by
  // construction anyway.
  const bool is_control_like =
      node.value_descriptor.has_value() &&
      node.value_descriptor->kind == ValueKind::kSecretWithheld;
  const NodeTextClass label_class =
      is_control_like ? NodeTextClass::kControlLabel : NodeTextClass::kContent;
  if (MayEmitText(node.sensitivity, label_class)) {
    out->name = node.name;
    out->description = node.description;
  }
  if (MayEmitText(node.sensitivity, NodeTextClass::kContent)) {
    for (const TextRun& run : node.text_runs) {
      auto text_run = mojom::TextRun::New();
      text_run->text = run.text;
      text_run->source_kind = ToMojom(run.source_kind);
      text_run->sensitivity = ToMojom(run.sensitivity);
      text_run->truncated = run.truncated;
      text_run->source_locator = run.source_locator;
      text_run->source_start = run.source_start;
      text_run->source_end = run.source_end;
      text_run->content_trust = ToMojom(run.content_trust);
      AppendContentSignals(run.content_signals, &text_run->content_signals);
      out->text_runs.push_back(std::move(text_run));
    }
  }

  for (NodeState state : node.states) {
    out->states.push_back(ToMojom(state));
  }
  if (node.value_descriptor.has_value()) {
    auto value = mojom::ValueDescriptor::New();
    value->kind = ToMojom(node.value_descriptor->kind);
    value->present = node.value_descriptor->present;
    value->redacted = node.value_descriptor->redacted;
    // A normalized value is a value. It travels only for a node that is
    // demonstrably not sensitive, which is the same answer MayObserveValue()
    // gives - stated through MayEmitText() so there is one rule and not two.
    value->normalized_value =
        MayEmitText(node.sensitivity, NodeTextClass::kControlValue)
            ? node.value_descriptor->normalized_value
            : std::nullopt;
    value->unit = node.value_descriptor->unit;
    value->currency_code = node.value_descriptor->currency_code;
    out->value_descriptor = std::move(value);
  }
  if (node.destination.has_value()) {
    out->destination = ToMojom(node.destination.value());
  }
  if (node.bounds.has_value()) {
    auto bounds = mojom::Bounds::New();
    bounds->x = node.bounds->x;
    bounds->y = node.bounds->y;
    bounds->width = node.bounds->width;
    bounds->height = node.bounds->height;
    out->bounds = std::move(bounds);
  }
  for (ActionKind action : node.actions) {
    out->actions.push_back(ToMojom(action));
  }
  for (SourceKind source : node.sources) {
    out->sources.push_back(ToMojom(source));
  }
  for (const Attribute& attribute : node.attributes) {
    auto typed = mojom::NodeAttribute::New();
    typed->name = ToMojom(attribute.key);
    typed->value = attribute.value;
    out->attributes.push_back(std::move(typed));
  }
  for (const FieldEvidence& evidence : node.evidence) {
    out->evidence.push_back(ToMojom(evidence));
  }
  return out;
}

mojom::SemanticEdgePtr ToMojom(const SemanticEdge& edge) {
  auto out = mojom::SemanticEdge::New();
  out->from_frame_id = edge.from_frame_id.value();
  out->from_node_id = edge.from_node_id.value();
  out->to_frame_id = edge.to_frame_id.value();
  out->to_node_id = edge.to_node_id.value();
  out->relationship = ToMojom(edge.relationship);
  out->inferred = edge.inferred;
  out->confidence = edge.confidence;
  return out;
}

}  // namespace taffy::wire
