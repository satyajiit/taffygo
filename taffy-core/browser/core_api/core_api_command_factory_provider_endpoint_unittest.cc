// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The two endpoint rules, and the wall between them.
//
// This factory holds two checks over one field, and decision 0096 section 2
// requires that they never be written in terms of each other:
//
//   * `CheckCatalogProviderEndpoint` judges an address a served document
//     named. https, publicly routable, spelled exactly as an origin.
//   * `CheckCustomProviderEndpoint` decides what a person may register as
//     their own model server's address. The port and base path are theirs,
//     plain http is admitted to a literal local machine and nowhere else.
//
// A suite that asserted only one of them would let the other be quietly
// widened into it, which is the failure decision 0096 exists to prevent, so
// both are here and the case in the middle —
// `TheTwoRulesDisagreeAndThatIsTheDesign` — names addresses each one accepts
// and the other refuses. If that case ever passes with an empty disagreement,
// the two rules have become one.
//
// The other thing this file is for is the agreement that has to hold in the
// *other* direction: an endpoint accepted here must be one the register can
// hold and one the transport will send to. Nothing but a test compares those,
// because they are three functions in three targets, and an endpoint accepted
// at the save and refused at the send is a provider a person can create and
// can never use.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <stddef.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/core_api/core_api_command_factory_provider_internal.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/model/custom_provider_endpoint_store.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNowMs = 1000u;
constexpr char kProviderId[] = "my-gateway";

// The address decision 0096 was written for: a person's own server, on their
// own network, with a port and a base path and no certificate.
constexpr char kOwnServer[] = "http://192.168.1.9:11434/v1";

class FixedEntropy final : public CoreApiEntropySource {
public:
  std::string NewOpaqueId(std::string_view domain) override {
    return std::string(domain) + "-fixed";
  }

  std::array<uint8_t, 32> NewTaskSeed() override {
    std::array<uint8_t, 32> seed{};
    seed.fill(0x5a);
    return seed;
  }
};

CoreApiCommandFactory NewFactory() {
  return CoreApiCommandFactory("profile-fixed",
                               std::make_unique<FixedEntropy>());
}

ProviderCommandResult SaveWithEndpoint(const std::string &endpoint) {
  return NewFactory().BuildSaveCustomProvider(
      kProviderId, "My gateway", endpoint,
      api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>(),
      std::vector<api::CustomModelSpecViewPtr>(),
      std::optional<api::ServerKindView>(), kGeneration, kNowMs);
}

ProviderCommandResult ProbeWithEndpoint(const std::string &endpoint) {
  // The same provider identity the save above uses, so that the two answers
  // this file compares differ in the endpoint and in nothing else.
  return NewFactory().BuildProbeCustomEndpoint(
      kProviderId, endpoint, api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>(), kGeneration, kNowMs);
}

// The endpoint as it will actually be submitted, which is the string the
// register has to hold and the transport has to recognise. Read off the built
// command rather than off the argument, because the argument is what somebody
// asked for and this is what the process decided to send.
std::string SubmittedEndpoint(const ProviderCommandResult &result) {
  return result.command && result.command->core_service_command &&
                 result.command->core_service_command->save_custom_provider
             ? result.command->core_service_command->save_custom_provider
                   ->endpoint
             : std::string();
}

// ---------------------------------------------------------------------------
// The catalog rule
// ---------------------------------------------------------------------------

// An endpoint the catalog named is an origin, and one spelling of it.
//
// The path belongs to the compiled route table, not to the document: a browser
// that appended a compiled path to an endpoint that already had one would
// reach a URL nobody wrote, and `IsValidCoreModelRequest` refuses such a
// catalog endpoint anyway. A query, a fragment, user information, an explicit
// `:443` and an upper-case host are refused by the same rule and for the same
// reason: two spellings that reach one provider would be two providers.
TEST(CoreApiProviderEndpointTest, ACatalogEndpointIsAnOriginAndOneSpelling) {
  const struct {
    const char *endpoint;
    ProviderRequestRefusal refusal;
  } kCases[] = {
      // Not https at all. The empty string and the bare host land here too:
      // neither parses as a URL, and "the endpoint is not an https URL" is
      // exactly what is wrong with both.
      {"", ProviderRequestRefusal::kEndpointNotHttps},
      {"gateway.example.com", ProviderRequestRefusal::kEndpointNotHttps},
      {"http://gateway.example.com/v1",
       ProviderRequestRefusal::kEndpointNotHttps},
      {"file:///etc/passwd", ProviderRequestRefusal::kEndpointNotHttps},
      {"data:text/plain,hello", ProviderRequestRefusal::kEndpointNotHttps},
      // A path, and the trailing slash that is one. GURL canonicalizes
      // "https://host" to a URL whose path is "/", so the two differ only in
      // what was written — and the endpoint is compared against what was
      // written, because that is the string the core stores and the transport
      // later reads.
      {"https://gateway.example.com/v1",
       ProviderRequestRefusal::kEndpointNotAnOrigin},
      {"https://gateway.example.com/",
       ProviderRequestRefusal::kEndpointNotAnOrigin},
      // Host "v1", not a hostless URL: a standard scheme's authority swallows
      // every leading slash.
      {"https:///v1", ProviderRequestRefusal::kEndpointNotAnOrigin},
      {"https://gateway.example.com:443",
       ProviderRequestRefusal::kEndpointNotAnOrigin},
      {"https://Gateway.Example.COM",
       ProviderRequestRefusal::kEndpointNotAnOrigin},
      {"https://gateway.example.com/v1?key=secret",
       ProviderRequestRefusal::kEndpointNotAnOrigin},
      {"https://gateway.example.com/v1#fragment",
       ProviderRequestRefusal::kEndpointNotAnOrigin},
      {"https://someone@gateway.example.com/v1",
       ProviderRequestRefusal::kEndpointNotAnOrigin},
      {"https://someone:secret@gateway.example.com/v1",
       ProviderRequestRefusal::kEndpointNotAnOrigin}};

  for (const auto &test_case : kCases) {
    EXPECT_EQ(CheckCatalogProviderEndpoint(test_case.endpoint),
              test_case.refusal)
        << test_case.endpoint;
  }
}

// The refusal decision 0049 assigns to the browser, and the reason it is a
// classification rather than a pattern: 0x7f.1 and 2130706433 are both
// 127.0.0.1 once GURL has canonicalized them, and ::ffff:169.254.169.254 is
// the link-local metadata address wearing IPv6. A pattern list gets each of
// those wrong; net::IPAddress gets all of them right in both families.
TEST(CoreApiProviderEndpointTest, ACatalogEndpointOffThePublicInternet) {
  for (const char *endpoint :
       {"https://127.0.0.1/v1", "https://0x7f.1/v1", "https://2130706433/v1",
        "https://localhost/v1", "https://[::1]/v1",
        "https://169.254.169.254/latest/meta-data/",
        "https://[::ffff:169.254.169.254]/latest/meta-data/",
        "https://10.0.0.1/v1", "https://192.168.1.1/v1",
        "https://172.16.0.1/v1", "https://[fd00::1]/v1",
        "https://0.0.0.0/v1"}) {
    EXPECT_EQ(CheckCatalogProviderEndpoint(endpoint),
              ProviderRequestRefusal::kEndpointNotPublic)
        << endpoint;
  }
}

// A name is not resolved by the catalog rule on purpose: the request has not
// been sent, and an answer now would be a different answer from the one at
// connect time. A single-label name is accepted for the same reason and not by
// oversight — on a network with a search suffix, https://llm is somebody's
// real gateway. A non-default port is part of the origin and is kept.
//
// `kEndpointNoHost` has no spelling that reaches it: a standard URL with a
// genuinely empty host does not canonicalize at all, so GURL reports it
// invalid and the not-https arm answers first, and "https:///v1" is host "v1"
// rather than hostless. The check stays as a guard on that behaviour.
TEST(CoreApiProviderEndpointTest, ACatalogEndpointsNameIsNotResolvedHere) {
  for (const char *endpoint :
       {"https://gateway.example.com", "https://gateway.example.com:8443",
        "https://llm", "https://[2606:4700::1111]"}) {
    EXPECT_EQ(CheckCatalogProviderEndpoint(endpoint),
              ProviderRequestRefusal::kNone)
        << endpoint;
  }
}

// An endpoint the catalog rule accepts is one the catalog transport will send
// to. `IsValidCoreModelRequest` refuses any catalog endpoint that is not
// *exactly* an https origin, and a check looser than that one is not a
// lenience — it is an endpoint that is accepted here and refused as `DENIED`
// at every later request, with nothing anybody could read. The two rules have
// no gate comparing them, so this is the gate.
TEST(CoreApiProviderEndpointTest, ACatalogEndpointIsOneTheTransportSendsTo) {
  for (const char *endpoint :
       {"https://gateway.example.com", "https://gateway.example.com:8443",
        "https://llm", "https://[2606:4700::1111]",
        "https://gateway.example.com/v1", "https://gateway.example.com/"}) {
    if (CheckCatalogProviderEndpoint(endpoint) !=
        ProviderRequestRefusal::kNone) {
      // Refused here is the safe answer; this case is only about what is
      // accepted.
      continue;
    }
    auto request = service::ModelRequestEffect::New();
    request->route_id = "route-1";
    request->model_id = "model-1";
    request->disclosure = service::DisclosureClass::kUserSelectedContent;
    request->max_output_bytes = 1024u;
    request->provider_id = kProviderId;
    request->wire_api = service::ProviderWireApi::kOpenAiCompletions;
    request->endpoint = endpoint;
    request->endpoint_kind = service::ModelEndpointKind::kCatalogOrigin;
    EXPECT_TRUE(IsValidCoreModelRequest(*request, service::kMaxIdentifierBytes,
                                        service::kMaxEffectBytes))
        << endpoint;
  }
}

// ---------------------------------------------------------------------------
// The register rule
// ---------------------------------------------------------------------------

// The save decision 0096 exists to make possible, and the two things the old
// rule threw away: the port and the base path. Both survive to both contracts,
// byte for byte, because the register will later compare against exactly these
// bytes.
TEST(CoreApiProviderEndpointTest, AnOwnAddressKeepsItsPortAndItsBasePath) {
  const ProviderCommandResult result = SaveWithEndpoint(kOwnServer);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  ASSERT_TRUE(result.command->core_api_command->save_custom_provider);
  EXPECT_EQ(result.command->core_api_command->save_custom_provider->endpoint,
            kOwnServer);
  EXPECT_EQ(SubmittedEndpoint(result), kOwnServer);
}

// Cleartext is refused by name, not generically.
//
// This is the refusal a person most needs told: the address is fine, the
// scheme is fine, and the pair is not. Being told "that is not a valid
// endpoint" would send somebody to check the host they typed correctly.
TEST(CoreApiProviderEndpointTest, CleartextOffTheLocalNetworkIsNamedAsSuch) {
  const ProviderCommandResult result =
      SaveWithEndpoint("http://gateway.example.com/v1");

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kEndpointCleartextNotLocal);
  EXPECT_FALSE(result.command);
  EXPECT_NE(result.refusal, ProviderRequestRefusal::kEndpointNotAnAddress);
  EXPECT_NE(result.refusal, ProviderRequestRefusal::kMalformedCommand);
}

// One refusal per thing that can be wrong, in the order the classifier decides
// them. The order is the point of the last two rows: an address that carries
// both a query and plain http is reported as the query, because a person who
// typed a query has a different thing to fix than one who typed http and being
// told about the scheme first would send them to fix the wrong one.
TEST(CoreApiProviderEndpointTest, EveryOwnAddressRefusalIsNamedByWhatIsWrong) {
  const struct {
    const char *endpoint;
    ProviderRequestRefusal refusal;
  } kCases[] = {
      {"", ProviderRequestRefusal::kEndpointNotAnAddress},
      {"192.168.1.9:11434", ProviderRequestRefusal::kEndpointNotAnAddress},
      {"file:///etc/passwd", ProviderRequestRefusal::kEndpointNotAnAddress},
      {"data:text/plain,hello", ProviderRequestRefusal::kEndpointNotAnAddress},
      {"ftp://192.168.1.9/v1", ProviderRequestRefusal::kEndpointNotAnAddress},
      {"http://someone:secret@192.168.1.9:11434/v1",
       ProviderRequestRefusal::kEndpointCarriesCredentials},
      {"http://192.168.1.9:11434/v1?key=secret",
       ProviderRequestRefusal::kEndpointCarriesQuery},
      {"http://192.168.1.9:11434/v1#fragment",
       ProviderRequestRefusal::kEndpointCarriesFragment},
      {"http://gateway.example.com/v1",
       ProviderRequestRefusal::kEndpointCleartextNotLocal},
      // A name that resolves to a private address is still a name, and
      // resolution happens after this check.
      {"http://box.internal/v1",
       ProviderRequestRefusal::kEndpointCleartextNotLocal},
      // Refused before the scheme is looked at.
      {"http://gateway.example.com/v1?key=secret",
       ProviderRequestRefusal::kEndpointCarriesQuery}};

  for (const auto &test_case : kCases) {
    const ProviderCommandResult result = SaveWithEndpoint(test_case.endpoint);
    EXPECT_EQ(result.refusal, test_case.refusal) << test_case.endpoint;
    EXPECT_FALSE(result.command) << test_case.endpoint;
  }
}

TEST(CoreApiProviderEndpointTest, AnOwnAddressPastTheContractBoundIsRefused) {
  const std::string too_long =
      "http://192.168.1.9:11434/" +
      std::string(api::kMaxProviderEndpointBytes, 'p');

  EXPECT_EQ(SaveWithEndpoint(too_long).refusal,
            ProviderRequestRefusal::kEndpointTooLong);
  EXPECT_EQ(CheckCatalogProviderEndpoint(too_long),
            ProviderRequestRefusal::kEndpointTooLong);
}

// Every address form decision 0096 section 3 admits, accepted.
//
// `localhost` is in this list rather than refused with the names it resembles,
// and it is the row that matters most: it is what somebody running a server on
// the machine in front of them will type, and a rule that refused it would
// have made the ordinary case the broken one.
TEST(CoreApiProviderEndpointTest, EveryLocalSpellingIsRegistrable) {
  for (const char *endpoint :
       {"http://192.168.1.9:11434/v1", "http://127.0.0.1:8000/v1",
        "http://localhost:11434/v1", "http://[::1]:8080/v1",
        "http://10.0.0.5:8000", "http://172.16.4.2:1234/v1/",
        "http://workstation.local:11434/v1", "http://[fd00::1]:8000/v1",
        // https is allowed to anything at all, which is the half of the rule
        // that is not about locality.
        "https://gateway.example.com/v1", "https://gateway.example.com"}) {
    const ProviderCommandResult result = SaveWithEndpoint(endpoint);
    EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone) << endpoint;
    EXPECT_TRUE(result.command) << endpoint;
  }
}

// The probe accepts exactly what a save accepts, and refuses exactly what a
// save refuses.
//
// An address a probe may reach is an address this product is about to fetch
// from, so a probe that accepted more than a save would be a way to ask the
// browser to make a request nothing would ever have let it save. The
// comparison is made against the save's own answer rather than against a
// second table, so the two cannot come apart by one of them being updated.
TEST(CoreApiProviderEndpointTest, AProbeAcceptsExactlyWhatASaveAccepts) {
  for (const char *endpoint :
       {"http://192.168.1.9:11434/v1", "http://localhost:11434/v1",
        "https://gateway.example.com/v1", "http://gateway.example.com/v1",
        "http://192.168.1.9:11434/v1?key=secret",
        "http://someone:secret@192.168.1.9/v1", "", "not-an-address"}) {
    EXPECT_EQ(ProbeWithEndpoint(endpoint).refusal,
              SaveWithEndpoint(endpoint).refusal)
        << endpoint;
  }
}

// ---------------------------------------------------------------------------
// The wall between the two, and the agreements across it
// ---------------------------------------------------------------------------

// Each rule accepts addresses the other refuses, which is what makes them two
// rules rather than one with a flag.
//
// If this case ever finds no disagreement in either direction, the rules have
// been merged — by somebody widening the catalog rule to admit a person's
// server, or narrowing the register rule to the catalog's shape. Either is the
// failure decision 0096 section 2 is written to prevent, and neither would be
// visible anywhere else.
TEST(CoreApiProviderEndpointTest, TheTwoRulesDisagreeAndThatIsTheDesign) {
  // Registrable, and not a catalog endpoint: cleartext to a local machine, a
  // port with a path, a trailing slash.
  for (const char *endpoint :
       {"http://192.168.1.9:11434/v1", "http://localhost:11434/v1",
        "https://gateway.example.com/v1", "https://gateway.example.com/"}) {
    EXPECT_EQ(SaveWithEndpoint(endpoint).refusal,
              ProviderRequestRefusal::kNone)
        << endpoint;
    EXPECT_NE(CheckCatalogProviderEndpoint(endpoint),
              ProviderRequestRefusal::kNone)
        << endpoint;
  }

  // A catalog endpoint the register would hold too — the rules overlap, they
  // are simply not the same rule. What is asserted here is only that the
  // overlap is not everything.
  EXPECT_EQ(CheckCatalogProviderEndpoint("https://gateway.example.com"),
            ProviderRequestRefusal::kNone);
  EXPECT_EQ(SaveWithEndpoint("https://gateway.example.com").refusal,
            ProviderRequestRefusal::kNone);
}

class CoreApiProviderEndpointRegisterTest : public testing::Test {
protected:
  CoreApiProviderEndpointRegisterTest() {
    profile_preferences::RegisterProfilePreferences(prefs_.registry());
  }

  TestingPrefServiceSimple prefs_;
};

// An address this factory accepts is one the register can hold, and it holds
// the same bytes.
//
// Two independent applications of one policy sit between a person pressing
// save and a request being sent: this factory refuses an address before an
// operation identity exists, and the register refuses one on the way to disk.
// They ask the same classifier, which is why they agree — and this is the case
// that would fail if either grew an opinion of its own, because the failure it
// would otherwise produce is a save the surface reports as accepted and a
// register that never held the row.
TEST_F(CoreApiProviderEndpointRegisterTest, AnAcceptedAddressIsOneToRegister) {
  const ProviderCommandResult result = SaveWithEndpoint(kOwnServer);
  ASSERT_TRUE(result.command);

  ASSERT_TRUE(WriteCustomProviderEndpoint(&prefs_, kProviderId,
                                          SubmittedEndpoint(result)));
  // Byte for byte, with no normalization anywhere between the two: the
  // comparison at send time is equality against this row, so a register that
  // held a parsed and re-serialized copy would refuse the address it was given.
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, kProviderId),
            std::optional<std::string>(kOwnServer));
}

// And an address the register holds is one the transport will send to.
//
// The third leg of the triangle. `IsValidCoreModelRequest` in its sender form
// is the only thing that decides whether a request naming a person's own
// address leaves the process, and it decides by asking this register — so a
// save, a register row and a send must name one string. A trailing slash added
// anywhere along that path would break this case, which is exactly what it is
// for.
TEST_F(CoreApiProviderEndpointRegisterTest, ARegisteredAddressIsOneToSendTo) {
  const ProviderCommandResult result = SaveWithEndpoint(kOwnServer);
  ASSERT_TRUE(result.command);
  ASSERT_TRUE(WriteCustomProviderEndpoint(&prefs_, kProviderId,
                                          SubmittedEndpoint(result)));

  auto request = service::ModelRequestEffect::New();
  request->route_id = "route-1";
  request->model_id = "model-1";
  request->disclosure = service::DisclosureClass::kUserSelectedContent;
  request->max_output_bytes = 1024u;
  request->provider_id = kProviderId;
  request->wire_api = service::ProviderWireApi::kOpenAiCompletions;
  request->endpoint = SubmittedEndpoint(result);
  request->endpoint_kind = service::ModelEndpointKind::kUserBaseUrl;

  EXPECT_TRUE(IsValidCoreModelRequest(*request, service::kMaxIdentifierBytes,
                                      service::kMaxEffectBytes,
                                      CustomProviderEndpointLookup(&prefs_)));

  // And the same address for a provider nobody registered it against is
  // refused, so the case above is passing on the register rather than on the
  // address being acceptable.
  request->provider_id = "another-gateway";
  EXPECT_FALSE(IsValidCoreModelRequest(*request, service::kMaxIdentifierBytes,
                                       service::kMaxEffectBytes,
                                       CustomProviderEndpointLookup(&prefs_)));
}

// Which of the two refuses a thirty-third provider, and which does not.
//
// The register's bound is the register's alone — the factory has no idea how
// many providers exist and should not, because it holds no file. That split is
// the reason `ProfileCoreApiFacade::SaveCustomProvider` must refuse when the
// registration fails rather than forwarding anyway: the command it is holding
// is perfectly well formed, and forwarding it would tell the core about a
// provider whose address this browser will never recognise. The store's own
// suite owns the bound's behaviour; what is asserted here is only that the two
// answers differ, which is what makes the facade's ordering load-bearing.
TEST_F(CoreApiProviderEndpointRegisterTest, TheRegisterBoundIsNotTheFactorys) {
  const size_t bound = static_cast<size_t>(service::kMaxCustomProviders);
  for (size_t index = 0; index < bound; ++index) {
    ASSERT_TRUE(WriteCustomProviderEndpoint(
        &prefs_, "server-" + std::to_string(index), kOwnServer))
        << index;
  }

  EXPECT_EQ(SaveWithEndpoint(kOwnServer).refusal,
            ProviderRequestRefusal::kNone);
  EXPECT_FALSE(WriteCustomProviderEndpoint(&prefs_, kProviderId, kOwnServer));
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, kProviderId), std::nullopt);
}

} // namespace
} // namespace taffy
