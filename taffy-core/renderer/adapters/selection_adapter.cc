// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/selection_adapter.h"

#include <inttypes.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "taffy/renderer/adapters/bounded_web_text.h"
#include "taffy/renderer/adapters/form_control_type_names.h"
#include "taffy/renderer/content_metadata.h"
#include "third_party/blink/public/mojom/forms/form_control_type.mojom-shared.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_ax_context.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"

// VERIFY AT SP-04 - selection is where the public Blink surface has the most
// spellings and where being wrong leaks a secret rather than losing a
// feature:
//
//   * blink::WebLocalFrame::HasSelection() and SelectionAsText(). Confirm
//     that SelectionAsText() returns the DOCUMENT selection and not the
//     contents of a focused editable; if it can return the latter, the
//     prohibited-control check below stops being a guard and becomes the only
//     guard, and it must then run for every editable ancestor rather than for
//     the focused element alone.
//
//   * blink::WebAXObject::Selection(...) - the out-parameter form
//     RenderAccessibilityImpl uses to fill ui::AXTreeData's sel_anchor_* and
//     sel_focus_* fields. If the pin has moved to a WebAXSelection-returning
//     form, only ReadSelection() below changes; everything after it works
//     from two WebAXObjects.
//
//   * blink::WebDocument::FocusedElement() and
//     blink::WebElement::DynamicTo<blink::WebFormControlElement>().
//
//   * Whether a selection can span a shadow boundary such that the anchor and
//     the focus have no common ancestor in the accessibility tree. If it can,
//     the common-ancestor walk below returns nothing and this adapter reports
//     partial coverage, which is the safe outcome - but the corpus fixture
//     that proves it belongs in the shadow family.

namespace taffy {

namespace {

constexpr uint32_t kSelectionRuleVersion = 1;
constexpr char kAdapterName[] = "selection";

// The two ends of the user's selection, as accessibility objects. Both may be
// detached, which is a selection this adapter cannot describe rather than an
// error.
struct SelectionEnds {
  blink::WebAXObject anchor;
  blink::WebAXObject focus;
  bool valid = false;
};

SelectionEnds ReadSelection(const blink::WebAXObject& root) {
  SelectionEnds ends;
  // Confirmed at the 152 pin: WebAXObject::Selection() still fills the
  // anchor/focus pair by out-parameter, but it now leads with
  // `bool& is_selection_backward`. The anchor is the selection's base and the
  // focus its extent - unnormalised - and the new flag is exactly
  // `base > extent` (BlinkAXTreeSource::Selection). The coverage walk below
  // turns `inside` on at whichever end it reaches first, so it is already
  // direction-agnostic and this adapter has no use for the flag; it is read
  // into a local and dropped here rather than plumbed somewhere a reader would
  // think it was consulted.
  bool is_selection_backward = false;
  int anchor_offset = 0;
  int focus_offset = 0;
  ax::mojom::TextAffinity anchor_affinity =
      ax::mojom::TextAffinity::kDownstream;
  ax::mojom::TextAffinity focus_affinity = ax::mojom::TextAffinity::kDownstream;
  root.Selection(is_selection_backward, ends.anchor, anchor_offset,
                 anchor_affinity, ends.focus, focus_offset, focus_affinity);
  // An invalid selection comes back as two null objects, which read as
  // detached: that is a selection this adapter cannot describe, not an error.
  ends.valid = !ends.anchor.IsDetached() && !ends.focus.IsDetached();
  return ends;
}

// Ancestor chain from `object` up to the root, nearest first, bounded by the
// depth budget so a pathological tree cannot make this unbounded.
std::vector<blink::WebAXObject> AncestorChain(const blink::WebAXObject& object,
                                              uint32_t max_depth) {
  std::vector<blink::WebAXObject> chain;
  blink::WebAXObject current = object;
  while (!current.IsDetached() && chain.size() < max_depth) {
    chain.push_back(current);
    current = current.ParentObject();
  }
  return chain;
}

// The deepest object that contains both ends. Returned detached when the two
// ends share no ancestor, which the caller reports as partial coverage rather
// than guessing at a container.
blink::WebAXObject CommonAncestor(const blink::WebAXObject& anchor,
                                  const blink::WebAXObject& focus,
                                  uint32_t max_depth) {
  const std::vector<blink::WebAXObject> anchor_chain =
      AncestorChain(anchor, max_depth);
  const std::vector<blink::WebAXObject> focus_chain =
      AncestorChain(focus, max_depth);
  for (const blink::WebAXObject& candidate : anchor_chain) {
    for (const blink::WebAXObject& other : focus_chain) {
      if (candidate.Equals(other)) {
        return candidate;
      }
    }
  }
  return blink::WebAXObject();
}

// Describes the focused control well enough to ask whether reading the
// selection would read a secret. Deliberately assembled before any text is
// touched.
FieldDescriptor DescribeFocusedControl(const blink::WebDocument& document,
                                       const ExtractionContext& context) {
  FieldDescriptor field;
  field.in_cross_origin_frame = context.cross_origin_frame();
  field.insecure_context = !document.IsSecureContext();
  field.policy_floor = context.policy_floor();

  const blink::WebElement focused = document.FocusedElement();
  if (focused.IsNull()) {
    return field;
  }
  field.aria_role = base::ToLowerASCII(
      focused.GetAttribute(blink::WebString::FromUtf8("role")).Utf8());
  field.authored_name = base::ToLowerASCII(
      focused.GetAttribute(blink::WebString::FromUtf8("name")).Utf8());
  field.autocomplete_token = base::ToLowerASCII(
      focused.GetAttribute(blink::WebString::FromUtf8("autocomplete")).Utf8());
  field.label_text = base::ToLowerASCII(
      focused.GetAttribute(blink::WebString::FromUtf8("aria-label")).Utf8());

  const blink::WebFormControlElement control =
      focused.DynamicTo<blink::WebFormControlElement>();
  if (control.IsNull()) {
    return field;
  }
  // Confirmed at the 152 pin: WebInputElement::IsPasswordFieldForAutofill() is
  // gone, and its exact replacement is
  // FormControlTypeForAutofill() == kInputPassword. That accessor returns
  // kInputPassword for any text field whose HTMLInputElement reports
  // HasBeenPasswordField(), which is the "page disguised a password field as
  // type=text" signal this gate exists to catch - so the check is the same
  // strength as before, not a fallback to the type attribute. It also means
  // the WebInputElement downcast is no longer needed: only an input can ever
  // report kInputPassword.
  const blink::mojom::FormControlType control_type =
      control.FormControlTypeForAutofill();
  field.control_type = form_control_type_names::ControlTypeString(control_type);
  field.blink_reports_password_field =
      control_type == blink::mojom::FormControlType::kInputPassword;
  return field;
}

}  // namespace

SelectionAdapter::SelectionAdapter() = default;
SelectionAdapter::~SelectionAdapter() = default;

AdapterKind SelectionAdapter::kind() const {
  return AdapterKind::kSelection;
}

std::string_view SelectionAdapter::name() const {
  return kAdapterName;
}

uint32_t SelectionAdapter::extraction_rule_version() const {
  return kSelectionRuleVersion;
}

bool SelectionAdapter::annotates_existing_nodes() const {
  return true;
}

AdapterResult SelectionAdapter::Run(ExtractionContext& context) {
  AdapterResult result;

  const blink::WebDocument document = context.frame->GetDocument();
  if (document.IsNull()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-document");
    return result;
  }
  if (!context.frame->HasSelection()) {
    // No selection is not a failure and not an empty answer: it is this
    // adapter having nothing to say about this document right now. A request
    // scoped to the selection gets UNSUPPORTED from the endpoint, which is
    // what protocol section 6.2 asks for.
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-selection");
    return result;
  }
  if (!context.accumulated) {
    // Nothing to annotate. Reporting a selection with no nodes in it would
    // be a result that looks whole and is not.
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-nodes-to-annotate");
    return result;
  }

  const SelectionLimits& limits = context.limits->selection();
  const ContentSignalLimits& signal_limits = context.limits->content_signals();
  const RendererContentTrust document_trust =
      content_metadata::DocumentTrust(context.cross_origin_frame());
  const ContentSignalMask frame_signals = content_metadata::ContextSignals(
      /*hidden_by_style=*/false, /*language_mismatch=*/false,
      context.cross_origin_frame());

  // --- The secret gate, before any text is read ---------------------------
  const FieldDescriptor focused = DescribeFocusedControl(document, context);
  const ProhibitedCategory prohibited =
      context.prohibited_value_filter.CategoryOf(focused);

  if (!context.ledger->ChargeNode()) {
    context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
    result.status = AdapterStatus::kIncomplete;
    result.truncation = context.ledger->report();
    return result;
  }

  // The selection region itself has no element, so it lives in the derived
  // identity space and can never be an action target. Ordinal 1: ordinal 0 is
  // the document node.
  const SemanticNodeId region_id = context.store->AllocateOrLookup(
      context.store->MakeKey(SemanticGraphStore::IdentitySpace::kDerived, 1));

  if (prohibited != ProhibitedCategory::kNone) {
    // A caret inside a password field is a selection, and SelectionAsText()
    // would hand over the secret. Nothing is read: the region says a
    // selection exists inside a credential control and stops there
    // (protocol section 9.1).
    SemanticNode placeholder =
        context.prohibited_value_filter.BuildStructuralPlaceholder(
            region_id, context.store->frame_id(), focused);
    placeholder.role = SemanticRole::kRegion;
    placeholder.states.push_back(NodeState::kSelected);

    SemanticGraphStore::LiveNode live;
    live.node_id = region_id;
    live.dom_key =
        context.store->MakeKey(SemanticGraphStore::IdentitySpace::kDerived, 1);
    live.role = placeholder.role;
    live.sensitivity = Sensitivity::kCredential;
    live.content_trust = placeholder.content_trust;
    live.states = placeholder.states;
    context.store->UpsertLiveNode(std::move(live));

    result.nodes.push_back(std::move(placeholder));
    result.warnings.emplace_back(WarningCode::kSensitiveZoneSuppressed,
                                 "selection-inside-credential-control");
    result.truncation = context.ledger->report();
    result.status = AdapterStatus::kIncomplete;
    return result;
  }

  // --- Which described nodes the selection covers -------------------------
  //
  // The context has to be alive for as long as any WebAXObject taken from it,
  // and the document has to be layout-clean before the cache's relation table
  // is built. Without the first, WebAXObject::FromWebDocument DCHECKs on a
  // missing cache; without the second, AXRelationCache DCHECKs on the
  // lifecycle. Both are browser crashes in a build with DCHECKs on, and
  // accessibility_adapter.cc has done it this way from the start — these two
  // lines are what this adapter was missing.
  blink::WebAXContext ax_context(document, ui::kAXModeBasic);
  if (!ax_context.HasActiveDocument()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-active-document");
    return result;
  }
  ax_context.UpdateAXForAllDocuments();

  const blink::WebAXObject root = blink::WebAXObject::FromWebDocument(document);
  if (root.IsDetached()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterFailed, "detached-root");
    return result;
  }

  const SelectionEnds ends = ReadSelection(root);
  std::vector<SemanticNodeId> covered;
  // Keep traversal order in `covered` for deterministic edges, and a
  // separate membership index so a large selection does not turn duplicate
  // suppression and the sensitivity pass into quadratic scans.
  std::set<SemanticNodeId> covered_ids;
  bool coverage_partial = false;

  if (!ends.valid) {
    coverage_partial = true;
  } else {
    const blink::WebAXObject ancestor = CommonAncestor(
        ends.anchor, ends.focus, context.limits->snapshot().max_depth());
    if (ancestor.IsDetached()) {
      // The two ends share no ancestor this walk could find - a selection
      // spanning a shadow boundary can look like this. Partial coverage, not
      // a guess at a container.
      coverage_partial = true;
    } else {
      // Pre-order walk of the common ancestor, recording the nodes between
      // the anchor and the focus in document order. Iterative with an
      // explicit stack: recursion over a page's accessibility tree is a
      // stack-depth attack a hostile fixture will try.
      struct Entry {
        blink::WebAXObject object;
        uint32_t depth;
      };
      std::vector<Entry> stack;
      stack.push_back({ancestor, 0u});
      bool inside = ends.anchor.Equals(ends.focus);
      bool finished = false;

      while (!stack.empty() && !finished) {
        const Entry current = stack.back();
        stack.pop_back();
        if (current.object.IsDetached() || !context.ledger->CheckDeadline()) {
          coverage_partial = true;
          break;
        }
        if (!context.ledger->WithinDepth(current.depth)) {
          coverage_partial = true;
          continue;
        }

        const bool is_anchor = current.object.Equals(ends.anchor);
        const bool is_focus = current.object.Equals(ends.focus);
        if (is_anchor || is_focus) {
          if (!inside) {
            inside = true;
          } else {
            finished = true;
          }
        }

        if (inside) {
          const std::optional<SemanticNodeId> node_id =
              context.store->Lookup(context.store->MakeKey(
                  SemanticGraphStore::IdentitySpace::kAccessibility,
                  current.object.AxID()));
          if (node_id.has_value()) {
            if (covered.size() >= limits.max_nodes()) {
              coverage_partial = true;
              finished = true;
            } else if (covered_ids.insert(node_id.value()).second) {
              covered.push_back(node_id.value());
            }
          } else {
            // The selection covers something no producing adapter described.
            // Reported, never minted: allocating an identity here would
            // create a node the action path could resolve and nothing could
            // explain.
            coverage_partial = true;
          }
        }

        // Children pushed in reverse so the pop order is document order.
        const unsigned child_count = current.object.ChildCount();
        const size_t queue_slots =
            limits.max_nodes() > stack.size()
                ? static_cast<size_t>(limits.max_nodes()) - stack.size()
                : 0u;
        const unsigned children_to_queue =
            static_cast<unsigned>(std::min<size_t>(child_count, queue_slots));
        if (children_to_queue < child_count) {
          coverage_partial = true;
        }
        for (unsigned i = children_to_queue; i > 0; --i) {
          const blink::WebAXObject child = current.object.ChildAt(i - 1);
          if (!child.IsDetached()) {
            stack.push_back({child, current.depth + 1});
          }
        }
      }
    }
  }

  // --- Sensitivity, then text, in that order ------------------------------
  // The selection is at least as sensitive as the most sensitive thing in it.
  // Computing this before reading the text is what makes MayEmitText() a gate
  // rather than a post-filter.
  Sensitivity sensitivity = SensitivityClassifier::Stricter(
      Sensitivity::kNotSensitive, context.policy_floor());
  if (context.cross_origin_frame()) {
    sensitivity = Sensitivity::kUnknownSensitive;
  }
  for (const SemanticNode& node : context.accumulated->nodes) {
    if (!context.ledger->CheckDeadline()) {
      // The rest of the selected nodes could be stricter. Unknown is the only
      // safe classification when the pass cannot finish, and it prevents the
      // text read below.
      sensitivity = Sensitivity::kUnknownSensitive;
      coverage_partial = true;
      break;
    }
    if (covered_ids.contains(node.node_id)) {
      sensitivity =
          SensitivityClassifier::Stricter(sensitivity, node.sensitivity);
    }
  }

  SemanticNode region;
  region.node_id = region_id;
  region.frame_id = context.store->frame_id();
  region.role = SemanticRole::kRegion;
  // Mark the region itself as selected. Projection can then keep exactly the
  // region and the covered nodes instead of retaining every generic REGION in
  // the document as a guess at which one represents the selection.
  region.states.push_back(NodeState::kSelected);
  region.sensitivity = sensitivity;
  region.confidence = 1.0;
  content_metadata::ApplyNodeContext(&region, document_trust, frame_signals);
  region.sources.push_back(SourceKind::kAccessibility);
  region.evidence.push_back(
      MakeEvidence(SemanticField::kStates, SourceKind::kAccessibility,
                   "selection/range", Transformation::kNone));

  bool text_withheld = false;
  if (MayEmitText(sensitivity, NodeTextClass::kContent)) {
    BoundedWebText bounded;
    if (coverage_partial || !context.ledger->CheckDeadline()) {
      coverage_partial = true;
      text_withheld = true;
    } else {
      bounded = BoundWebTextLazily(
          [&context]() { return context.frame->SelectionAsText(); },
          *context.ledger, limits.max_text_bytes());
    }
    std::string text = std::move(bounded.text);
    if (context.prohibited_value_filter.LooksLikeHighRiskIdentifier(text)) {
      // A user can select a card number off a page. Dropped whole; a mask
      // would still state the length.
      text.clear();
      text_withheld = true;
    }
    if (!text.empty()) {
      TextRun run;
      run.text = std::move(text);
      run.source_kind = SourceKind::kAccessibility;
      run.sensitivity = sensitivity;
      run.truncated = bounded.truncated;
      content_metadata::LabelTextRun(&run, &region, document_trust,
                                     frame_signals, signal_limits);
      region.text_runs.push_back(std::move(run));
      region.evidence.push_back(
          MakeEvidence(SemanticField::kTextRuns, SourceKind::kAccessibility,
                       "selection/text", Transformation::kNone));
    }
  } else {
    text_withheld = true;
  }

  SemanticGraphStore::LiveNode live;
  live.node_id = region_id;
  live.dom_key =
      context.store->MakeKey(SemanticGraphStore::IdentitySpace::kDerived, 1);
  live.role = region.role;
  live.sensitivity = sensitivity;
  live.content_trust = region.content_trust;
  live.states = region.states;
  context.store->UpsertLiveNode(std::move(live));
  result.nodes.push_back(std::move(region));

  // --- Annotations and edges ----------------------------------------------
  for (const SemanticNodeId& node_id : covered) {
    if (!context.ledger->CheckDeadline()) {
      coverage_partial = true;
      break;
    }
    NodeAnnotation annotation;
    annotation.node_id = node_id;
    annotation.states.push_back(NodeState::kSelected);
    annotation.evidence.push_back(MakeEvidence(
        SemanticField::kStates, SourceKind::kAccessibility,
        base::StringPrintf("selection/covers/%s", node_id.value().c_str()),
        Transformation::kNone));
    result.annotations.push_back(std::move(annotation));

    SemanticEdge edge;
    edge.from_frame_id = context.store->frame_id();
    edge.from_node_id = region_id;
    edge.to_frame_id = context.store->frame_id();
    edge.to_node_id = node_id;
    edge.relationship = EdgeType::kContains;
    result.edges.push_back(std::move(edge));
  }

  result.truncation = context.ledger->report();
  if (text_withheld) {
    result.warnings.emplace_back(WarningCode::kSensitiveZoneSuppressed,
                                 "selection-text-withheld");
  }
  if (coverage_partial) {
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "selection-coverage-partial");
  }
  result.status =
      (coverage_partial || text_withheld || result.truncation.truncated)
          ? AdapterStatus::kIncomplete
          : AdapterStatus::kOk;
  return result;
}

}  // namespace taffy
