// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where a model request actually goes, for each of the two kinds of endpoint.
//
// A file of its own rather than more cases in profile_model_broker_unittest.cc,
// which is already past the line cap and is about what the transport does with
// a call once it has a URL — the credential, the cancellation, the status
// mapping. This one is about the URL, and there are two rules for composing it
// (decision 0096 section 2):
//
//   * a **catalog origin** gets the whole compiled path resolved against it,
//     over https, exactly as it always has;
//   * a **registered base URL** is a person's own server, so it keeps its port
//     and whatever base path they typed, and this process joins only the
//     operation beneath it.
//
// The half that is easy to get wrong quietly is the second, and the case that
// says so is `AUserBaseUrlJoinsBeneathTheBasePath`: `GURL::Resolve` reads a
// base path with no trailing separator as a file and drops its last segment,
// which turns `http://box:8000/v1` into `http://box:8000/chat/completions` —
// an address a server answers with a plausible 404 rather than an error
// anybody reads. Both spellings of the base are asserted for that reason.
//
// There is a third thing that can decide a host and it is not a third rule: one
// vendor issues the address with the credential, so the browser substitutes it
// at send time inside the answer the catalog rule already gave. The catalog
// cases of `ProfileModelBrokerCredentialHostTest` are about that: the
// substitution, the two bounds on it, and the thing those bounds do when they
// refuse. They refuse the call. A row that licenses a credential-host domain
// names the licensing parent of that domain as its catalog origin — the issued
// host must sit at or beneath it, and which host that is depends on the
// account — so the catalog origin there is a licensing bound and not an
// endpoint, and falling back to it would be sending the request nowhere with a
// plausible HTTP error to show for it.
//
// The register cases of the same fixture are decision 0116: for a request that
// claims a person's own address, the credential's host decides not where the
// request goes but whether the credential is spent there at all.

#include "taffy/browser/model/profile_model_broker_endpoint_test_support.h"

#include "taffy/browser/model/profile_model_broker.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

using model_broker_endpoint_test::Effect;
using model_broker_endpoint_test::NowMs;
using model_broker_endpoint_test::ProfileModelBrokerEndpointTest;
using model_broker_endpoint_test::kProviderId;
using model_broker_endpoint_test::kWorkerOrigin;

namespace mojom = core_service::mojom;

// The one vendor shape whose credential names its own address, as a row this
// binary would carry. No shipping row can run a flow — none has a dated terms
// review — so the shape is dictated here the way every other provider-auth
// suite dictates it.
constexpr char kLicensedProviderId[] = "test-licensed-host";
constexpr char kLicensedCatalogOrigin[] = "https://api.vendor.example";

constexpr ProviderAuthVendor kLicensedVendor = {
    kLicensedProviderId,
    ProviderAuthFlowKind::kDeviceCode,
    ProviderAuthRedirectKind::kManualCode,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "",
    "https://vendor.example/device",
    "https://vendor.example/token",
    "https://vendor.example/second",
    ".vendor.example",
    "",
    "",
    "",
    "",
    "Someone else's command-line tool",
    {},
    {},
    "2026-08-29",
    /*authorization_displays_code=*/false,
    /*state_is_pkce_verifier=*/false,
    /*presents_client_identity=*/true,
    "test-client",
    "",
    "read:user",
};

// The address decision 0096 was written for: a port and a base path, and no
// certificate because it is a machine on the person's own network.
constexpr char kOwnServer[] = "http://192.168.1.9:11434/v1";

TEST_F(ProfileModelBrokerEndpointTest, AUserBaseUrlJoinsBeneathTheBasePath) {
  const struct {
    mojom::ProviderWireApi wire_api;
    const char *base_url;
    const char *expected;
  } kCases[] = {
      {mojom::ProviderWireApi::kOpenAiCompletions, "http://box.local:8000/v1",
       "http://box.local:8000/v1/chat/completions"},
      {mojom::ProviderWireApi::kOpenAiCompletions, "http://box.local:8000/v1/",
       "http://box.local:8000/v1/chat/completions"},
      {mojom::ProviderWireApi::kAnthropicMessages, "http://box.local:8000/v1",
       "http://box.local:8000/v1/messages"},
      {mojom::ProviderWireApi::kAnthropicMessages, "http://box.local:8000/v1/",
       "http://box.local:8000/v1/messages"},
      // The one family that names the model in its path, beneath the base
      // rather than beneath the origin.
      {mojom::ProviderWireApi::kGoogleGenerativeLanguage,
       "http://box.local:8000/v1beta",
       "http://box.local:8000/v1beta/models/fixture-model:generateContent"}};

  for (const auto &test_case : kCases) {
    Register(kProviderId, test_case.base_url);
    EXPECT_EQ(SentTo(test_case.wire_api, kProviderId, test_case.base_url,
                     mojom::ModelEndpointKind::kUserBaseUrl,
                     test_case.expected),
              GURL(test_case.expected))
        << test_case.base_url;
  }
}

// The whole point, end to end through the transport: a port, a base path and
// plain http to a machine on the person's own network, and the request leaves.
TEST_F(ProfileModelBrokerEndpointTest, AnOwnServerKeepsItsPortAndCleartext) {
  Register(kProviderId, kOwnServer);
  constexpr char kExpected[] =
      "http://192.168.1.9:11434/v1/chat/completions";
  factory_.AddResponse(kExpected, R"({"choices":[]})");

  EXPECT_EQ(Run(Effect(mojom::ProviderWireApi::kOpenAiCompletions, kProviderId,
                       kOwnServer, mojom::ModelEndpointKind::kUserBaseUrl)),
            mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->url, GURL(kExpected));
  // Nothing about a request to a person's own server may be steered by what it
  // answers, which is the same posture every other call here has.
  EXPECT_EQ(observed_->redirect_mode, network::mojom::RedirectMode::kError);
  EXPECT_EQ(observed_->credentials_mode,
            network::mojom::CredentialsMode::kOmit);
}

// A base URL with no path of its own is a base whose path is the root, and it
// composes the operation directly under it. It is not given the family's
// version segment: what a person typed is the base, and inventing a `/v1` they
// did not type would be this process deciding where their server keeps its
// API.
TEST_F(ProfileModelBrokerEndpointTest, ABaseWithNoPathIsTheRoot) {
  constexpr char kBase[] = "http://box.local:11434";
  Register(kProviderId, kBase);

  EXPECT_EQ(SentTo(mojom::ProviderWireApi::kOpenAiCompletions, kProviderId,
                   kBase, mojom::ModelEndpointKind::kUserBaseUrl,
                   "http://box.local:11434/chat/completions"),
            GURL("http://box.local:11434/chat/completions"));
}

// The register is the only authority that can accept a person's own address,
// so its absence is a refusal rather than a deferral, and a row for another
// provider is not a row for this one.
TEST_F(ProfileModelBrokerEndpointTest, AnUnregisteredAddressNeverLeaves) {
  const mojom::EffectEnvelopePtr unregistered =
      Effect(mojom::ProviderWireApi::kOpenAiCompletions, kProviderId,
             kOwnServer, mojom::ModelEndpointKind::kUserBaseUrl);

  // No lookup installed at all.
  EXPECT_EQ(Run(unregistered.Clone()), mojom::EffectStatus::kDenied);

  // A row for a different provider.
  Register("another-gateway", kOwnServer);
  EXPECT_EQ(Run(unregistered.Clone()), mojom::EffectStatus::kDenied);

  // The same provider, one byte different.
  Register(kProviderId, "http://192.168.1.9:11435/v1");
  EXPECT_EQ(Run(unregistered.Clone()), mojom::EffectStatus::kDenied);

  EXPECT_EQ(factory_.NumPending(), 0);
}

// A join that could not stay beneath the base is refused rather than sent.
//
// None of these can reach the register through the save path — the address
// policy refuses a query and a fragment, and an unparseable string is not an
// address — but "cannot happen because another file refuses it" is not a
// property of this function. Appending to a base carrying a query is the one
// worth naming: `http://box:8000/v1?x` would compose
// `http://box:8000/v1?x/chat/completions`, whose origin is unchanged, whose
// path is `/v1`, and whose query swallowed the operation. A server answers
// that request rather than objecting to it.
TEST_F(ProfileModelBrokerEndpointTest, AJoinThatCannotStayBeneathIsRefused) {
  for (const char *base_url : {"http://box.local:8000/v1?key=x",
                               "http://box.local:8000/v1#fragment",
                               "not-an-address", ""}) {
    Register(kProviderId, base_url);
    EXPECT_EQ(Run(Effect(mojom::ProviderWireApi::kOpenAiCompletions,
                         kProviderId, base_url,
                         mojom::ModelEndpointKind::kUserBaseUrl)),
              mojom::EffectStatus::kDenied)
        << base_url;
    EXPECT_EQ(factory_.NumPending(), 0) << base_url;
  }
}

// The managed route stays where it is compiled.
//
// A managed effect must claim a catalog origin and must name the compiled
// worker origin, and the two are checked separately. This case drives the
// first: a register that holds the worker origin for this provider — which the
// command factory and the core's provider plane both make impossible, in
// different files — still buys nothing here.
TEST_F(ProfileModelBrokerEndpointTest, TheManagedRouteIsNotAPersonsAddress) {
  Register(kProviderId, kWorkerOrigin);

  EXPECT_EQ(Run(Effect(mojom::ProviderWireApi::kManaged, kProviderId,
                       kWorkerOrigin, mojom::ModelEndpointKind::kUserBaseUrl)),
            mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
}

// And claiming the catalog does not get the managed wire anywhere either. The
// endpoint here is a perfectly good https origin, so it passes the catalog
// rule and reaches the managed check with nothing else wrong with it — which
// is what makes this a test of the origin pin rather than of the validator
// above it. Nothing in this file widens where the managed wire may be spoken.
TEST_F(ProfileModelBrokerEndpointTest, TheManagedWireStillNeedsItsOwnOrigin) {
  EXPECT_EQ(Run(Effect(mojom::ProviderWireApi::kManaged, kProviderId,
                       "https://provider.taffy.test",
                       mojom::ModelEndpointKind::kCatalogOrigin)),
            mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
}

// The third thing that can decide a host, and the only one that is neither of
// the two rules above: the credential itself.
//
// One vendor issues the address with the token, so the isolated core cannot
// state it and never does — every effect here claims the catalog origin, the
// catalog rule judges it, and what the substitution does is narrow inside the
// answer that rule already gave. That is why there is no third endpoint kind:
// nothing about the request's claimed authority changes.
class ProfileModelBrokerCredentialHostTest
    : public ProfileModelBrokerEndpointTest {
protected:
  void SetUp() override {
    ProfileModelBrokerEndpointTest::SetUp();
    broker_->SetCredentialResolver(base::BindLambdaForTesting(
        [this](const std::string &, const std::string &,
               ProfileModelBroker::ProviderCredentialCallback callback) {
          std::move(callback).Run(std::string("credential-1"), issued_);
        }));
  }

  // The same effect the cases above use, plus the handle that makes phase one
  // run at all.
  mojom::EffectEnvelopePtr KeyedEffect(const std::string &endpoint) {
    mojom::EffectEnvelopePtr effect =
        Effect(mojom::ProviderWireApi::kOpenAiCompletions, kLicensedProviderId,
               endpoint, mojom::ModelEndpointKind::kCatalogOrigin);
    effect->model_request->credential_handle = kLicensedProviderId;
    return effect;
  }

  // The keyed effect claiming the register's address instead, with the
  // register holding that exact string for the provider (decision 0116).
  mojom::EffectEnvelopePtr RegisteredKeyedEffect(const std::string &provider_id,
                                                 const std::string &base_url) {
    Register(provider_id, base_url);
    mojom::EffectEnvelopePtr effect =
        Effect(mojom::ProviderWireApi::kOpenAiCompletions, provider_id,
               base_url, mojom::ModelEndpointKind::kUserBaseUrl);
    effect->model_request->credential_handle = provider_id;
    return effect;
  }

  std::optional<std::string> issued_;
  ScopedProviderAuthVendorForTesting scoped_vendor_{&kLicensedVendor};
};

TEST_F(ProfileModelBrokerCredentialHostTest, AnIssuedAddressReplacesTheHost) {
  constexpr char kExpected[] =
      "https://one.api.vendor.example/v1/chat/completions";
  issued_ = "https://one.api.vendor.example";
  factory_.AddResponse(kExpected, "{}");

  EXPECT_EQ(Run(KeyedEffect(kLicensedCatalogOrigin)),
            mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  // The host moved and the path did not: it is still the compiled one, joined
  // by the same catalog arm.
  EXPECT_EQ(observed_->url, GURL(kExpected));
}

// Every refusal refuses the call, and nothing leaves. The five are: an address
// nowhere near the vendor, one under a domain the core never named — the bound
// that keeps this product's statement of where a request went true —
// cleartext, an address carrying a path, and the empty string, which is not an
// address at all and needs no rule of its own to be turned away.
//
// The catalog route is stubbed anyway, so that a request that fell back to it
// would succeed rather than fail for want of a response: this case has to fail
// when the fall-through returns, not pass for a second reason.
TEST_F(ProfileModelBrokerCredentialHostTest, ARefusedAddressRefusesTheCall) {
  factory_.AddResponse("https://api.vendor.example/v1/chat/completions", "{}");

  for (const char *refused : {"https://attacker.example",
                              "https://other.vendor.example",
                              "http://one.api.vendor.example",
                              "https://one.api.vendor.example/v1", ""}) {
    issued_ = refused;
    // Bracketed because one of the five is the empty string, and an unlabelled
    // failure message would be the only one with nothing after the colon.
    EXPECT_EQ(Run(KeyedEffect(kLicensedCatalogOrigin)),
              mojom::EffectStatus::kDenied)
        << "[" << refused << "]";
    EXPECT_FALSE(observed_) << "[" << refused << "]";
  }
  EXPECT_EQ(factory_.NumPending(), 0);
}

// And so does a store that answered with no address at all, for a vendor whose
// credentials name one.
//
// This is the case that arrives looking exactly like the ordinary answer every
// other vendor gives, and it is why the absence is read against this binary's
// vendor table rather than taken at face value. It is reachable without anyone
// misbehaving: the browser keeps the token and drops an address outside the
// licensed domain when it seals the record, and a record sealed before the row
// existed carries none.
TEST_F(ProfileModelBrokerCredentialHostTest, AMissingAddressRefusesTheCall) {
  factory_.AddResponse("https://api.vendor.example/v1/chat/completions", "{}");
  issued_ = std::nullopt;

  EXPECT_EQ(Run(KeyedEffect(kLicensedCatalogOrigin)),
            mojom::EffectStatus::kDenied);
  EXPECT_FALSE(observed_);
  EXPECT_EQ(factory_.NumPending(), 0);
}

// Decision 0116 section 2. The register's address is never rewritten — the
// request goes to the address the person typed, not to the host the credential
// named — and beneath the vendor's licensed suffix the credential is spent
// there with nothing said. A self-run proxy beneath the vendor's own domain is
// the case an outright refusal would have cost, and the issued host itself,
// registered by hand, is the other legitimate shape.
TEST_F(ProfileModelBrokerCredentialHostTest,
       BeneathTheLicensedSuffixTheRequestGoesUnchanged) {
  issued_ = "https://one.api.vendor.example";
  const struct {
    const char *base_url;
    const char *expected;
  } kCases[] = {
      {"https://proxy.vendor.example/v1",
       "https://proxy.vendor.example/v1/chat/completions"},
      {"https://one.api.vendor.example/v1/",
       "https://one.api.vendor.example/v1/chat/completions"},
  };

  for (const auto &test_case : kCases) {
    factory_.AddResponse(test_case.expected, "{}");
    EXPECT_EQ(Run(RegisteredKeyedEffect(kLicensedProviderId,
                                        test_case.base_url)),
              mojom::EffectStatus::kCompleted)
        << test_case.base_url;
    ASSERT_TRUE(observed_) << test_case.base_url;
    EXPECT_EQ(observed_->url, GURL(test_case.expected));
    EXPECT_EQ(observed_->headers.GetHeader("authorization"),
              std::optional<std::string>("Bearer credential-1"))
        << test_case.base_url;
  }
}

// Decision 0116 section 3. A subscription credential that named its own host
// is not spent at a registered address outside the licensed suffix, and the
// refusal is the same closed denial the licence check answers a catalog
// request with. The five: an address nowhere near the vendor, the vendor's
// bare domain (the suffix carries its leading dot, and the domain itself is
// the vendor's site rather than an issued host), a look-alike that merely ends
// in the same characters, cleartext beneath the suffix, and the decision 0096
// address itself — a person's own machine, which a subscription credential was
// never issued for.
//
// Every registered address's own route is stubbed, so a request that went
// anywhere would complete rather than fail for want of a response: this case
// has to fail when the drop returns, not pass for a second reason.
TEST_F(ProfileModelBrokerCredentialHostTest,
       OutsideItThePairingIsRefusedByName) {
  issued_ = "https://one.api.vendor.example";

  for (const char *base_url :
       {"https://attacker.example/v1", "https://vendor.example/v1",
        "https://evilvendor.example/v1", "http://one.api.vendor.example/v1",
        kOwnServer}) {
    factory_.AddResponse(base::StrCat({base_url, "/chat/completions"}), "{}");
    EXPECT_EQ(Run(RegisteredKeyedEffect(kLicensedProviderId, base_url)),
              mojom::EffectStatus::kDenied)
        << base_url;
    EXPECT_FALSE(observed_) << base_url;
  }
  EXPECT_EQ(factory_.NumPending(), 0);
}

// Decision 0116 section 3, last paragraph. A credential that names no host is
// unaffected: it never had an address to be spent beneath, and the register's
// address is where it has always gone. Two shapes arrive that way — the
// licensed vendor's store answering with no address, which the catalog arm
// above refuses and this arm does not, and an ordinary provider whose row
// licenses nothing, for which no address was ever expected. The second is the
// decision 0096 case with a key on it, and it is sent exactly as it was before
// this arm existed.
TEST_F(ProfileModelBrokerCredentialHostTest,
       ACredentialThatNamesNoHostIsUnaffected) {
  issued_ = std::nullopt;
  const struct {
    const char *provider_id;
    const char *base_url;
    const char *expected;
  } kCases[] = {
      {kLicensedProviderId, "https://gateway.example/v1",
       "https://gateway.example/v1/chat/completions"},
      {kProviderId, kOwnServer,
       "http://192.168.1.9:11434/v1/chat/completions"},
  };

  for (const auto &test_case : kCases) {
    factory_.AddResponse(test_case.expected, "{}");
    EXPECT_EQ(Run(RegisteredKeyedEffect(test_case.provider_id,
                                        test_case.base_url)),
              mojom::EffectStatus::kCompleted)
        << test_case.base_url;
    ASSERT_TRUE(observed_) << test_case.base_url;
    EXPECT_EQ(observed_->url, GURL(test_case.expected));
    EXPECT_EQ(observed_->headers.GetHeader("authorization"),
              std::optional<std::string>("Bearer credential-1"))
        << test_case.base_url;
  }
}

} // namespace
} // namespace taffy
