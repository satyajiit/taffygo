// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SECRET_SHAPE_RULES_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SECRET_SHAPE_RULES_H_

#include <string_view>
#include <vector>

#include "taffy/components/intelligence/content/scrubbed_text.h"
#include "taffy/components/intelligence/content/secret_shape_scanner.h"

// The individual secret-shape rules of PAR-SEC-009.
//
// Split from secret_shape_scanner.cc so that the two questions stay separable:
// this file answers "what does a secret look like", and the scanner answers
// "what do we do when two rules claim the same span". They change for
// different reasons and at different rates — a new secret shape is an ordinary
// addition here, while a change to the merge is a change to the safety
// property — and a reviewer should be able to see which of the two a diff is.
//
// Every rule is a single forward pass with bounded look-ahead. There is no
// pattern engine anywhere in this seam, because a scrubber an attacker can
// make quadratic is a way to hang the browser process from a page.

namespace taffy {

// Runs every rule over `text` and appends what each of them found. The result
// may overlap and may be out of order; resolving that is the scanner's job.
void RunAllSecretShapeRules(std::string_view text,
                            std::vector<SecretMatch>* out);

// How structural a rule is. When two matches overlap the more structural one
// keeps its replacement text, because it knows what it found; the entropy rule
// is a guess and always loses to something that identified itself.
int SecretShapeRulePrecedence(ScrubRule rule);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SECRET_SHAPE_RULES_H_
