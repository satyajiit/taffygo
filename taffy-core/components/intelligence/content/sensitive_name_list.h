// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SENSITIVE_NAME_LIST_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SENSITIVE_NAME_LIST_H_

#include <stddef.h>

#include <string_view>

// Parameter and field names whose value is sensitive by definition
// (PAR-SEC-009, REQ-SEC-001).
//
// One list, in one file, because three separate places need the same answer
// and the failure mode of them disagreeing is silent: the browser's scrubber
// would redact a field the renderer's redaction had already let through, or —
// worse — the reverse. The renderer half is
// //taffy/renderer/field_redaction.h, which works from the field's
// type and autofill attributes rather than from its name; this list is the
// name-based half and covers the cases a form's markup does not label.
//
// The list is a floor, not a ceiling. Every name here means "the value is
// sensitive"; a name absent from it means only "this list does not say", and
// the shape rules in secret_shape_rules.cc are what catch the rest.

namespace taffy {

// True when `name` — a parameter, a header, or a JSON field — declares its
// value sensitive. Case insensitive.
bool IsSensitiveValueName(std::string_view name);

// The list itself, for a test that wants to walk it rather than sample it.
size_t GetSensitiveValueNameCount();
std::string_view GetSensitiveValueName(size_t index);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SENSITIVE_NAME_LIST_H_
