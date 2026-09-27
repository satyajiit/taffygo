// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What an accepted provider request becomes.
//
// The refusals are the other half and live in
// core_api_command_factory_provider_refusal_unittest.cc; the file cap is 400
// lines and the two halves are the same size, so they are two files.
//
// This is where the credential rule is proved. A credential reaches this
// process as an opaque secure-store handle and leaves it in exactly one field;
// `TheCredentialHandleReachesNoFieldButItsOwn` walks every string of both
// projected commands to say so, and the reply shape cannot carry one at all —
// that half is a static_assert in profile_core_api_facade_unittest.cc, where
// the interface is in scope.
//
// The endpoint is not here at all. There are two endpoint rules in this
// factory — one for an address a catalog named, one for an address a person
// typed — and decision 0096 section 2 requires that they never be written in
// terms of each other, so they are asserted together and away from everything
// else, in core_api_command_factory_provider_endpoint_unittest.cc. That file
// also holds the agreements across them: an endpoint this factory accepts is
// one the register can hold and one the transport will send to.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/browser/core_service_command_validation.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNowMs = 1000u;

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

// A save with nothing declared on it. What a declared roster becomes is
// core_api_command_factory_provider_custom_unittest.cc's subject; naming the
// two whole-state fields once here keeps every case below about the field it
// is actually testing.
std::vector<api::CustomModelSpecViewPtr> NoModels() {
  return std::vector<api::CustomModelSpecViewPtr>();
}

std::optional<api::ServerKindView> NoServer() {
  return std::optional<api::ServerKindView>();
}

// Every string either projected command carries, so a test can say where a
// value did and did not travel rather than checking the one field it expected.
std::vector<std::string> EveryString(const ProjectedCoreCommand &projected) {
  const api::CoreCommand &request = *projected.core_api_command;
  const service::CoreServiceCommand &submitted = *projected.core_service_command;
  std::vector<std::string> strings{
      request.operation->operation_id, request.operation->idempotency_key,
      submitted.operation->operation_id, submitted.operation->idempotency_key};
  if (request.save_provider_credential) {
    strings.push_back(request.save_provider_credential->provider_id);
    strings.push_back(request.save_provider_credential->credential_handle);
  }
  if (request.save_custom_provider) {
    strings.push_back(request.save_custom_provider->provider_id);
    strings.push_back(request.save_custom_provider->display_name);
    strings.push_back(request.save_custom_provider->endpoint);
    // Named even though it is the field the handle belongs in. A walk that
    // skipped it would count one copy where there are two and read a leak
    // into a neighbouring field as the legitimate one.
    if (request.save_custom_provider->credential_handle) {
      strings.push_back(*request.save_custom_provider->credential_handle);
    }
  }
  if (submitted.save_provider_credential) {
    strings.push_back(submitted.save_provider_credential->provider_id);
    strings.push_back(submitted.save_provider_credential->credential_handle);
  }
  if (submitted.start_provider_auth) {
    strings.push_back(submitted.start_provider_auth->flow_id);
    strings.push_back(submitted.start_provider_auth->provider_id);
    strings.push_back(submitted.start_provider_auth->redirect_binding_id);
  }
  if (submitted.save_custom_provider) {
    strings.push_back(submitted.save_custom_provider->provider_id);
    strings.push_back(submitted.save_custom_provider->display_name);
    strings.push_back(submitted.save_custom_provider->endpoint);
    if (submitted.save_custom_provider->credential_handle) {
      strings.push_back(*submitted.save_custom_provider->credential_handle);
    }
  }
  return strings;
}

// One saved key, projected onto both contracts.
TEST(CoreApiProviderCommandTest, ASavedCredentialCrossesAsAHandleAndNothing) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildSaveProviderCredential(
      "anthropic", api::ProviderAuthMethodView::kApiKey, "vault-ref-7",
      kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  EXPECT_EQ(result.command->core_api_command->kind,
            api::CoreCommandKind::kSaveProviderCredential);
  ASSERT_TRUE(result.command->core_api_command->save_provider_credential);
  EXPECT_EQ(result.command->core_api_command->save_provider_credential->auth_method,
            api::ProviderAuthMethodView::kApiKey);
  EXPECT_EQ(result.command->core_service_command->kind,
            service::CoreServiceCommandKind::kSaveProviderCredential);
  ASSERT_TRUE(result.command->core_service_command->save_provider_credential);
  const auto &submitted =
      *result.command->core_service_command->save_provider_credential;
  EXPECT_EQ(submitted.provider_id, "anthropic");
  EXPECT_EQ(submitted.credential_handle, "vault-ref-7");
  EXPECT_EQ(submitted.auth_method, service::ProviderAuthMethod::kApiKey);
  EXPECT_EQ(result.command->core_service_command->operation->service_generation,
            kGeneration);
}

TEST(CoreApiProviderCommandTest, ASubscriptionSignInCarriesTheOtherMethod) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildSaveProviderCredential(
      "anthropic", api::ProviderAuthMethodView::kOauth, "vault-ref-7",
      kGeneration, kNowMs);

  ASSERT_TRUE(result.command);
  EXPECT_EQ(result.command->core_service_command->save_provider_credential->auth_method,
            service::ProviderAuthMethod::kOauth);
}

// The rule decision 0049 exists for: a credential is a reference, and it
// reaches exactly one field. Nothing derives an identity from it, nothing
// mixes it into the idempotency key, and nothing carries a second copy.
//
// Both commands that carry a handle are walked, not only the one named for it.
// A custom provider carries its handle beside a display name and an endpoint —
// three caller-supplied strings in one body — which is the shape a stray copy
// would hide in.
TEST(CoreApiProviderCommandTest, TheCredentialHandleReachesNoFieldButItsOwn) {
  const ProviderCommandResult saved =
      NewFactory().BuildSaveProviderCredential(
          "anthropic", api::ProviderAuthMethodView::kApiKey, "vault-ref-7",
          kGeneration, kNowMs);
  const ProviderCommandResult custom = NewFactory().BuildSaveCustomProvider(
      "my-gateway", "My gateway", "https://gateway.example.com",
      api::ProviderWireApiView::kOpenAiResponses, "vault-ref-7", NoModels(),
      NoServer(), kGeneration, kNowMs);

  for (const ProviderCommandResult *result : {&saved, &custom}) {
    ASSERT_TRUE(result->command);
    int carried = 0;
    for (const std::string &value : EveryString(*result->command)) {
      if (value.find("vault-ref-7") != std::string::npos) {
        ++carried;
      }
    }
    // Once on each contract, and in the credential field of each.
    EXPECT_EQ(carried, 2);
  }
}

TEST(CoreApiProviderCommandTest, AForgottenCredentialNamesOnlyTheProvider) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result =
      factory.BuildForgetProviderCredential("anthropic", kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  EXPECT_EQ(result.command->core_service_command->kind,
            service::CoreServiceCommandKind::kForgetProviderCredential);
  ASSERT_TRUE(result.command->core_service_command->forget_provider_credential);
  EXPECT_EQ(result.command->core_service_command->forget_provider_credential->provider_id,
            "anthropic");
}

// The state report carries a name and a closed enum onto both contracts, and
// nothing else: no handle, no material, no field a key could travel in. Both
// projections are asserted because the pair is what the seam sends — a report
// that reached one contract and not the other would be a roster disagreeing
// with the registry it mirrors.
TEST(CoreApiProviderCommandTest, AStateReportProjectsTheStateOntoBothContracts) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildSetProviderCredentialState(
      "xai", api::ProviderCredentialStateView::kRefreshFailed, kGeneration,
      kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  EXPECT_EQ(result.command->core_api_command->kind,
            api::CoreCommandKind::kSetProviderCredentialState);
  ASSERT_TRUE(
      result.command->core_api_command->set_provider_credential_state);
  EXPECT_EQ(result.command->core_api_command->set_provider_credential_state
                ->provider_id,
            "xai");
  EXPECT_EQ(
      result.command->core_api_command->set_provider_credential_state->state,
      api::ProviderCredentialStateView::kRefreshFailed);
  EXPECT_EQ(result.command->core_service_command->kind,
            service::CoreServiceCommandKind::kSetProviderCredentialState);
  ASSERT_TRUE(
      result.command->core_service_command->set_provider_credential_state);
  EXPECT_EQ(result.command->core_service_command->set_provider_credential_state
                ->provider_id,
            "xai");
  EXPECT_EQ(result.command->core_service_command->set_provider_credential_state
                ->state,
            service::ProviderCredentialState::kRefreshFailed);
}

// The probe (decision 0083) carries the same two fields as a save onto both
// contracts, and like the save it moves the handle into exactly one field per
// contract. What it must not do is touch the registry: the projected kind is
// the probe's own, so the record-keeping plane can refuse it as the foreign
// command it is.
TEST(CoreApiProviderCommandTest, AProbeCrossesBothContractsAsItself) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildProbeProviderKey(
      "anthropic", "vault-ref-7", kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  EXPECT_EQ(result.command->core_api_command->kind,
            api::CoreCommandKind::kProbeProviderKey);
  ASSERT_TRUE(result.command->core_api_command->probe_provider_key);
  EXPECT_EQ(result.command->core_api_command->probe_provider_key->provider_id,
            "anthropic");
  EXPECT_EQ(
      result.command->core_api_command->probe_provider_key->credential_handle,
      "vault-ref-7");
  EXPECT_EQ(result.command->core_service_command->kind,
            service::CoreServiceCommandKind::kProbeProviderCredential);
  ASSERT_TRUE(result.command->core_service_command->probe_provider_credential);
  const auto &submitted =
      *result.command->core_service_command->probe_provider_credential;
  EXPECT_EQ(submitted.provider_id, "anthropic");
  EXPECT_EQ(submitted.credential_handle, "vault-ref-7");
}

TEST(CoreApiProviderCommandTest, AProbeRefusesTheFieldsASaveWouldRefuse) {
  CoreApiCommandFactory factory = NewFactory();
  // The same checks as a save, because a probe that could not be saved is a
  // question about a record that could never exist.
  EXPECT_EQ(factory.BuildProbeProviderKey("", "vault-ref-7", kGeneration,
                                          kNowMs)
                .refusal,
            ProviderRequestRefusal::kEmptyProviderId);
  EXPECT_EQ(factory.BuildProbeProviderKey("Anthropic", "vault-ref-7",
                                          kGeneration, kNowMs)
                .refusal,
            ProviderRequestRefusal::kProviderIdAlphabet);
  EXPECT_EQ(
      factory.BuildProbeProviderKey("anthropic", "", kGeneration, kNowMs)
          .refusal,
      ProviderRequestRefusal::kEmptyCredentialHandle);
  EXPECT_EQ(factory
                .BuildProbeProviderKey(
                    "anthropic",
                    std::string(kMaxProviderCredentialHandleBytes + 1u, 'h'),
                    kGeneration, kNowMs)
                .refusal,
            ProviderRequestRefusal::kCredentialHandleTooLong);
}

// The flow identity and the redirect binding are the browser's, never the
// caller's: a surface that chose either could replay one person's redirect
// into another person's flow. There is no parameter for them, and this asserts
// they came from the entropy source instead.
TEST(CoreApiProviderCommandTest, ASignInMintsItsOwnFlowAndRedirectBinding) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result =
      factory.BuildStartProviderAuth("anthropic", kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  ASSERT_TRUE(result.command->core_service_command->start_provider_auth);
  const auto &started = *result.command->core_service_command->start_provider_auth;
  EXPECT_EQ(started.provider_id, "anthropic");
  EXPECT_EQ(started.flow_id, "provider-flow-fixed");
  EXPECT_EQ(started.redirect_binding_id, "provider-binding-fixed");
  EXPECT_NE(started.flow_id, started.redirect_binding_id);
  EXPECT_EQ(started.issued_at_monotonic_ms, kNowMs);
}

TEST(CoreApiProviderCommandTest, CancellationCarriesOneExactFlowIdentity) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildCancelProviderAuth(
      "provider-flow-fixed", kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  ASSERT_TRUE(result.command->core_api_command->cancel_provider_auth);
  ASSERT_TRUE(result.command->core_service_command->cancel_provider_auth);
  EXPECT_EQ(result.command->core_api_command->kind,
            api::CoreCommandKind::kCancelProviderAuth);
  EXPECT_EQ(result.command->core_service_command->kind,
            service::CoreServiceCommandKind::kCancelProviderAuth);
  EXPECT_EQ(result.command->core_api_command->cancel_provider_auth->flow_id,
            "provider-flow-fixed");
  EXPECT_EQ(result.command->core_service_command->cancel_provider_auth->flow_id,
            "provider-flow-fixed");
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *result.command->core_service_command, service::kMaxCommandBytes));
}

TEST(CoreApiProviderCommandTest, CancellationRefusesAbsentOrOversizedIdentity) {
  CoreApiCommandFactory factory = NewFactory();
  EXPECT_EQ(factory.BuildCancelProviderAuth("", kGeneration, kNowMs).refusal,
            ProviderRequestRefusal::kMalformedCommand);
  EXPECT_EQ(factory
                .BuildCancelProviderAuth(
                    std::string(api::kMaxIdentifierBytes + 1u, 'f'),
                    kGeneration, kNowMs)
                .refusal,
            ProviderRequestRefusal::kMalformedCommand);
}

TEST(CoreApiProviderCommandTest, ACustomProviderCarriesItsEndpointVerbatim) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildSaveCustomProvider(
      "my-gateway", "My gateway", "https://gateway.example.com",
      api::ProviderWireApiView::kOpenAiResponses, std::optional<std::string>(),
      NoModels(), NoServer(), kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  ASSERT_TRUE(result.command->core_service_command->save_custom_provider);
  const auto &saved = *result.command->core_service_command->save_custom_provider;
  EXPECT_EQ(saved.provider_id, "my-gateway");
  EXPECT_EQ(saved.display_name, "My gateway");
  EXPECT_EQ(saved.endpoint, "https://gateway.example.com");
  EXPECT_EQ(saved.wire_api, service::ProviderWireApi::kOpenAiResponses);
  // An endpoint with no key of its own is a provider a person reaches without
  // one, not a provider with an empty key.
  EXPECT_FALSE(saved.credential_handle.has_value());
}

// The four families the model router can write. A view that projected onto the
// wrong one would route a person's own endpoint to a body it cannot parse.
TEST(CoreApiProviderCommandTest, EveryWireFamilyProjectsOntoItsOwn) {
  const struct {
    api::ProviderWireApiView view;
    service::ProviderWireApi projected;
  } kFamilies[] = {
      {api::ProviderWireApiView::kAnthropicMessages,
       service::ProviderWireApi::kAnthropicMessages},
      {api::ProviderWireApiView::kOpenAiResponses,
       service::ProviderWireApi::kOpenAiResponses},
      {api::ProviderWireApiView::kOpenAiCompletions,
       service::ProviderWireApi::kOpenAiCompletions},
      {api::ProviderWireApiView::kGoogleGenerativeLanguage,
       service::ProviderWireApi::kGoogleGenerativeLanguage}};

  for (const auto &family : kFamilies) {
    CoreApiCommandFactory factory = NewFactory();
    const ProviderCommandResult result = factory.BuildSaveCustomProvider(
        "my-gateway", "My gateway", "https://gateway.example.com",
        family.view, "vault-ref-7", NoModels(), NoServer(), kGeneration,
        kNowMs);
    ASSERT_TRUE(result.command);
    EXPECT_EQ(result.command->core_service_command->save_custom_provider->wire_api,
              family.projected);
    EXPECT_EQ(result.command->core_service_command->save_custom_provider->credential_handle,
              std::optional<std::string>("vault-ref-7"));
  }
}

// The managed wire is the product's own reserved dialect: a custom provider
// claiming it would put a person's endpoint behind the entitlement bearer.
TEST(CoreApiProviderCommandTest, TheManagedWireIsNotACustomProviderDialect) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildSaveCustomProvider(
      "my-gateway", "My gateway", "https://gateway.example.com",
      api::ProviderWireApiView::kManaged, "vault-ref-7", NoModels(),
      NoServer(), kGeneration, kNowMs);
  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kMalformedCommand);
  EXPECT_FALSE(result.command);
}

TEST(CoreApiProviderCommandTest, ARemovedCustomProviderNamesOnlyTheProvider) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result =
      factory.BuildRemoveCustomProvider("my-gateway", kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  EXPECT_EQ(result.command->core_service_command->kind,
            service::CoreServiceCommandKind::kRemoveCustomProvider);
  ASSERT_TRUE(result.command->core_service_command->remove_custom_provider);
  EXPECT_EQ(result.command->core_service_command->remove_custom_provider->provider_id,
            "my-gateway");
}

} // namespace
} // namespace taffy
