// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The path a catalog origin composes, for every vendor that needs one.
//
// Split from profile_model_broker_endpoint_unittest.cc, which is about which
// address a request may name — a person's own server, a credential's issued
// host — and had grown past the line cap. This file is about the other half:
// given an address the catalog gave, what path is joined beneath it. The two
// answer to different decisions (0049 keeps every path in this process; 0096
// governs an address a person typed) and neither substitutes for the other.
//
// The table this exercises fails in one direction and it is quiet: a vendor
// with no row composes the bare family path, which is a real address at most
// vendors and a 404 at the ones that need a prefix. So every row is a case.

#include "taffy/browser/model/profile_model_broker_endpoint_test_support.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using model_broker_endpoint_test::Effect;
using model_broker_endpoint_test::kProviderId;
using model_broker_endpoint_test::ProfileModelBrokerEndpointTest;

// A catalog origin composes exactly what it always has, for every family and
// for both shapes the vendor table can take.
//
// The prefixed rows are the ones worth naming. `openrouter` carries `/api`,
// `groq` carries a base path and the family's own version segment, and `zai`
// carries a base path *and* its own version because that vendor serves the
// OpenAI-shaped API at `/api/paas/v4`. A test that exercised only the rows with
// no version override would pass with the version column ignored entirely, and
// the failure that produced would be a plausible 404 rather than anything
// anybody notices.
TEST_F(ProfileModelBrokerEndpointTest, ACatalogOriginComposesAsItAlwaysHas) {
  constexpr char kOrigin[] = "https://provider.taffy.test";
  const struct {
    mojom::ProviderWireApi wire_api;
    const char *provider_id;
    const char *expected;
  } kCases[] = {
      {mojom::ProviderWireApi::kAnthropicMessages, "anthropic",
       "https://provider.taffy.test/v1/messages"},
      {mojom::ProviderWireApi::kOpenAiResponses, "openai",
       "https://provider.taffy.test/v1/responses"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "cerebras",
       "https://provider.taffy.test/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "openrouter",
       "https://provider.taffy.test/api/v1/chat/completions"},
      {mojom::ProviderWireApi::kGoogleGenerativeLanguage, "google",
       "https://provider.taffy.test/v1beta/models/"
       "fixture-model:generateContent"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "groq",
       "https://provider.taffy.test/openai/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "fireworks",
       "https://provider.taffy.test/inference/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "zai",
       "https://provider.taffy.test/api/paas/v4/chat/completions"},
      // The four vendors decision 0114 added. Three carry a prefix and the
      // fourth carries none, and both halves are here because the way this
      // table goes wrong is a row nobody wrote: an absent provider composes
      // the bare family path, which is a real address at most vendors and a
      // 404 at these three.
      {mojom::ProviderWireApi::kOpenAiCompletions, "venice",
       "https://provider.taffy.test/api/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "novita",
       "https://provider.taffy.test/openai/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "kilocode",
       "https://provider.taffy.test/api/gateway/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "chutes",
       "https://provider.taffy.test/v1/chat/completions"},
      // The one row whose prefix is empty and whose version is two segments:
      // DeepInfra documents its OpenAI-shaped API at `/v1/openai`, so what
      // stands where the family's `/v1` would is `/v1/openai`. Written as a
      // case rather than trusted, because a row that composed
      // `/v1/openai/v1/chat/completions` — the shape a prefix column would
      // have produced — is a 404 the vendor answers politely.
      {mojom::ProviderWireApi::kOpenAiCompletions, "deepinfra",
       "https://provider.taffy.test/v1/openai/chat/completions"},
      // The vendors decision 0118's import added that serve this family
      // beneath a path. Every one of them is here rather than a sample,
      // because the table's failure mode is a row nobody wrote: an absent
      // provider composes the bare family path, and a vendor that does not
      // serve it there answers a 404 that reads like an outage.
      {mojom::ProviderWireApi::kOpenAiCompletions, "qwen",
       "https://provider.taffy.test/compatible-mode/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "qwen-token-plan",
       "https://provider.taffy.test/compatible-mode/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "qwen-token-plan-cn",
       "https://provider.taffy.test/compatible-mode/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "zai-coding-cn",
       "https://provider.taffy.test/api/coding/paas/v4/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "opencode",
       "https://provider.taffy.test/zen/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "opencode-go",
       "https://provider.taffy.test/zen/go/v1/chat/completions"},
      {mojom::ProviderWireApi::kAnthropicMessages, "minimax-cn",
       "https://provider.taffy.test/anthropic/v1/messages"},
      // And two the same import added that carry no prefix at all, which is
      // the other half of the same rule: a row written for a vendor that
      // needs none would send every one of its calls somewhere it is not.
      {mojom::ProviderWireApi::kOpenAiCompletions, "huggingface",
       "https://provider.taffy.test/v1/chat/completions"},
      {mojom::ProviderWireApi::kAnthropicMessages, "vercel-ai-gateway",
       "https://provider.taffy.test/v1/messages"}};

  for (const auto &test_case : kCases) {
    EXPECT_EQ(SentTo(test_case.wire_api, test_case.provider_id, kOrigin,
                     mojom::ModelEndpointKind::kCatalogOrigin,
                     test_case.expected),
              GURL(test_case.expected))
        << test_case.expected;
  }
}

// The catalog rule still refuses cleartext, and a register holding the same
// address does not help: a request that claimed the catalog is answered by the
// catalog rule and never by the register beside it.
TEST_F(ProfileModelBrokerEndpointTest, ACatalogOriginIsStillHttpsOnly) {
  Register(kProviderId, "http://provider.taffy.test");

  EXPECT_EQ(Run(Effect(mojom::ProviderWireApi::kOpenAiCompletions, kProviderId,
                       "http://provider.taffy.test",
                       mojom::ModelEndpointKind::kCatalogOrigin)),
            mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
}

// The join, with and without the separator a person might or might not type.
//
// Both spellings are the same request. The one without is the case
// `GURL::Resolve` would have got wrong by dropping `v1` and sending the call
// to the server's root, and the one with is the case that would have been
// right by accident — asserting only the second is how that defect ships.
}  // namespace
}  // namespace taffy
