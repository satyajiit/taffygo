// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/accessibility_adapter.h"

#include <inttypes.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/stringprintf.h"
#include "taffy/renderer/adapters/accessibility_relationships.h"
#include "taffy/renderer/content_metadata.h"
#include "third_party/blink/public/web/web_ax_context.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"
#include "url/gurl.h"
#include "url/origin.h"

// VERIFY AT SP-04 - this adapter is the one most likely to need reshaping
// against the real API, and the one whose cost most needs measuring:
//   * blink::WebAXContext construction and its ui::AXMode argument, plus
//     whether UpdateAXForAllDocuments() (or its current name) must be called
//     before the tree is readable. Getting this wrong yields a stale tree
//     rather than an error, which is the worst failure shape available.
//   * blink::WebAXObject::Serialize(ui::AXNodeData*, ui::AXMode) - the
//     serialization entry point used by RenderAccessibilityImpl. If the
//     signature differs, everything below still works from AXNodeData; only
//     the call changes.
//   * The cost of forcing an accessibility tree on a page that has none, on
//     the supported-device floor. If it is too expensive, this adapter
//     becomes opt-in per request and the DOM adapter has to grow its own
//     composed-tree traversal - which is a much larger change and should be
//     decided with numbers, not guessed at now. Related: OD-030.
//   * Whether kExposeInlineTextBoxes-class modes can be left off. They are
//     not needed here and they multiply node counts.

namespace taffy {

namespace {

constexpr uint32_t kAxRuleVersion = 2;
constexpr char kAdapterName[] = "accessibility";

SemanticRole RoleFromAx(ax::mojom::Role role) {
  switch (role) {
    case ax::mojom::Role::kRootWebArea:
      return SemanticRole::kDocument;
    case ax::mojom::Role::kHeading:
      return SemanticRole::kHeading;
    case ax::mojom::Role::kParagraph:
      return SemanticRole::kParagraph;
    case ax::mojom::Role::kList:
      return SemanticRole::kList;
    case ax::mojom::Role::kListItem:
      return SemanticRole::kListItem;
    case ax::mojom::Role::kTable:
      return SemanticRole::kTable;
    case ax::mojom::Role::kRow:
      return SemanticRole::kTableRow;
    case ax::mojom::Role::kCell:
    case ax::mojom::Role::kColumnHeader:
    case ax::mojom::Role::kRowHeader:
      return SemanticRole::kTableCell;
    case ax::mojom::Role::kLink:
      return SemanticRole::kLink;
    case ax::mojom::Role::kButton:
      return SemanticRole::kButton;
    case ax::mojom::Role::kSearchBox:
      return SemanticRole::kSearchField;
    case ax::mojom::Role::kTextField:
    case ax::mojom::Role::kTextFieldWithComboBox:
      return SemanticRole::kTextField;
    case ax::mojom::Role::kCheckBox:
      return SemanticRole::kCheckbox;
    case ax::mojom::Role::kRadioButton:
      return SemanticRole::kRadio;
    case ax::mojom::Role::kComboBoxSelect:
    case ax::mojom::Role::kListBox:
      return SemanticRole::kSelect;
    case ax::mojom::Role::kListBoxOption:
    case ax::mojom::Role::kMenuListOption:
      return SemanticRole::kOption;
    case ax::mojom::Role::kImage:
      return SemanticRole::kImage;
    case ax::mojom::Role::kVideo:
    case ax::mojom::Role::kAudio:
      return SemanticRole::kMedia;
    case ax::mojom::Role::kMain:
    case ax::mojom::Role::kRegion:
    case ax::mojom::Role::kNavigation:
    case ax::mojom::Role::kArticle:
    case ax::mojom::Role::kComplementary:
    case ax::mojom::Role::kContentInfo:
    case ax::mojom::Role::kBanner:
      return SemanticRole::kRegion;
    default:
      // An unmapped accessibility role is an unknown role, and an unmapped
      // role that is focusable is an unknown *interactive* role. Neither is
      // guessed at: an action policy can refuse an unknown interactive node,
      // but it cannot refuse a node we mislabelled as a button.
      return SemanticRole::kUnknownContent;
  }
}

// `object` is the same node `data` was serialized from: focus is no longer a
// serialized state, so it has to be asked of the live object. See the kFocused
// comment below.
void AppendStates(const blink::WebAXObject& object,
                  const ui::AXNodeData& data,
                  SemanticNode& node) {
  // Absence means unknown, so only states that are positively determined are
  // appended (see NodeState in semantic_graph.h).
  // Both a state and its negation exist, so this says what was determined
  // rather than leaving the answer to be inferred from silence.
  node.states.push_back(data.IsInvisibleOrIgnored() ? NodeState::kNotVisible
                                                    : NodeState::kVisible);
  // Confirmed at the 152 pin: ax::mojom::State has no kFocused. Focus stopped
  // being a per-node serialized state and became tree-level -
  // BlinkAXTreeSource fills ui::AXTreeData::focus_id from the cache's focused
  // object - so AXNodeData alone cannot answer it. WebAXObject::IsFocused() is
  // the same predicate the old state was set from (AXNodeObject::IsFocused():
  // this object is the document's focused element, or it is the web area of a
  // frame that is focused and active), so asking the live object here keeps the
  // meaning rather than approximating it. Deliberately not
  // WebAXObject::FromWebDocumentFocused(): that returns the root web area when
  // nothing is focused, which would assert kFocused on the document node of an
  // unfocused frame.
  if (object.IsFocused()) {
    node.states.push_back(NodeState::kFocused);
  }
  if (data.HasState(ax::mojom::State::kExpanded)) {
    node.states.push_back(NodeState::kExpanded);
  }
  if (data.HasState(ax::mojom::State::kCollapsed)) {
    node.states.push_back(NodeState::kCollapsed);
  }
  if (data.HasState(ax::mojom::State::kRequired)) {
    node.states.push_back(NodeState::kRequired);
  }
  if (data.HasState(ax::mojom::State::kEditable)) {
    node.states.push_back(NodeState::kEditable);
  }

  switch (data.GetRestriction()) {
    case ax::mojom::Restriction::kNone:
      node.states.push_back(NodeState::kEnabled);
      break;
    case ax::mojom::Restriction::kReadOnly:
      node.states.push_back(NodeState::kReadOnly);
      break;
    case ax::mojom::Restriction::kDisabled:
      node.states.push_back(NodeState::kDisabled);
      break;
  }

  switch (data.GetCheckedState()) {
    case ax::mojom::CheckedState::kTrue:
      node.states.push_back(NodeState::kChecked);
      break;
    case ax::mojom::CheckedState::kFalse:
      node.states.push_back(NodeState::kUnchecked);
      break;
    case ax::mojom::CheckedState::kMixed:
      node.states.push_back(NodeState::kMixed);
      break;
    case ax::mojom::CheckedState::kNone:
      // Not a checkable control. Neither member is asserted.
      break;
  }
  if (data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected)) {
    node.states.push_back(NodeState::kSelected);
  }
  // Confirmed at the 152 pin: busy moved out of ax::mojom::State and is now
  // ax::mojom::BoolAttribute::kBusy, which AXObject::Serialize adds only when
  // the node is busy - so a missing attribute reads as false, same as the
  // missing state did.
  if (data.GetBoolAttribute(ax::mojom::BoolAttribute::kBusy)) {
    node.states.push_back(NodeState::kBusy);
  }
  if (data.GetInvalidState() != ax::mojom::InvalidState::kNone &&
      data.GetInvalidState() != ax::mojom::InvalidState::kFalse) {
    node.states.push_back(NodeState::kInvalid);
  }
}

}  // namespace

AccessibilityAdapter::AccessibilityAdapter() = default;
AccessibilityAdapter::~AccessibilityAdapter() = default;

AdapterKind AccessibilityAdapter::kind() const {
  return AdapterKind::kAccessibility;
}

std::string_view AccessibilityAdapter::name() const {
  return kAdapterName;
}

uint32_t AccessibilityAdapter::extraction_rule_version() const {
  return kAxRuleVersion;
}

AdapterResult AccessibilityAdapter::Run(ExtractionContext& context) {
  AdapterResult result;

  const blink::WebDocument document = context.frame->GetDocument();
  if (document.IsNull()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-document");
    return result;
  }

  // The narrowest mode that answers the question: names, roles, and states.
  // Inline text boxes and HTML attributes are deliberately not requested -
  // nothing here reads them and both multiply the cost.
  blink::WebAXContext ax_context(document, ui::kAXModeBasic);
  if (!ax_context.HasActiveDocument()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-active-document");
    return result;
  }
  ax_context.UpdateAXForAllDocuments();

  const ContentSignalLimits& signal_limits = context.limits->content_signals();
  const RendererContentTrust document_trust =
      content_metadata::DocumentTrust(context.cross_origin_frame());

  const blink::WebAXObject root = blink::WebAXObject::FromWebDocument(document);
  if (root.IsDetached()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterFailed, "detached-root");
    return result;
  }

  struct Entry {
    blink::WebAXObject object;
    uint32_t depth;
    SemanticNodeId parent_id;
    bool has_parent;
    // True for everything below a text-entry control. See the check that
    // consumes it, below the serialization, for why the subtree is dropped.
    bool inside_text_control;
  };
  std::vector<Entry> stack;
  stack.push_back({root, 0u, SemanticNodeId(), false, false});

  AccessibilityRelationships relationships(context);
  bool child_queue_truncated = false;

  while (!stack.empty()) {
    const Entry current = stack.back();
    stack.pop_back();

    if (!context.ledger->CheckDeadline() || context.ledger->exhausted()) {
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
      break;
    }
    if (!context.ledger->WithinDepth(current.depth)) {
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
      continue;
    }
    if (current.object.IsDetached()) {
      // The tree moved underneath the walk. That is a graph change, not an
      // error: record it so the revision advances and the caller knows the
      // result describes a page that has since moved.
      context.store->NoteChange(
          SemanticGraphStore::ChangeClass::kAdapterInvalidated);
      continue;
    }

    ui::AXNodeData data;
    current.object.Serialize(&data, ui::kAXModeBasic);

    if (current.inside_text_control) {
      // A text control's VALUE, arriving as the accessible name of the
      // static-text object Blink puts inside the control's inner editor.
      //
      // MEASURED on 2026-08-20 against
      // `<input type="search" name="search" value="desk lamp">` with no
      // label, aria-label, placeholder or title: the control itself
      // serializes as role kSearchBox with an empty name and
      // `kValue = "desk lamp"`, and a static-text child of it serializes with
      // `kName = "desk lamp"`. Reading names indiscriminately therefore reads
      // every form control value on the page - ordinary and credential alike
      // - and ships them in `name`, a field nothing downstream inspects for a
      // value.
      //
      // The interior of a control is not this adapter's to describe. The form
      // schema adapter is the component that describes controls; it reads
      // label, aria-label and placeholder, and never a value (protocol
      // section 9.1). Dropping the subtree here is that same rule applied to
      // the path that would otherwise go around it, and it drops nothing
      // else: the control's own node is still emitted, by the entry above
      // this one, with its role, states and actions intact.
      continue;
    }

    const SemanticRole role = RoleFromAx(data.role);
    const bool focusable = data.HasState(ax::mojom::State::kFocusable);

    if (!context.ledger->ChargeNode()) {
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
      break;
    }

    // The accessibility object is keyed by the DOM node it belongs to, so the
    // DOM and accessibility adapters converge on one semantic node instead of
    // producing two nodes for one thing. Where they disagree about a field,
    // both pieces of evidence are kept (protocol section 7.5).
    //
    // Keyed in the accessibility identity space, never in the DOM one:
    // accessibility ids and DOM node ids are unrelated sequences of small
    // integers and would otherwise merge unrelated nodes.
    //
    // VERIFY AT SP-04: whether joining the DOM and accessibility views of the
    // same element onto one semantic node is worth the cost. Doing it needs a
    // DOM node accessor on WebAXObject and a stable rule for objects that
    // have no DOM node (generated list markers, anonymous blocks). Until that
    // is measured the two adapters contribute separate nodes joined by
    // SAME_ENTITY_AS at the browser broker, which is more honest than a join
    // that is sometimes wrong.
    const int64_t identity_key = current.object.AxID();
    const SemanticNodeId node_id =
        context.store->AllocateOrLookup(context.store->MakeKey(
            SemanticGraphStore::IdentitySpace::kAccessibility, identity_key));

    SemanticNode node;
    node.node_id = node_id;
    node.frame_id = context.store->frame_id();
    node.role = role == SemanticRole::kUnknownContent && focusable
                    ? SemanticRole::kUnknownInteractive
                    : role;
    node.confidence = 1.0;
    // Everything this adapter produces arrived through the composed tree,
    // which is the whole reason it is the shadow path (protocol section 8.2).
    // Recording it per node lets a test assert that shadow-only content in
    // the graph came from a capability Chromium already has and not from a
    // traversal written for AI.
    node.projection_path = ProjectionPath::kComposedTreeThroughAccessibility;

    const std::string locator = base::StringPrintf("ax/%" PRId64, identity_key);
    node.sources.push_back(SourceKind::kAccessibility);
    node.evidence.push_back(MakeEvidence(SemanticField::kRole,
                                         SourceKind::kAccessibility, locator,
                                         Transformation::kNone));
    node.evidence.push_back(MakeEvidence(SemanticField::kStates,
                                         SourceKind::kAccessibility, locator,
                                         Transformation::kNone));

    AppendStates(current.object, data, node);
    const ContentSignalMask content_context = content_metadata::ContextSignals(
        /*hidden_by_style=*/data.IsInvisibleOrIgnored(),
        /*language_mismatch=*/false, context.cross_origin_frame());
    content_metadata::ApplyNodeContext(&node, document_trust, content_context);

    // Sensitivity FIRST, then text, so the emission gate is a gate rather
    // than a post-filter. Classifying from the accessible name is safe
    // because the classifier only ever raises: a page that names a region
    // "ordinary" does not make it ordinary.
    const std::string name =
        data.GetStringAttribute(ax::mojom::StringAttribute::kName);
    // A link or a button is named for what it does, so its own words do not
    // classify it; a passage is named by what it says, so they do.
    const bool names_a_control = node.role == SemanticRole::kLink ||
                                 node.role == SemanticRole::kButton;
    node.sensitivity =
        names_a_control
            ? context.sensitivity_classifier.ClassifyControlName(
                  context.cross_origin_frame(), context.policy_floor())
            : context.sensitivity_classifier.ClassifyContentRegion(
                  name, context.cross_origin_frame(), context.policy_floor());

    // A control's own value, read for one purpose and never emitted: a
    // control that names itself from its value - a submit button's caption is
    // the same computation - would otherwise ship that value in `name`. The
    // subtree rule above covers a text control's inner editor; this covers
    // the control's own node.
    const std::string control_value =
        data.GetStringAttribute(ax::mojom::StringAttribute::kValue);
    const bool name_is_the_controls_value =
        !control_value.empty() && name == control_value;

    if (!name.empty() && !name_is_the_controls_value &&
        MayEmitText(node.sensitivity, NodeTextClass::kContent)) {
      std::string bounded = context.ledger->BoundText(name);
      // A page can put a secret in an accessible name, and TalkBack would
      // read it aloud. That does not make it safe to ship to a model.
      if (!context.prohibited_value_filter.LooksLikeHighRiskIdentifier(
              bounded)) {
        node.name = std::move(bounded);
        content_metadata::AddNodeTextSignals(&node, *node.name, content_context,
                                             signal_limits);
        node.evidence.push_back(MakeEvidence(SemanticField::kName,
                                             SourceKind::kAccessibility,
                                             locator, Transformation::kNone));
      }
    }

    const std::string description =
        data.GetStringAttribute(ax::mojom::StringAttribute::kDescription);
    if (!description.empty() && description != control_value &&
        MayEmitText(node.sensitivity, NodeTextClass::kContent)) {
      std::string bounded = context.ledger->BoundText(description);
      if (!context.prohibited_value_filter.LooksLikeHighRiskIdentifier(
              bounded)) {
        node.description = std::move(bounded);
        content_metadata::AddNodeTextSignals(&node, *node.description,
                                             content_context, signal_limits);
        node.evidence.push_back(MakeEvidence(SemanticField::kDescription,
                                             SourceKind::kAccessibility,
                                             locator, Transformation::kNone));
      }
    }

    // Protocol section 11.1: these are observations of what could be attempted,
    // not permission. The endpoint refuses most of them anyway; the browser
    // process is the only place that can say yes.
    if (focusable) {
      node.actions.push_back(ActionKind::kFocus);
    }
    if (role == SemanticRole::kLink || role == SemanticRole::kButton) {
      node.actions.push_back(ActionKind::kActivate);
      const std::string url =
          data.GetStringAttribute(ax::mojom::StringAttribute::kUrl);
      if (!url.empty()) {
        Destination destination;
        destination.url = url;
        // The one bit the model is told to read. `is_cross_origin` is what
        // becomes the word that tells a result which leaves this site from one
        // that does not, and leaving it at its `false` default reported every
        // link on a search results page as staying on the engine. A phone
        // measured what that costs: three searches and ten reads and queries
        // over twenty turns, because opening a result never looked like
        // progress. `DomAdapter` has always computed this; this adapter runs
        // first and spends the shared node budget on a page that size, so on a
        // results page its answer is the only one there is.
        //
        // `opens_new_tab` and `is_download` stay false and are not oversights:
        // neither is derivable from `AXNodeData`, which carries no `target`
        // and no `download` attribute. Stated here so the next reader does not
        // have to work out which of the three was deliberate.
        destination.is_cross_origin =
            url::Origin::Create(GURL(url)) != url::Origin::Create(document.Url());
        node.destination = std::move(destination);
        node.evidence.push_back(MakeEvidence(SemanticField::kDestination,
                                             SourceKind::kAccessibility,
                                             locator, Transformation::kNone));
      }
    }
    node.actions.push_back(ActionKind::kScrollIntoView);

    // Authored relationships, recorded now and resolved after the walk: an
    // aria-labelledby target can appear anywhere in the tree, including
    // before the node that references it (protocol section 7.4).
    relationships.Collect(data, node_id, *context.ledger);

    SemanticGraphStore::LiveNode live;
    live.node_id = node_id;
    live.dom_key = context.store->MakeKey(
        SemanticGraphStore::IdentitySpace::kAccessibility, identity_key);
    live.role = node.role;
    live.actions = node.actions;
    live.sensitivity = node.sensitivity;
    live.content_trust = node.content_trust;
    live.states = node.states;
    live.destination = node.destination;
    context.store->UpsertLiveNode(std::move(live));

    if (current.has_parent) {
      SemanticEdge edge;
      edge.from_frame_id = context.store->frame_id();
      edge.from_node_id = current.parent_id;
      edge.to_frame_id = context.store->frame_id();
      edge.to_node_id = node_id;
      edge.relationship = EdgeType::kContains;
      result.edges.push_back(std::move(edge));
    }
    result.nodes.push_back(std::move(node));

    // Anything below a text-entry control is that control's own value. The
    // flag is sticky down the subtree because Blink nests the inner editor
    // one or more levels deep.
    const bool children_are_inside_a_text_control =
        current.inside_text_control || data.IsTextField();

    const unsigned child_count = current.object.ChildCount();
    const size_t unqueued_node_slots =
        context.ledger->RemainingNodes() > stack.size()
            ? context.ledger->RemainingNodes() - stack.size()
            : 0u;
    const unsigned children_to_queue = static_cast<unsigned>(
        std::min<size_t>(child_count, unqueued_node_slots));
    if (children_to_queue < child_count) {
      child_queue_truncated = true;
      context.ledger->NoteOmittedNodes(child_count - children_to_queue,
                                       /*could_change_answer=*/true);
    }
    // The first children are kept and pushed last-first, so popping yields
    // document order, as the DOM adapter's walk does. This walk pushed them
    // first-first and kept the last ones, so a page arrived bottom to top and
    // a bound cut off its top: on a phone the myAadhaar download page began
    // with its footer and FAQ, a search results page with "More search
    // results", and the projection's budget ran out before the form or the
    // first results (verification report, section 2.48).
    for (unsigned i = children_to_queue; i-- > 0;) {
      const blink::WebAXObject child = current.object.ChildAt(i);
      if (child.IsDetached()) {
        continue;
      }
      stack.push_back({child, current.depth + 1, node_id, true,
                       children_are_inside_a_text_control});
    }
  }

  // Lookup, never allocation: a relation to an undescribed target must not
  // mint an identity the action path could resolve.
  relationships.Resolve(context, result);

  result.truncation = context.ledger->report();
  if (child_queue_truncated) {
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "accessibility-child-fanout-bound");
  }
  // This adapter does NOT claim that it crossed a shadow boundary. It cannot
  // tell: everything it sees arrives through the composed tree, so a shadow
  // host looks like any other subtree from here, and the previous version's
  // "an ignored node with children" heuristic was a guess dressed as a fact.
  // The honest statement needs two facts from two adapters - the DOM walk
  // stopped at a boundary, and content beyond it reached the graph through
  // this path - so SnapshotBuilder makes it, using the per-node projection
  // path recorded above (protocol section 8.2).
  result.status = (result.truncation.truncated || relationships.incomplete() ||
                   child_queue_truncated)
                      ? AdapterStatus::kIncomplete
                      : AdapterStatus::kOk;
  return result;
}

}  // namespace taffy
