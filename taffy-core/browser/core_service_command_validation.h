// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_COMMAND_VALIDATION_H_
#define TAFFY_BROWSER_CORE_SERVICE_COMMAND_VALIDATION_H_

#include <stddef.h>
#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// How many answers one field-value request may carry back (decision 0088).
//
// 8, and the number is not this file's to choose: it is `MAX_REQUESTED_FIELDS`
// in
// taffy-core/components/intelligence/core/rust/task-engine/src/field_values.rs,
// where `SuppliedValueCount::new` refuses anything larger. The Core Service
// contract declares no constant for it, so the browser states it here with its
// source named rather than letting a count the core will refuse travel as a
// well-formed command — the same rule `kMaxProviderCredentialHandleBytes`
// follows: bound at the narrower side, so the refusal names the field.
//
// This is the one copy in C++. Nothing else restates it; the coordinator
// bounds a person's answers against the fields it actually resolved, which is
// necessarily no larger.
inline constexpr uint32_t kMaxSuppliedFieldValues = 8u;

// Validates the generated tagged body and its bounded payload. Profile state,
// generation, revision, approval, and permission correlation remain manager
// responsibilities and are deliberately not accepted as inputs here.
bool IsStructurallyValidCoreServiceCommand(
    const core_service::mojom::CoreServiceCommand& command,
    size_t maximum_bytes);

// The clause that refused, or nullptr when the command is structurally valid.
//
// The predicate above is what callers decide with; this is what they say when
// it answers no. Every refusal here becomes one `kInvalidCommand` on a wire
// that carries a status and nothing else, so without a name the twenty-odd
// rules in this file are indistinguishable from each other and from the ones
// the ordered core keeps. The labels are compiled in and name the rule, never
// a value from the command.
const char* CoreServiceCommandStructuralRefusal(
    const core_service::mojom::CoreServiceCommand& command,
    size_t maximum_bytes);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_COMMAND_VALIDATION_H_
