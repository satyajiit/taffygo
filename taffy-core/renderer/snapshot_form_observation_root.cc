// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/snapshot_form_observation_root.h"

#include <optional>

#include "taffy/renderer/semantic_graph_store.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_form_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"

namespace taffy {

FormObservationRootStatus ValidateFormObservationRoot(
    const mojom::SnapshotRequest& request,
    blink::WebLocalFrame* frame,
    const SemanticGraphStore& store,
    ObservationRootValidationPhase phase) {
  const bool section = request.scope == mojom::ObservationScope::kSection;
  if (section != !request.form_root.is_null()) {
    return FormObservationRootStatus::kInvalidShape;
  }
  if (!section) {
    return FormObservationRootStatus::kOk;
  }
  if (!frame || request.form_root->node_id.empty() ||
      request.form_root->minimum_graph_revision == 0u) {
    return FormObservationRootStatus::kInvalidShape;
  }

  std::optional<GraphRevision> minimum_revision;
  if (phase == ObservationRootValidationPhase::kAdmission) {
    minimum_revision.emplace(request.form_root->minimum_graph_revision);
  }
  const SemanticGraphStore::ResolveResult resolved =
      store.Resolve(SemanticNodeId(request.form_root->node_id),
                    store.page_epoch(), minimum_revision);
  if (resolved.status != SemanticGraphStore::ResolveStatus::kOk ||
      !resolved.node.has_value()) {
    return FormObservationRootStatus::kAbsentOrStale;
  }
  if (resolved.node->dom_key.space != SemanticGraphStore::IdentitySpace::kDom) {
    return FormObservationRootStatus::kWrongDocumentOrType;
  }

  const blink::WebDocument document = frame->GetDocument();
  const blink::WebNode node = blink::WebNode::FromDomNodeId(
      static_cast<int>(resolved.node->dom_key.dom_node_id));
  if (document.IsNull() || node.IsNull() || !node.IsConnected() ||
      node.GetDocument() != document || !node.IsElementNode() ||
      node.DynamicTo<blink::WebFormElement>().IsNull()) {
    return FormObservationRootStatus::kWrongDocumentOrType;
  }
  return FormObservationRootStatus::kOk;
}

std::string_view FormObservationRootDetailCode(
    FormObservationRootStatus status) {
  switch (status) {
    case FormObservationRootStatus::kOk:
      return "form-root-ok";
    case FormObservationRootStatus::kInvalidShape:
      return "form-root-invalid-shape";
    case FormObservationRootStatus::kAbsentOrStale:
      return "form-root-absent-or-stale";
    case FormObservationRootStatus::kWrongDocumentOrType:
      return "form-root-wrong-document-or-type";
  }
  return "form-root-invalid-status";
}

MediaObservationRootStatus ValidateMediaObservationRoot(
    const mojom::SnapshotRequest& request,
    blink::WebLocalFrame* frame,
    const SemanticGraphStore& store,
    ObservationRootValidationPhase phase) {
  if (request.media_root.is_null()) {
    return MediaObservationRootStatus::kOk;
  }
  if (request.scope != mojom::ObservationScope::kDocument ||
      !request.form_root.is_null() || !frame ||
      request.media_root->node_id.empty() ||
      request.media_root->minimum_graph_revision == 0u) {
    return MediaObservationRootStatus::kInvalidShape;
  }

  std::optional<GraphRevision> minimum_revision;
  if (phase == ObservationRootValidationPhase::kAdmission) {
    minimum_revision.emplace(request.media_root->minimum_graph_revision);
  }
  const SemanticGraphStore::ResolveResult resolved =
      store.Resolve(SemanticNodeId(request.media_root->node_id),
                    store.page_epoch(), minimum_revision);
  if (resolved.status != SemanticGraphStore::ResolveStatus::kOk ||
      !resolved.node.has_value()) {
    return MediaObservationRootStatus::kAbsentOrStale;
  }
  if (resolved.node->dom_key.space != SemanticGraphStore::IdentitySpace::kDom ||
      (resolved.node->role != SemanticRole::kImage &&
       resolved.node->role != SemanticRole::kMedia)) {
    return MediaObservationRootStatus::kWrongDocumentOrType;
  }

  const blink::WebDocument document = frame->GetDocument();
  const blink::WebNode node = blink::WebNode::FromDomNodeId(
      static_cast<int>(resolved.node->dom_key.dom_node_id));
  if (document.IsNull() || node.IsNull() || !node.IsConnected() ||
      node.GetDocument() != document || !node.IsElementNode()) {
    return MediaObservationRootStatus::kWrongDocumentOrType;
  }
  return MediaObservationRootStatus::kOk;
}

std::string_view MediaObservationRootDetailCode(
    MediaObservationRootStatus status) {
  switch (status) {
    case MediaObservationRootStatus::kOk:
      return "media-root-ok";
    case MediaObservationRootStatus::kInvalidShape:
      return "media-root-invalid-shape";
    case MediaObservationRootStatus::kAbsentOrStale:
      return "media-root-absent-or-stale";
    case MediaObservationRootStatus::kWrongDocumentOrType:
      return "media-root-wrong-document-or-type";
  }
  return "media-root-invalid-status";
}

}  // namespace taffy
