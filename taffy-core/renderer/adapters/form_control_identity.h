// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_FORM_CONTROL_IDENTITY_H_
#define TAFFY_RENDERER_ADAPTERS_FORM_CONTROL_IDENTITY_H_

#include <stdint.h>

#include <string>

#include "taffy/renderer/adapters/adapter.h"
#include "third_party/blink/public/web/web_form_control_element.h"

namespace taffy::form_schema_internal {

// Joins one exact DOM form control to Blink's accessibility identity and
// attaches its standard accessible label. When a SECTION would otherwise
// lose that label at the browser's sensitivity gate, this also emits one
// bounded, non-actionable accessibility witness under the exact AX identity.
// Returns false only when that required witness cannot fit the shared budget.
bool EnrichFormControlIdentity(const blink::WebFormControlElement& control,
                               ExtractionContext& context,
                               uint32_t rule_version,
                               SemanticNode& control_node,
                               AdapterResult& result);

// What the page calls `control`, for the person's sheet and nothing else.
//
// The graph withholds the label of a challenge's answer and of a one-time
// code (`MayEmitText`), so the model never reads a CAPTCHA's question. A
// sheet asking the person for either value still has to name the row, and
// it names it from the live node's label. This label is kept only there: it
// never enters the graph, the core or the model, and it charges no graph
// budget (decision 0249). Empty when the control has no accessible name,
// when the name would come from its value, or when it looks like an
// identifier.
std::string PersonFacingControlLabel(
    const blink::WebFormControlElement& control,
    const ExtractionContext& context);

}  // namespace taffy::form_schema_internal

#endif  // TAFFY_RENDERER_ADAPTERS_FORM_CONTROL_IDENTITY_H_
