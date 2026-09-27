// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_FORM_SCHEMA_CONTROL_H_
#define TAFFY_RENDERER_ADAPTERS_FORM_SCHEMA_CONTROL_H_

#include <stdint.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "taffy/renderer/challenge_classifier.h"
#include "taffy/renderer/adapters/adapter.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_form_element.h"

namespace taffy::form_schema_internal {

// Authored form metadata shared by the form-region and control paths. This is
// intentionally not a value accessor; the form adapter never reads a control's
// current value.
std::string AuthoredAttribute(const blink::WebElement& element,
                              const char* name);
std::string FormSemanticsOf(const blink::WebFormElement& form);
ChallengeSignals ChallengeSignalsOf(
    const blink::WebFormElement& form,
    const std::vector<blink::WebFormControlElement>& controls);
// The challenge one control carries, and the DOM node the sheet presents for
// it: the picture of an image challenge or the widget of an interactive one.
struct FieldChallenge {
  ChallengeKind kind = ChallengeKind::kNone;
  std::optional<int64_t> presentation_dom_node_id;
};

// Classifies `control` from its form's signals and, when those name nothing,
// from the block around the field. A form that asks for a number and a
// CAPTCHA holds two fields, so the form-wide reading can never find the
// CAPTCHA; the block around it can (decision 0193).
FieldChallenge ClassifyFieldChallenge(
    const blink::WebFormControlElement& control,
    const FieldDescriptor& field,
    ProhibitedCategory category,
    const ChallengeSignals& form_signals);

// Owns the one classification/emission path used by both controls inside a
// form and standalone controls. Keeping this path singular prevents the
// privacy and action-capability rules from drifting between the two walks.
class FormControlEmitter {
 public:
  FormControlEmitter(ExtractionContext& context,
                     AdapterResult& result,
                     std::set<int64_t>& covered,
                     RendererContentTrust document_trust,
                     ContentSignalMask frame_signals,
                     bool insecure_context,
                     uint32_t rule_version,
                     bool& saw_prohibited_field);

  bool Emit(const blink::WebFormControlElement& control,
            const std::string& form_semantics,
            const std::optional<SemanticNodeId>& parent_form,
            const ChallengeSignals& challenge_signals);

 private:
  const raw_ref<ExtractionContext> context_;
  const raw_ref<AdapterResult> result_;
  const raw_ref<std::set<int64_t>> covered_;
  const RendererContentTrust document_trust_;
  const ContentSignalMask frame_signals_;
  const bool insecure_context_;
  const uint32_t rule_version_;
  const raw_ref<bool> saw_prohibited_field_;
};

}  // namespace taffy::form_schema_internal

#endif  // TAFFY_RENDERER_ADAPTERS_FORM_SCHEMA_CONTROL_H_
