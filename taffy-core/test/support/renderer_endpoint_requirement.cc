// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/renderer_endpoint_requirement.h"

#include "taffy/common/public/bip_protocol_support.h"
#include "taffy/test/support/scripted_bip_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

// static
bool RendererEndpointRequirement::IsEndpointBound(ScriptedBipClient& client,
                                                  const TabId& tab_id) {
  const ProtocolSupportEnvelope envelope = client.QueryProtocolSupport(tab_id);
  return envelope.code == ObservationResultCode::kOk &&
         !envelope.endpoint_protocol_version.empty();
}

// static
void RendererEndpointRequirement::Require(ScriptedBipClient& client,
                                          const TabId& tab_id) {
  if (IsEndpointBound(client, tab_id)) {
    return;
  }
  FAIL()
      << "No Taffy renderer endpoint answered protocol negotiation in this "
         "test binary, so every observation after this point would report "
         "unsupported and this test would pass without measuring anything.\n"
         "\n"
         "Where it does exist: the product instrumentation suite, where the "
         "product's own renderer client creates the endpoint for every frame. "
         "That is the binary this suite is meant to run in.\n"
         "\n"
         "VERIFY AT SP-04, to run it anywhere else: how a downstream "
         "content_shell-derived test binary installs its own "
         "ContentRendererClient. Upstream files to read, in order:\n"
         "  content/shell/app/shell_main_delegate.cc "
         "(CreateContentRendererClient)\n"
         "  content/public/renderer/content_renderer_client.h\n"
         "  content/public/test/content_browser_test.h\n"
         "Bind //taffy/renderer's frame observer from whichever of "
         "the three the pinned milestone supports. Do not weaken this check "
         "instead: an unbound endpoint and a broken endpoint produce the same "
         "result code, and only this failure tells them apart.";
}

}  // namespace taffy::test
