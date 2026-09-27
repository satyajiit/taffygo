// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SECURITY_POSTURE_EVALUATOR_H_
#define TAFFY_BROWSER_SECURITY_POSTURE_EVALUATOR_H_

#include "taffy/browser/security_posture.h"

// Judges a SecurityPosture (PAR-SEC-001, PAR-SEC-008).
//
// Pure, so the whole compliance table is unit-testable without a browser. That
// matters more here than anywhere else in this directory: the table says which
// configurations are unfit to ship, and a table that could only be exercised
// on a device would be a table nobody exercised.
//
// It judges; it does not repair. There is no code path in TaffyGo that turns a
// protection back on in response to a finding, and there should not be: a build
// that disabled the sandbox and then re-enabled it at run time would be a build
// whose posture depends on timing.

namespace taffy {

SecurityPostureVerdict EvaluateSecurityPosture(const SecurityPosture& posture);

// A stable, content-free description of one violation, for a report a person
// reads. Returns an empty view for kNone and for a value this build does not
// know, so a caller never prints a placeholder.
const char* DescribeSecurityPostureViolation(SecurityPostureViolation value);

}  // namespace taffy

#endif  // TAFFY_BROWSER_SECURITY_POSTURE_EVALUATOR_H_
