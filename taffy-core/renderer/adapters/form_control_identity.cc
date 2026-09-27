// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/form_control_identity.h"

#include <inttypes.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "taffy/renderer/content_metadata.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"

namespace taffy::form_schema_internal {
namespace {

std::string SafeAccessibleName(const ui::AXNodeData& data) {
  // A name derived from the control's value is deliberately rejected. Form
  // observation never reads field values, and asking accessibility for the
  // name must not become a side door around that invariant.
  switch (data.GetNameFrom()) {
    case ax::mojom::NameFrom::kValue:
    case ax::mojom::NameFrom::kNone:
    case ax::mojom::NameFrom::kAttributeExplicitlyEmpty:
    case ax::mojom::NameFrom::kProhibited:
    case ax::mojom::NameFrom::kProhibitedAndRedundant:
      return std::string();
    case ax::mojom::NameFrom::kAttribute:
    case ax::mojom::NameFrom::kCaption:
    case ax::mojom::NameFrom::kContents:
    case ax::mojom::NameFrom::kCssAltText:
    case ax::mojom::NameFrom::kInterestFor:
    case ax::mojom::NameFrom::kPlaceholder:
    case ax::mojom::NameFrom::kRelatedElement:
    case ax::mojom::NameFrom::kTitle:
    case ax::mojom::NameFrom::kPopoverTarget:
      return data.GetStringAttribute(ax::mojom::StringAttribute::kName);
  }
}

bool GraphAlreadyContains(const ExtractedGraph* graph,
                          const SemanticNodeId& node_id) {
  return graph != nullptr &&
         std::ranges::any_of(graph->nodes, [&](const SemanticNode& node) {
           return node.node_id == node_id;
         });
}

FieldEvidence Evidence(SemanticField field,
                       SourceKind source,
                       std::string locator,
                       uint32_t rule_version,
                       Transformation transformation) {
  return FieldEvidence(field, source, std::move(locator), rule_version,
                       transformation);
}

SemanticEdge ExactIdentityEdge(const FrameId& frame_id,
                               const SemanticNodeId& accessibility_node_id,
                               const SemanticNodeId& control_node_id) {
  SemanticEdge edge;
  edge.from_frame_id = frame_id;
  edge.from_node_id = accessibility_node_id;
  edge.to_frame_id = frame_id;
  edge.to_node_id = control_node_id;
  edge.relationship = EdgeType::kSameEntityAs;
  return edge;
}

SemanticNode LabelWitness(const SemanticNodeId& accessibility_node_id,
                          const SemanticNode& control_node,
                          std::string label,
                          Sensitivity label_sensitivity,
                          std::string locator,
                          const ExtractionContext& context,
                          uint32_t rule_version) {
  SemanticNode witness;
  witness.node_id = accessibility_node_id;
  witness.frame_id = context.store->frame_id();
  witness.role = control_node.role;
  witness.name = std::move(label);
  witness.sensitivity = label_sensitivity;
  witness.confidence = 1.0;
  witness.sources.push_back(SourceKind::kAccessibility);
  witness.projection_path = ProjectionPath::kComposedTreeThroughAccessibility;
  content_metadata::ApplyNodeContext(&witness, control_node.content_trust,
                                     control_node.content_signals);
  content_metadata::AddNodeTextSignals(&witness, *witness.name,
                                       control_node.content_signals,
                                       context.limits->content_signals());
  witness.evidence.push_back(Evidence(SemanticField::kRole,
                                      SourceKind::kFormControl, locator,
                                      rule_version, Transformation::kInferred));
  witness.evidence.push_back(Evidence(SemanticField::kName,
                                      SourceKind::kAccessibility, locator,
                                      rule_version, Transformation::kNone));
  witness.evidence.push_back(
      Evidence(SemanticField::kSensitivity, SourceKind::kAccessibility,
               std::move(locator), rule_version, Transformation::kInferred));
  return witness;
}

}  // namespace

bool EnrichFormControlIdentity(const blink::WebFormControlElement& control,
                               ExtractionContext& context,
                               uint32_t rule_version,
                               SemanticNode& control_node,
                               AdapterResult& result) {
  // The HTML accessible-name computation is the one place that already knows
  // every way a control can be labelled: <label for>, a wrapping <label>,
  // ARIA, title, placeholder, and button contents.
  const blink::WebAXObject object = blink::WebAXObject::FromWebNode(control);
  if (object.IsDetached()) {
    return true;
  }
  if (!object.IsIncludedInTree()) {
    // Blink refuses to serialize an object it did not include in its tree, and
    // refuses it fatally outside an official build, so asking one of these for
    // a name aborted the renderer rather than answering it. A form's control
    // list comes from the DOM, so it carries controls the accessibility tree
    // leaves out: a hidden input, or one under a display:none subtree.
    //
    // Return rather than continue without the serialization, because there is
    // nothing here to enrich with. An unincluded object has no accessible name
    // to compute, and the accessibility adapter emits no identity row for one
    // either, so the exact-identity edge looked up below could never be found.
    return true;
  }
  ui::AXNodeData data;
  object.Serialize(&data, ui::kAXModeBasic);

  const std::optional<SemanticNodeId> accessibility_node_id =
      context.store->Lookup(context.store->MakeKey(
          SemanticGraphStore::IdentitySpace::kAccessibility, object.AxID()));
  if (accessibility_node_id.has_value() &&
      *accessibility_node_id != control_node.node_id) {
    result.edges.push_back(ExactIdentityEdge(context.store->frame_id(),
                                             *accessibility_node_id,
                                             control_node.node_id));
  }

  const std::string accessible_name = SafeAccessibleName(data);
  const std::string raw_label(
      base::TrimWhitespaceASCII(accessible_name, base::TRIM_ALL));
  const Sensitivity label_sensitivity =
      context.sensitivity_classifier.ClassifyContentRegion(
          raw_label, context.cross_origin_frame(), context.policy_floor());
  if (!MayEmitText(control_node.sensitivity, NodeTextClass::kControlLabel)) {
    return true;
  }

  std::string label = context.ledger->BoundText(raw_label);
  if (label.empty() ||
      context.prohibited_value_filter.LooksLikeHighRiskIdentifier(label)) {
    return true;
  }

  const std::string locator =
      base::StringPrintf("form-control/%" PRId64, control.GetDomNodeId());
  control_node.name = label;
  control_node.sources.push_back(SourceKind::kAccessibility);
  content_metadata::AddNodeTextSignals(&control_node, label,
                                       control_node.content_signals,
                                       context.limits->content_signals());
  control_node.evidence.push_back(
      Evidence(SemanticField::kName, SourceKind::kAccessibility, locator,
               rule_version, Transformation::kNone));

  // The browser deliberately withholds every name attached to a sensitive
  // control. A SECTION does not run the full accessibility adapter, so its AX
  // identity row would otherwise be absent and the exact SAME_ENTITY_AS edge
  // would be removed as dangling. Emit only the narrow label witness needed
  // to preserve that identity: it carries no value, state, action, or bounds.
  if (context.scope != ExtractionScope::kSection ||
      control_node.sensitivity == Sensitivity::kNotSensitive ||
      label_sensitivity != Sensitivity::kNotSensitive ||
      !accessibility_node_id.has_value() ||
      *accessibility_node_id == control_node.node_id ||
      GraphAlreadyContains(context.accumulated, *accessibility_node_id)) {
    return true;
  }
  if (!context.ledger->ChargeNode()) {
    context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
    return false;
  }
  result.nodes.push_back(LabelWitness(*accessibility_node_id, control_node,
                                      std::move(label), label_sensitivity,
                                      locator, context, rule_version));
  return true;
}

std::string PersonFacingControlLabel(
    const blink::WebFormControlElement& control,
    const ExtractionContext& context) {
  // A sheet row's label; generous for a label, small beside any page.
  constexpr size_t kMaxPersonFacingLabelBytes = 256;
  const blink::WebAXObject object = blink::WebAXObject::FromWebNode(control);
  // The same two refusals as above, for the same reason: an unincluded
  // object aborts the renderer when serialized, and has no name to give.
  if (object.IsDetached() || !object.IsIncludedInTree()) {
    return std::string();
  }
  ui::AXNodeData data;
  object.Serialize(&data, ui::kAXModeBasic);
  const std::string name = SafeAccessibleName(data);
  std::string label(base::TruncateUTF8ToByteSize(
      base::TrimWhitespaceASCII(name, base::TRIM_ALL),
      kMaxPersonFacingLabelBytes));
  if (context.prohibited_value_filter.LooksLikeHighRiskIdentifier(label)) {
    return std::string();
  }
  return label;
}

}  // namespace taffy::form_schema_internal
