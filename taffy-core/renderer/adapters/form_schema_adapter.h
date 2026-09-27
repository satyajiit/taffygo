// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_FORM_SCHEMA_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_FORM_SCHEMA_ADAPTER_H_

#include <string_view>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// Observation of form structure and the exact accessibility operations each
// classified control can accept: which controls exist, what class of input
// they take, what they are labelled, whether they are enabled, and how
// sensitive they are.
//
// This adapter writes nothing and reads no value. Both halves are worth
// stating plainly because both are load-bearing:
//
//   * NO WRITE PRIMITIVE. This file may advertise SET_TEXT, SELECT_OPTION,
//     TOGGLE, or SUBMIT_FORM on an exact eligible control, but it cannot
//     perform any of them. RendererActionExecutor owns the single platform
//     accessibility path; browser policy, capability, journal and liveness
//     gates remain independent of this observation.
//
//   * NO VALUES. Not even for ordinary fields, at this milestone. Emptiness
//     is a fact about a value, and a code path that reads a value "only when
//     the field is ordinary" is a code path whose safety depends entirely on
//     the classifier being right. Not having the code path at all is a much
//     stronger guarantee than having a well-guarded one, and the research
//     workflow does not need field values to describe a form. Grep this file
//     for a value accessor: there is none, and adding one is a milestone
//     decision plus a threat-model change, not a patch.
//
// It walks controls in two passes, and the second is not optional. A sign-in
// box with no <form> element around it is the ordinary shape of the web, and
// an adapter that only enumerated document.Forms() would miss exactly the
// controls that matter most for classification - which is how the previous
// version of this file came to report itself INCOMPLETE on every single page.
class FormSchemaAdapter final : public Adapter {
 public:
  FormSchemaAdapter();
  ~FormSchemaAdapter() override;

  AdapterKind kind() const override;
  std::string_view name() const override;
  uint32_t extraction_rule_version() const override;
  AdapterResult Run(ExtractionContext& context) override;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_FORM_SCHEMA_ADAPTER_H_
