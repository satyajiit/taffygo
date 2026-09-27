// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_FORM_CONTROL_TYPE_NAMES_H_
#define TAFFY_RENDERER_ADAPTERS_FORM_CONTROL_TYPE_NAMES_H_

// One mapping from Blink's form-control enumeration to the lowercase HTML type
// string, in one place.
//
// Its own translation unit for the reason the other vocabulary files in this
// directory are — dom_role_mapping, structured_data_vocabulary: it is a naming
// table, not adapter logic, and the adapters that need it are not the same
// adapter. Two of them need it, and until SP-01 both carried their own copy of
// the same thirty-one-arm switch. Two copies of a table that classifiers match
// on is the shape where a drift is invisible: each file would keep compiling,
// and only one of them would agree with the redaction vocabulary.
//
// The spellings are Blink's own, from its FormControlTypeAsString() overrides.
// That is what makes the strings safe to match on rather than merely plausible.

#include <string>
#include <string_view>

#include "third_party/blink/public/mojom/forms/form_control_type.mojom-shared.h"

namespace taffy::form_control_type_names {

// The `type` IDL value of a form control as a lowercase HTML type string.
//
// Total over the enumeration, with no `default:` arm: a control kind added
// upstream breaks the build here rather than silently becoming the empty string
// in a redaction vocabulary that matches on it.
std::string ControlTypeString(blink::mojom::FormControlType type);

// The closed set the renderer may dispatch through SET_TEXT. Form extraction
// and bounded challenge-shape accounting both use this answer; duplicating it
// made a newly supported input capable of being filled without being counted
// as a second entry beside a challenge.
bool IsSetTextControl(std::string_view type);

}  // namespace taffy::form_control_type_names

#endif  // TAFFY_RENDERER_ADAPTERS_FORM_CONTROL_TYPE_NAMES_H_
