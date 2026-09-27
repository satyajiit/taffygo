// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/form_schema_adapter.h"

#include <inttypes.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/stringprintf.h"
#include "taffy/renderer/adapters/form_schema_control.h"
#include "taffy/renderer/content_metadata.h"
#include "third_party/blink/public/web/web_ax_context.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_form_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "ui/accessibility/ax_mode.h"

// VERIFY AT SP-04 - form observation is where the Blink API has moved most
// between milestones, and where being wrong is a privacy bug rather than a
// missing feature:
//   * blink::WebDocument::Forms() and blink::WebFormElement::
//     GetFormControlElements() - names survived to 152; both now return
//     std::vector rather than blink::WebVector.
//   * FormControlTypeForAutofill() returns blink::mojom::FormControlType. The
//     control emitter is the only place the spelling is interpreted.
//   * WebInputElement::IsPasswordFieldForAutofill() is gone; its exact
//     equivalent is FormControlTypeForAutofill() == kInputPassword, which is
//     what Autofill itself uses.
//   * The second pass uses emitted DOM node ids rather than relying on
//     WebFormControlElement::Form(), so standalone controls stay covered.
//   * Blink still exposes no public renderer signal for a password-manager
//     suggestion; the verification list owns that absence.

namespace taffy {
namespace {

constexpr uint32_t kFormRuleVersion = 3;
constexpr char kAdapterName[] = "form-schema";

}  // namespace

FormSchemaAdapter::FormSchemaAdapter() = default;
FormSchemaAdapter::~FormSchemaAdapter() = default;

AdapterKind FormSchemaAdapter::kind() const {
  return AdapterKind::kForms;
}

std::string_view FormSchemaAdapter::name() const {
  return kAdapterName;
}

uint32_t FormSchemaAdapter::extraction_rule_version() const {
  return kFormRuleVersion;
}

AdapterResult FormSchemaAdapter::Run(ExtractionContext& context) {
  AdapterResult result;

  const blink::WebDocument document = context.frame->GetDocument();
  if (document.IsNull()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-document");
    return result;
  }

  // The control emitter asks Blink's accessibility implementation for the
  // standard accessible name. WebAXObject alone does not keep that tree
  // active: without a context, ordinary <label for>, wrapping labels and
  // button contents serialize with empty names even though the controls are
  // otherwise actionable. Keep the narrow basic context alive for this
  // entire extraction and update it before resolving any control.
  blink::WebAXContext ax_context(document, ui::kAXModeBasic);
  if (ax_context.HasActiveDocument()) {
    ax_context.UpdateAXForAllDocuments();
  }

  const FormLimits& limits = context.limits->forms();
  const RendererContentTrust document_trust =
      content_metadata::DocumentTrust(context.cross_origin_frame());
  const ContentSignalMask frame_signals = content_metadata::ContextSignals(
      /*hidden_by_style=*/false, /*language_mismatch=*/false,
      context.cross_origin_frame());
  const bool insecure_context = !document.IsSecureContext();
  bool saw_prohibited_field = false;
  bool form_limit_reached = false;
  bool control_limit_reached = false;

  // The first pass records every emitted control. The second pass uses the
  // same emitter and this set, so controls outside a form cannot acquire a
  // different classification or action rule.
  std::set<int64_t> covered;
  form_schema_internal::FormControlEmitter emit_control(
      context, result, covered, document_trust, frame_signals, insecure_context,
      extraction_rule_version(), saw_prohibited_field);

  uint32_t forms_seen = 0;
  const std::vector<blink::WebFormElement> forms = document.Forms();
  for (const blink::WebFormElement& form : forms) {
    if (context.ledger->exhausted() || !context.ledger->CheckDeadline()) {
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
      break;
    }
    if (form.IsNull()) {
      continue;
    }
    if (forms_seen >= limits.max_forms()) {
      form_limit_reached = true;
      break;
    }
    ++forms_seen;

    const int64_t form_key = form.GetDomNodeId();
    if (!context.ledger->ChargeNode()) {
      break;
    }
    const SemanticNodeId form_id =
        context.store->AllocateOrLookup(context.store->MakeKey(
            SemanticGraphStore::IdentitySpace::kDom, form_key));
    const std::string form_semantics =
        form_schema_internal::FormSemanticsOf(form);

    SemanticNode form_node;
    form_node.node_id = form_id;
    form_node.frame_id = context.store->frame_id();
    form_node.role = SemanticRole::kRegion;
    content_metadata::ApplyNodeContext(&form_node, document_trust,
                                       frame_signals);
    {
      FieldDescriptor form_field;
      form_field.form_semantics = form_semantics;
      form_field.in_cross_origin_frame = context.cross_origin_frame();
      form_field.insecure_context = insecure_context;
      form_field.policy_floor = context.policy_floor();
      const ProhibitedCategory form_category =
          context.prohibited_value_filter.CategoryOf(form_field);
      form_node.sensitivity =
          context.sensitivity_classifier.Classify(form_field, form_category);
    }
    const std::string form_locator =
        base::StringPrintf("form/%" PRId64, form_key);
    form_node.sources.push_back(SourceKind::kDom);
    form_node.evidence.push_back(MakeEvidence(SemanticField::kRole,
                                              SourceKind::kDom, form_locator,
                                              Transformation::kNone));
    Destination form_destination;
    form_destination.url =
        form_schema_internal::AuthoredAttribute(form, "action");
    form_node.destination = std::move(form_destination);
    form_node.evidence.push_back(MakeEvidence(SemanticField::kDestination,
                                              SourceKind::kDom, form_locator,
                                              Transformation::kNone));
    form_node.attributes.emplace_back(AttributeKey::kInputType, "form");

    SemanticGraphStore::LiveNode form_live;
    form_live.node_id = form_id;
    form_live.dom_key = context.store->MakeKey(
        SemanticGraphStore::IdentitySpace::kDom, form_key);
    form_live.role = form_node.role;
    form_live.sensitivity = form_node.sensitivity;
    form_live.content_trust = form_node.content_trust;
    form_live.destination = form_node.destination;
    result.nodes.push_back(std::move(form_node));

    const std::vector<blink::WebFormControlElement> controls =
        form.GetFormControlElements();
    const ChallengeSignals challenge_signals =
        form_schema_internal::ChallengeSignalsOf(form, controls);
    uint32_t controls_seen = 0;
    bool form_controls_complete = true;
    for (const blink::WebFormControlElement& control : controls) {
      if (controls_seen >= limits.max_controls_per_form()) {
        control_limit_reached = true;
        form_controls_complete = false;
        break;
      }
      ++controls_seen;
      if (!emit_control.Emit(control, form_semantics, form_id,
                             challenge_signals)) {
        form_controls_complete = false;
        break;
      }
    }

    // Resolve advertises these opaque children so the browser can ask a
    // person about the complete bounded form rather than only the root node.
    // The list is derived from writable text controls the singular emitter
    // actually accepted; buttons, toggles and selects do not consume one of
    // the surface's eight value rows. A form whose control walk was truncated
    // publishes an engaged empty list, so a previously complete list is not
    // carried forward and no partial form can be drawn.
    std::vector<SemanticNodeId> emitted_fields;
    if (form_controls_complete) {
      emitted_fields.reserve(std::min(controls.size(), covered.size()));
      for (const blink::WebFormControlElement& control : controls) {
        if (control.IsNull() || !covered.contains(control.GetDomNodeId())) {
          continue;
        }
        const std::optional<SemanticNodeId> id = context.store->Lookup(
            context.store->MakeKey(SemanticGraphStore::IdentitySpace::kDom,
                                   control.GetDomNodeId()));
        if (!id.has_value()) {
          continue;
        }
        const SemanticGraphStore::LiveNode* field =
            context.store->FindLive(*id);
        if (field != nullptr &&
            std::ranges::find(field->actions, ActionKind::kSetText) !=
                field->actions.end()) {
          emitted_fields.push_back(*id);
        }
      }
    }
    form_live.form_field_node_ids = std::move(emitted_fields);
    form_live.form_semantics_authoritative = true;
    context.store->UpsertLiveNode(std::move(form_live));
  }

  // A sign-in box with no form element is an ordinary web shape. The walk is
  // iterative so a hostile DOM cannot turn its depth into renderer stack use.
  uint32_t formless_seen = 0;
  bool formless_limit_reached = false;
  const blink::WebElement body = document.Body();
  if (!body.IsNull()) {
    std::vector<std::pair<blink::WebNode, uint32_t>> stack;
    stack.emplace_back(body, 0u);
    while (!stack.empty()) {
      const auto [node, depth] = stack.back();
      stack.pop_back();
      if (context.ledger->exhausted() || !context.ledger->CheckDeadline()) {
        context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
        break;
      }
      if (!context.ledger->WithinDepth(depth) || !node.IsElementNode()) {
        continue;
      }
      const blink::WebElement element = node.To<blink::WebElement>();
      const blink::WebFormControlElement control =
          element.DynamicTo<blink::WebFormControlElement>();
      if (!control.IsNull()) {
        if (formless_seen >= limits.max_formless_controls()) {
          formless_limit_reached = true;
          break;
        }
        ++formless_seen;
        if (!emit_control.Emit(control, std::string(), std::nullopt,
                               ChallengeSignals())) {
          break;
        }
      }
      // The stack is LIFO, so push siblings in reverse. This preserves DOM
      // order in the emitted graph and, critically, in the positional field
      // list later presented to the person. Forward insertion here would
      // visit the last control first even though the walk is otherwise
      // deterministic.
      for (blink::WebNode child = element.LastChild(); !child.IsNull();
           child = child.PreviousSibling()) {
        stack.emplace_back(child, depth + 1);
      }
    }
  }

  result.truncation = context.ledger->report();
  if (saw_prohibited_field) {
    result.warnings.emplace_back(WarningCode::kSensitiveZoneSuppressed,
                                 "secret-field-value-not-read");
  }
  if (form_limit_reached) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "form-count-bound");
  }
  if (control_limit_reached) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "control-count-bound");
  }
  if (formless_limit_reached) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "formless-control-count-bound");
  }

  const bool bounded_out =
      form_limit_reached || control_limit_reached || formless_limit_reached;
  result.status = (result.truncation.truncated || bounded_out)
                      ? AdapterStatus::kIncomplete
                      : AdapterStatus::kOk;
  return result;
}

}  // namespace taffy
