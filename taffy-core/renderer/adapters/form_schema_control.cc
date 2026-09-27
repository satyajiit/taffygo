// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/form_schema_control.h"

#include <inttypes.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "taffy/renderer/adapters/form_control_identity.h"
#include "taffy/renderer/adapters/form_control_type_names.h"
#include "taffy/renderer/challenge_classifier.h"
#include "taffy/renderer/content_metadata.h"
#include "third_party/blink/public/mojom/forms/form_control_type.mojom-shared.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_input_element.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"

namespace taffy::form_schema_internal {
namespace {

std::string Utf8(const blink::WebString& value) {
  return value.IsNull() ? std::string() : value.Utf8();
}

std::string ControlTypeOf(const blink::WebFormControlElement& control) {
  return form_control_type_names::ControlTypeString(
      control.FormControlTypeForAutofill());
}

SemanticRole RoleForControlType(const std::string& type) {
  if (type == "checkbox") {
    return SemanticRole::kCheckbox;
  }
  if (type == "radio") {
    return SemanticRole::kRadio;
  }
  if (type == "select-one" || type == "select-multiple") {
    return SemanticRole::kSelect;
  }
  if (type == "search") {
    return SemanticRole::kSearchField;
  }
  if (type == "submit" || type == "button" || type == "reset") {
    return SemanticRole::kButton;
  }
  return SemanticRole::kTextField;
}

void AppendCurrentCheckedState(const blink::WebFormControlElement& control,
                               std::vector<NodeState>* states) {
  if (!states) {
    return;
  }

  // Blink refuses to serialize an object it did not include in its tree, and
  // refuses it fatally outside an official build. The control list this runs
  // over comes from the DOM, so it reaches controls the tree leaves out — a
  // hidden checkbox, or one under a display:none subtree — and it also runs
  // before anything has necessarily updated the tree's cached values.
  const blink::WebAXObject object = blink::WebAXObject::FromWebNode(control);
  if (!object.IsDetached() && object.IsIncludedInTree()) {
    ui::AXNodeData data;
    object.Serialize(&data, ui::kAXModeBasic);
    switch (data.GetCheckedState()) {
      case ax::mojom::CheckedState::kTrue:
        states->push_back(NodeState::kChecked);
        return;
      case ax::mojom::CheckedState::kFalse:
        states->push_back(NodeState::kUnchecked);
        return;
      case ax::mojom::CheckedState::kMixed:
        states->push_back(NodeState::kMixed);
        return;
      case ax::mojom::CheckedState::kNone:
        break;
    }
  }

  // Ask the element, rather than returning with the state unsaid. A control
  // the tree cannot describe is not a control with no checked state, and
  // `FormAdapterTest.AccessibilityToggleKeepsPreActionBaselineAndRefreshesExactState`
  // states the consequence of treating the two as the same: "an unchecked
  // control must assert its state; silence cannot authorize a state-setting
  // toggle". The dispatcher reads this baseline before it will toggle
  // anything, so an omitted kUnchecked is not a missing detail — it is a
  // refused action, and the refusal names the wrong cause.
  //
  // The element has no indeterminate accessor, so kMixed is reachable only
  // through the tree above. A hidden control that is indeterminate reports
  // unchecked here, which is the answer its submitted value would give.
  const blink::WebInputElement input =
      control.DynamicTo<blink::WebInputElement>();
  if (input.IsNull()) {
    return;
  }
  states->push_back(input.IsChecked() ? NodeState::kChecked
                                      : NodeState::kUnchecked);
}

FieldDescriptor DescribeField(const blink::WebFormControlElement& control,
                              const std::string& control_type,
                              const std::string& form_semantics,
                              const ExtractionContext& context,
                              bool insecure_context) {
  FieldDescriptor field;
  field.control_type = control_type;
  field.autocomplete_token =
      base::ToLowerASCII(AuthoredAttribute(control, "autocomplete"));
  field.authored_name = base::ToLowerASCII(Utf8(control.NameForAutofill()));
  field.label_text = base::ToLowerASCII(
      base::JoinString({AuthoredAttribute(control, "aria-label"),
                        AuthoredAttribute(control, "placeholder"),
                        AuthoredAttribute(control, "title")},
                       " "));
  field.aria_role = base::ToLowerASCII(AuthoredAttribute(control, "role"));
  field.form_semantics = form_semantics;
  field.in_cross_origin_frame = context.cross_origin_frame();
  field.insecure_context = insecure_context;
  field.policy_floor = context.policy_floor();
  field.hidden =
      control_type == "hidden" ||
      !AuthoredAttribute(control, "hidden").empty() ||
      base::ToLowerASCII(AuthoredAttribute(control, "aria-hidden")) == "true";
  field.blink_reports_password_field =
      control.FormControlTypeForAutofill() ==
      blink::mojom::FormControlType::kInputPassword;
  return field;
}

SemanticEdge MakeFormRelationshipEdge(const FrameId& frame_id,
                                      const SemanticNodeId& from,
                                      const SemanticNodeId& to,
                                      EdgeType relationship) {
  SemanticEdge edge;
  edge.from_frame_id = frame_id;
  edge.from_node_id = from;
  edge.to_frame_id = frame_id;
  edge.to_node_id = to;
  edge.relationship = relationship;
  return edge;
}

void AppendFormOwnershipEdges(const FrameId& frame_id,
                              const SemanticNodeId& form,
                              const SemanticNodeId& control,
                              std::vector<SemanticEdge>* edges) {
  // CONTAINS is the projection relationship: a SECTION rooted at this form
  // walks it to retain the control. OWNS is the identity relationship: unlike
  // generic DOM or accessibility containment, it states that Blink assigned
  // this exact WebFormControlElement to this exact WebFormElement. Keep both;
  // asking either relationship to carry both meanings makes an AX group or a
  // DOM fieldset indistinguishable from a form root.
  edges->push_back(
      MakeFormRelationshipEdge(frame_id, form, control, EdgeType::kContains));
  edges->push_back(
      MakeFormRelationshipEdge(frame_id, form, control, EdgeType::kOwns));
}

FieldEvidence MakeFormEvidence(SemanticField field,
                               SourceKind source_kind,
                               std::string source_locator,
                               Transformation transformation,
                               uint32_t rule_version) {
  return FieldEvidence(field, source_kind, std::move(source_locator),
                       rule_version, transformation);
}

}  // namespace

std::string AuthoredAttribute(const blink::WebElement& element,
                              const char* name) {
  return Utf8(element.GetAttribute(blink::WebString::FromUtf8(name)));
}

std::string FormSemanticsOf(const blink::WebFormElement& form) {
  if (form.IsNull()) {
    return std::string();
  }
  return base::ToLowerASCII(base::JoinString(
      {AuthoredAttribute(form, "name"), AuthoredAttribute(form, "id"),
       AuthoredAttribute(form, "autocomplete"),
       AuthoredAttribute(form, "action")},
      " "));
}

FormControlEmitter::FormControlEmitter(ExtractionContext& context,
                                       AdapterResult& result,
                                       std::set<int64_t>& covered,
                                       RendererContentTrust document_trust,
                                       ContentSignalMask frame_signals,
                                       bool insecure_context,
                                       uint32_t rule_version,
                                       bool& saw_prohibited_field)
    : context_(context),
      result_(result),
      covered_(covered),
      document_trust_(document_trust),
      frame_signals_(frame_signals),
      insecure_context_(insecure_context),
      rule_version_(rule_version),
      saw_prohibited_field_(saw_prohibited_field) {}

bool FormControlEmitter::Emit(const blink::WebFormControlElement& control,
                              const std::string& form_semantics,
                              const std::optional<SemanticNodeId>& parent_form,
                              const ChallengeSignals& challenge_signals) {
  if (control.IsNull()) {
    return true;
  }
  const int64_t control_key = control.GetDomNodeId();
  if (!covered_->insert(control_key).second) {
    return true;
  }
  if (!context_->ledger->ChargeNode()) {
    context_->ledger->NoteOmittedNode(/*could_change_answer=*/true);
    return false;
  }

  const std::string control_type = ControlTypeOf(control);
  const FieldDescriptor field = DescribeField(
      control, control_type, form_semantics, *context_, insecure_context_);
  const ProhibitedCategory category =
      context_->prohibited_value_filter.CategoryOf(field);
  const FieldChallenge field_challenge =
      ClassifyFieldChallenge(control, field, category, challenge_signals);
  const ChallengeKind challenge = field_challenge.kind;
  const SemanticNodeId control_id =
      context_->store->AllocateOrLookup(context_->store->MakeKey(
          SemanticGraphStore::IdentitySpace::kDom, control_key));
  if (category != ProhibitedCategory::kNone) {
    *saw_prohibited_field_ = true;
    SemanticNode placeholder =
        context_->prohibited_value_filter.BuildStructuralPlaceholder(
            control_id, context_->store->frame_id(), field);
    placeholder.challenge_kind = challenge;
    if (category == ProhibitedCategory::kOneTimeCode) {
      placeholder.states.push_back(control.IsEnabled() ? NodeState::kEnabled
                                                       : NodeState::kDisabled);
      placeholder.states.push_back(control.IsReadOnly() ? NodeState::kReadOnly
                                                        : NodeState::kEditable);
      if (control.IsFocusable()) {
        placeholder.actions.push_back(ActionKind::kFocus);
      }
      if (control.IsEnabled() && !control.IsReadOnly() && !field.hidden) {
        placeholder.actions.push_back(ActionKind::kSetText);
      }
      placeholder.actions.push_back(ActionKind::kScrollIntoView);
    }
    content_metadata::ApplyNodeContext(&placeholder, document_trust_,
                                       frame_signals_);

    SemanticGraphStore::LiveNode live;
    live.node_id = control_id;
    live.display_label = placeholder.name.value_or(std::string());
    // A one-time code is asked of the person on a sheet, which names it as
    // the page does; its placeholder carries no name for the graph.
    if (category == ProhibitedCategory::kOneTimeCode) {
      live.display_label = PersonFacingControlLabel(control, *context_);
    }
    live.dom_key = context_->store->MakeKey(
        SemanticGraphStore::IdentitySpace::kDom, control_key);
    live.role = placeholder.role;
    live.actions = placeholder.actions;
    live.sensitivity = placeholder.sensitivity;
    live.content_trust = placeholder.content_trust;
    live.challenge_kind = placeholder.challenge_kind;
    live.challenge_dom_node_id = field_challenge.presentation_dom_node_id;
    live.states = placeholder.states;
    live.form_semantics_authoritative = true;
    context_->store->UpsertLiveNode(std::move(live));

    if (parent_form.has_value()) {
      AppendFormOwnershipEdges(context_->store->frame_id(), parent_form.value(),
                               control_id, &result_->edges);
    }
    result_->nodes.push_back(std::move(placeholder));
    return true;
  }

  SemanticNode node;
  node.node_id = control_id;
  node.frame_id = context_->store->frame_id();
  node.role = RoleForControlType(control_type);
  node.sensitivity = context_->sensitivity_classifier.Classify(field, category);
  node.challenge_kind = challenge;
  if (challenge == ChallengeKind::kImage) {
    node.sensitivity = Sensitivity::kChallengeResponse;
  }
  content_metadata::ApplyNodeContext(&node, document_trust_, frame_signals_);

  const std::string locator =
      base::StringPrintf("form-control/%" PRId64, control_key);
  node.sources.push_back(SourceKind::kFormControl);
  node.evidence.push_back(
      MakeFormEvidence(SemanticField::kRole, SourceKind::kFormControl, locator,
                       Transformation::kNone, rule_version_));
  node.evidence.push_back(
      MakeFormEvidence(SemanticField::kSensitivity, SourceKind::kFormControl,
                       locator, Transformation::kInferred, rule_version_));
  node.evidence.push_back(
      MakeFormEvidence(SemanticField::kChallengeKind, SourceKind::kFormControl,
                       locator, Transformation::kInferred, rule_version_));

  // Accessibility and form-control identities deliberately occupy separate
  // namespaces. The identity module joins only Blink's exact views and owns
  // the narrow label witness a SECTION needs; a prohibited control returned
  // above and can never reach this path.
  if (!EnrichFormControlIdentity(control, *context_, rule_version_, node,
                                 *result_)) {
    return false;
  }

  node.attributes.emplace_back(AttributeKey::kInputType, control_type);
  if (!field.autocomplete_token.empty()) {
    node.attributes.emplace_back(AttributeKey::kAutocompleteToken,
                                 field.autocomplete_token);
  }
  node.states.push_back(control.IsEnabled() ? NodeState::kEnabled
                                            : NodeState::kDisabled);
  node.states.push_back(control.IsReadOnly() ? NodeState::kReadOnly
                                             : NodeState::kEditable);
  if (!AuthoredAttribute(control, "required").empty()) {
    node.states.push_back(NodeState::kRequired);
  }
  if (field.hidden) {
    node.states.push_back(NodeState::kNotVisible);
  }
  if (control_type == "checkbox" || control_type == "radio") {
    AppendCurrentCheckedState(control, &node.states);
  }
  node.evidence.push_back(
      MakeFormEvidence(SemanticField::kStates, SourceKind::kFormControl,
                       locator, Transformation::kNone, rule_version_));
  if (control.IsFocusable()) {
    node.actions.push_back(ActionKind::kFocus);
  }

  const bool enabled = control.IsEnabled();
  const bool writable = enabled && !control.IsReadOnly() && !field.hidden;
  if (writable && form_control_type_names::IsSetTextControl(control_type)) {
    node.actions.push_back(ActionKind::kSetText);
  } else if (writable && control_type == "select-one") {
    node.actions.push_back(ActionKind::kSelectOption);
  } else if (enabled && !field.hidden &&
             (control_type == "checkbox" || control_type == "radio")) {
    node.actions.push_back(ActionKind::kToggle);
  } else if (enabled && !field.hidden && control_type == "submit") {
    node.actions.push_back(ActionKind::kSubmitForm);
  }

  ValueDescriptor value;
  value.kind = ValueKind::kUnknown;
  value.present = false;
  value.redacted = false;
  node.value_descriptor = std::move(value);
  node.actions.push_back(ActionKind::kScrollIntoView);

  SemanticGraphStore::LiveNode live;
  live.node_id = control_id;
  live.display_label = node.name.value_or(std::string());
  // A challenge's answer has no graph label (`MayEmitText`), and the sheet
  // that asks the person for it names it as the page does (decision 0249).
  if (node.sensitivity == Sensitivity::kChallengeResponse) {
    live.display_label = PersonFacingControlLabel(control, *context_);
  }
  live.dom_key = context_->store->MakeKey(
      SemanticGraphStore::IdentitySpace::kDom, control_key);
  live.role = node.role;
  live.actions = node.actions;
  live.sensitivity = node.sensitivity;
  live.content_trust = node.content_trust;
  live.challenge_kind = node.challenge_kind;
  live.challenge_dom_node_id = field_challenge.presentation_dom_node_id;
  live.states = node.states;
  live.form_semantics_authoritative = true;
  context_->store->UpsertLiveNode(std::move(live));

  if (parent_form.has_value()) {
    AppendFormOwnershipEdges(context_->store->frame_id(), parent_form.value(),
                             control_id, &result_->edges);
  }
  result_->nodes.push_back(std::move(node));
  return true;
}

}  // namespace taffy::form_schema_internal
