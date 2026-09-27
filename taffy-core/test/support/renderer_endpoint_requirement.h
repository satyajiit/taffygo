// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_RENDERER_ENDPOINT_REQUIREMENT_H_
#define TAFFY_TEST_SUPPORT_RENDERER_ENDPOINT_REQUIREMENT_H_

#include "taffy/common/public/bip_identity.h"

// States, and checks, that a suite needs a real Taffy renderer endpoint bound.
//
// Whether one is bound is a property of the test BINARY, not of the test. The
// endpoint is created by the product's own renderer client, so it exists in the
// product instrumentation suite and does not exist in a plain content_shell
// browser test binary that has not installed one.
//
// That distinction cannot be left implicit, because of how the browser half
// answers when the endpoint is absent: negotiation reports UNSUPPORTED, and
// every observation after it reports UNSUPPORTED too. A correctness test that
// asserted "the result was not an error" would pass. It would be measuring an
// empty room.
//
// So a suite that needs the endpoint calls Require() first, and gets a failure
// that names exactly what is missing and where it exists instead. This is a
// hard failure rather than a skip for the same reason a missing corpus is: a
// green row in an exit-evidence packet has to mean the property was checked.

namespace taffy::test {

class ScriptedBipClient;

class RendererEndpointRequirement {
 public:
  RendererEndpointRequirement() = delete;

  // Runs protocol negotiation and returns true when an endpoint answered.
  // Consumes one request on `client`, which counts toward its
  // one-terminal-result bookkeeping like any other.
  [[nodiscard]] static bool IsEndpointBound(ScriptedBipClient& client,
                                            const TabId& tab_id);

  // Fails the current test with the instruction below when no endpoint
  // answered. Call it at the top of any test whose subject is what the endpoint
  // produces.
  static void Require(ScriptedBipClient& client, const TabId& tab_id);
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_RENDERER_ENDPOINT_REQUIREMENT_H_
