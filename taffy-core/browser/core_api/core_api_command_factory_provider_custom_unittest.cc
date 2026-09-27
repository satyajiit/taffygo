// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a person's own provider configuration becomes, and what it is refused
// for.
//
// The subject is the four builders in
// core_api_command_factory_provider_custom.cc and the one in
// core_api_command_factory_composer.cc. The older two files still own the
// fields those builders inherit — the identity alphabet, the handle bound and
// the endpoint rule — because those reach the same store from every entry
// point; what is asserted here is only what this wave added: a declared model
// roster, a detected server, a standing choice, an endpoint probe, and the
// composer's bounds.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNowMs = 1000u;
constexpr char kEndpoint[] = "https://gateway.example.com";

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

api::CustomModelSpecViewPtr Model(const std::string &model_id,
                                  const std::string &display_name) {
  return api::CustomModelSpecView::New(model_id, display_name,
                                       /*context_window=*/128000u,
                                       /*max_output_tokens=*/4096u,
                                       /*reasoning=*/true,
                                       /*tool_calling=*/true);
}

std::vector<api::CustomModelSpecViewPtr> RosterOf(size_t entries) {
  std::vector<api::CustomModelSpecViewPtr> models;
  for (size_t index = 0; index < entries; ++index) {
    const std::string id = "model-" + std::to_string(index);
    models.push_back(Model(id, "Model " + std::to_string(index)));
  }
  return models;
}

ProviderCommandResult
SaveCustomWith(std::vector<api::CustomModelSpecViewPtr> models,
               std::optional<api::ServerKindView> detected_server) {
  return NewFactory().BuildSaveCustomProvider(
      "my-gateway", "My gateway", kEndpoint,
      api::ProviderWireApiView::kOpenAiResponses, std::optional<std::string>(),
      std::move(models), detected_server, kGeneration, kNowMs);
}

// A declared roster reaches both contracts row for row and field for field. A
// projection that dropped a column would offer a person a model whose window
// or tool support the router then decides from a default.
TEST(CoreApiProviderCustomTest, ADeclaredRosterCrossesRowForRow) {
  std::vector<api::CustomModelSpecViewPtr> models;
  models.push_back(Model("qwen3-30b", "Qwen3 30B"));
  models.push_back(Model("llama-4-scout", "Llama 4 Scout"));
  const ProviderCommandResult result =
      SaveCustomWith(std::move(models), api::ServerKindView::kLmStudio);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  ASSERT_TRUE(result.command->core_service_command->save_custom_provider);
  const auto &saved =
      *result.command->core_service_command->save_custom_provider;
  ASSERT_EQ(saved.models.size(), 2u);
  EXPECT_EQ(saved.models[0]->model_id, "qwen3-30b");
  EXPECT_EQ(saved.models[0]->display_name, "Qwen3 30B");
  EXPECT_EQ(saved.models[0]->context_window, 128000u);
  EXPECT_EQ(saved.models[0]->max_output_tokens, 4096u);
  EXPECT_TRUE(saved.models[0]->reasoning);
  EXPECT_TRUE(saved.models[0]->tool_calling);
  EXPECT_EQ(saved.models[1]->model_id, "llama-4-scout");
  ASSERT_TRUE(saved.detected_server);
  EXPECT_EQ(saved.detected_server->server_kind, service::ServerKind::kLmStudio);
  // And the request half carries the same roster, so the journalled command
  // and the submitted one agree about what was declared.
  ASSERT_TRUE(result.command->core_api_command->save_custom_provider);
  EXPECT_EQ(
      result.command->core_api_command->save_custom_provider->models.size(),
      2u);
}

// Every server kind projects onto its own. One that projected onto a
// neighbour would tell the core a person's LM Studio was an Ollama, and the
// listing route the core then plans is per kind.
TEST(CoreApiProviderCustomTest, EveryServerKindProjectsOntoItsOwn) {
  const struct {
    api::ServerKindView view;
    service::ServerKind projected;
  } kKinds[] = {
      {api::ServerKindView::kOpenaiCompatible,
       service::ServerKind::kOpenaiCompatible},
      {api::ServerKindView::kOllama, service::ServerKind::kOllama},
      {api::ServerKindView::kLmStudio, service::ServerKind::kLmStudio},
      {api::ServerKindView::kVllm, service::ServerKind::kVllm},
      {api::ServerKindView::kLlamaCpp, service::ServerKind::kLlamaCpp}};

  for (const auto &kind : kKinds) {
    const ProviderCommandResult result = SaveCustomWith(RosterOf(1u), kind.view);
    ASSERT_TRUE(result.command);
    const auto &saved =
        *result.command->core_service_command->save_custom_provider;
    ASSERT_TRUE(saved.detected_server);
    EXPECT_EQ(saved.detected_server->server_kind, kind.projected);
  }
}

// No detected server is a provider nothing recognised, which is a real answer
// and not a missing one. It must not become a server kind by default.
TEST(CoreApiProviderCustomTest, AnUnrecognisedServerStaysAbsent) {
  const ProviderCommandResult result =
      SaveCustomWith(RosterOf(1u), std::optional<api::ServerKindView>());

  ASSERT_TRUE(result.command);
  EXPECT_FALSE(result.command->core_service_command->save_custom_provider
                   ->detected_server);
  EXPECT_FALSE(
      result.command->core_api_command->save_custom_provider->detected_server);
}

// The roster bound, from both sides. A roster past it is refused whole rather
// than truncated: a provider silently offering the first thirty-two of a
// person's models is worse than one they were told to shorten.
TEST(CoreApiProviderCustomTest, TheRosterBoundIsRefusedRatherThanTruncated) {
  const ProviderCommandResult largest =
      SaveCustomWith(RosterOf(static_cast<size_t>(api::kMaxCustomModelEntries)),
                     std::optional<api::ServerKindView>());
  EXPECT_EQ(largest.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(largest.command);
  EXPECT_EQ(largest.command->core_service_command->save_custom_provider
                ->models.size(),
            static_cast<size_t>(api::kMaxCustomModelEntries));

  const ProviderCommandResult refused =
      SaveCustomWith(RosterOf(static_cast<size_t>(api::kMaxCustomModelEntries) + 1u),
                     std::optional<api::ServerKindView>());
  EXPECT_EQ(refused.refusal, ProviderRequestRefusal::kTooManyModels);
  EXPECT_FALSE(refused.command);
}

// A row is bounded field by field, and each refusal names the field. A model
// id and a model display name are different fields from the provider's own,
// which is why they have refusals of their own.
TEST(CoreApiProviderCustomTest, EveryRosterRowIsBoundedByItsOwnField) {
  const struct {
    const char *model_id;
    const char *display_name;
    ProviderRequestRefusal refusal;
  } kCases[] = {
      {"", "Qwen3 30B", ProviderRequestRefusal::kEmptyModelId},
      {"qwen3-30b", "", ProviderRequestRefusal::kEmptyModelDisplayName}};

  for (const auto &test_case : kCases) {
    std::vector<api::CustomModelSpecViewPtr> models;
    models.push_back(Model(test_case.model_id, test_case.display_name));
    const ProviderCommandResult result =
        SaveCustomWith(std::move(models), std::optional<api::ServerKindView>());
    EXPECT_EQ(result.refusal, test_case.refusal) << test_case.model_id;
    EXPECT_FALSE(result.command) << test_case.model_id;
  }

  std::vector<api::CustomModelSpecViewPtr> long_id;
  long_id.push_back(
      Model(std::string(api::kMaxModelIdBytes + 1u, 'm'), "Qwen3 30B"));
  const ProviderCommandResult oversized_id = SaveCustomWith(
      std::move(long_id), std::optional<api::ServerKindView>());
  EXPECT_EQ(oversized_id.refusal, ProviderRequestRefusal::kModelIdTooLong);
  EXPECT_FALSE(oversized_id.command);

  std::vector<api::CustomModelSpecViewPtr> long_name;
  long_name.push_back(Model(
      "qwen3-30b", std::string(api::kMaxModelDisplayNameBytes + 1u, 'n')));
  const ProviderCommandResult oversized_name = SaveCustomWith(
      std::move(long_name), std::optional<api::ServerKindView>());
  EXPECT_EQ(oversized_name.refusal,
            ProviderRequestRefusal::kModelDisplayNameTooLong);
  EXPECT_FALSE(oversized_name.command);
}

// Two reserved wire families, refused at both entry points that take one.
//
// The managed wire is the product's own envelope, spoken only to the compiled
// worker origin; the Codex responses wire is one vendor's subscription
// endpoint reached with a subscription credential. A person's own endpoint is
// neither, and the core's provider plane refuses the same pair independently.
TEST(CoreApiProviderCustomTest, TheReservedWireFamiliesAreRefusedAtBothDoors) {
  for (const api::ProviderWireApiView reserved :
       {api::ProviderWireApiView::kManaged,
        api::ProviderWireApiView::kOpenAiCodexResponses}) {
    const ProviderCommandResult saved = NewFactory().BuildSaveCustomProvider(
        "my-gateway", "My gateway", kEndpoint, reserved,
        std::optional<std::string>(),
        std::vector<api::CustomModelSpecViewPtr>(),
        std::optional<api::ServerKindView>(), kGeneration, kNowMs);
    EXPECT_EQ(saved.refusal, ProviderRequestRefusal::kMalformedCommand);
    EXPECT_FALSE(saved.command);

    const ProviderCommandResult probed = NewFactory().BuildProbeCustomEndpoint(
        "my-gateway", kEndpoint, reserved, std::optional<std::string>(),
        kGeneration, kNowMs);
    EXPECT_EQ(probed.refusal, ProviderRequestRefusal::kMalformedCommand);
    EXPECT_FALSE(probed.command);
  }
}

// The probe carries an endpoint and a draft provider identity — the row its
// verdict is filed under, which the save that follows reuses so that probing an
// address and then saving it produces one roster row rather than two. What it
// must keep is the save's endpoint rule: a probe that reported a private
// address reachable would be a probe promising a save that then refuses.
TEST(CoreApiProviderCustomTest, AProbeAppliesTheSavesEndpointRule) {
  const ProviderCommandResult accepted = NewFactory().BuildProbeCustomEndpoint(
      "my-gateway", kEndpoint, api::ProviderWireApiView::kOpenAiCompletions,
      "vault-ref-7", kGeneration, kNowMs);
  EXPECT_EQ(accepted.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(accepted.command);
  EXPECT_EQ(accepted.command->core_api_command->kind,
            api::CoreCommandKind::kProbeCustomEndpoint);
  EXPECT_EQ(accepted.command->core_service_command->kind,
            service::CoreServiceCommandKind::kProbeCustomEndpoint);
  ASSERT_TRUE(accepted.command->core_service_command->probe_custom_endpoint);
  const auto &probe =
      *accepted.command->core_service_command->probe_custom_endpoint;
  EXPECT_EQ(probe.endpoint, kEndpoint);
  EXPECT_EQ(probe.wire_api, service::ProviderWireApi::kOpenAiCompletions);
  EXPECT_EQ(probe.credential_handle, std::optional<std::string>("vault-ref-7"));
  EXPECT_EQ(probe.provider_id, "my-gateway");

  // The probe refuses exactly what the save refuses, because an address a
  // probe may reach is an address this product is about to fetch from. The
  // rule is decision 0096 section 3's, so a port, a base path and plain http to
  // a literal local machine are all accepted at both doors; what is refused is
  // cleartext to anything else, and the address forms that carry something an
  // endpoint may not carry.
  //
  // The whole comparison, across both doors and every form, is
  // `AProbeAcceptsExactlyWhatASaveAccepts` in
  // core_api_command_factory_provider_endpoint_unittest.cc, which asserts the
  // two answers are equal rather than checking each against a second table.
  for (const char *refused :
       {"http://gateway.example.com", "http://192.168.1.9:11434/v1?key=x",
        "not-an-address"}) {
    const ProviderCommandResult result = NewFactory().BuildProbeCustomEndpoint(
        "my-gateway", refused, api::ProviderWireApiView::kOpenAiCompletions,
        std::optional<std::string>(), kGeneration, kNowMs);
    EXPECT_NE(result.refusal, ProviderRequestRefusal::kNone) << refused;
    EXPECT_FALSE(result.command) << refused;
  }

  // And the address a person actually has, which the old rule refused at this
  // door and the register rule admits.
  const ProviderCommandResult own_server =
      NewFactory().BuildProbeCustomEndpoint(
          "my-gateway", "http://192.168.1.9:11434/v1",
          api::ProviderWireApiView::kOpenAiCompletions,
          std::optional<std::string>(), kGeneration, kNowMs);
  EXPECT_EQ(own_server.refusal, ProviderRequestRefusal::kNone);
  EXPECT_TRUE(own_server.command);

  // The draft identity is held to the save's rule, so a spelling the save
  // would refuse is refused here too. A verdict filed under a row that can
  // never be created is a verdict no screen can find.
  const ProviderCommandResult bad_draft =
      NewFactory().BuildProbeCustomEndpoint(
          "My Gateway", kEndpoint, api::ProviderWireApiView::kOpenAiCompletions,
          std::optional<std::string>(), kGeneration, kNowMs);
  EXPECT_EQ(bad_draft.refusal, ProviderRequestRefusal::kProviderIdAlphabet);
  EXPECT_FALSE(bad_draft.command);
}

// Every rung of the thinking ladder crosses as itself. A projection that
// slipped by one would answer a person's "high" with the model's "medium", and
// nothing on either side of the wire would say so.
TEST(CoreApiProviderCustomTest, EveryThinkingRungCrossesAsItself) {
  const struct {
    api::ThinkingLevelView view;
    service::ThinkingLevel projected;
  } kRungs[] = {{api::ThinkingLevelView::kOff, service::ThinkingLevel::kOff},
                {api::ThinkingLevelView::kMinimal,
                 service::ThinkingLevel::kMinimal},
                {api::ThinkingLevelView::kLow, service::ThinkingLevel::kLow},
                {api::ThinkingLevelView::kMedium,
                 service::ThinkingLevel::kMedium},
                {api::ThinkingLevelView::kHigh, service::ThinkingLevel::kHigh},
                {api::ThinkingLevelView::kXhigh,
                 service::ThinkingLevel::kXhigh},
                {api::ThinkingLevelView::kMax, service::ThinkingLevel::kMax}};
  // The ladder is closed, so the table above has to be the whole of it.
  static_assert(sizeof(kRungs) / sizeof(kRungs[0]) ==
                api::kMaxModelThinkingLevels);

  for (const auto &rung : kRungs) {
    const ProviderCommandResult result =
        NewFactory().BuildSetProviderModelPreference(
            "anthropic", std::optional<std::string>("claude-sonnet-4-5"),
            rung.view, kGeneration, kNowMs);
    ASSERT_TRUE(result.command);
    const auto &preference =
        *result.command->core_service_command->set_provider_model_preference;
    ASSERT_TRUE(preference.thinking);
    EXPECT_EQ(preference.thinking->level, rung.projected);
    EXPECT_EQ(preference.model_id,
              std::optional<std::string>("claude-sonnet-4-5"));
    ASSERT_TRUE(
        result.command->core_api_command->set_provider_model_preference);
    ASSERT_TRUE(result.command->core_api_command->set_provider_model_preference
                    ->thinking);
    EXPECT_EQ(result.command->core_api_command->set_provider_model_preference
                  ->thinking->level,
              rung.view);
  }
}

// Both fields absent is the request to clear the choice, and it is accepted.
// Refusing it would leave a person able to make a choice and unable to unmake
// one, and "Taffy decides" is not the same answer as the lowest rung.
TEST(CoreApiProviderCustomTest, ClearingAPreferenceIsARequestAndNotAMistake) {
  const ProviderCommandResult result =
      NewFactory().BuildSetProviderModelPreference(
          "anthropic", std::optional<std::string>(),
          std::optional<api::ThinkingLevelView>(), kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kNone);
  ASSERT_TRUE(result.command);
  const auto &preference =
      *result.command->core_service_command->set_provider_model_preference;
  EXPECT_EQ(preference.provider_id, "anthropic");
  EXPECT_FALSE(preference.model_id.has_value());
  EXPECT_FALSE(preference.thinking);
}

TEST(CoreApiProviderCustomTest, APreferenceIsBoundedByTheModelIdField) {
  const ProviderCommandResult empty =
      NewFactory().BuildSetProviderModelPreference(
          "anthropic", std::optional<std::string>(""),
          std::optional<api::ThinkingLevelView>(), kGeneration, kNowMs);
  EXPECT_EQ(empty.refusal, ProviderRequestRefusal::kEmptyModelId);
  EXPECT_FALSE(empty.command);

  const ProviderCommandResult oversized =
      NewFactory().BuildSetProviderModelPreference(
          "anthropic",
          std::optional<std::string>(std::string(api::kMaxModelIdBytes + 1u,
                                                 'm')),
          std::optional<api::ThinkingLevelView>(), kGeneration, kNowMs);
  EXPECT_EQ(oversized.refusal, ProviderRequestRefusal::kModelIdTooLong);
  EXPECT_FALSE(oversized.command);

  const ProviderCommandResult identity =
      NewFactory().BuildSetProviderModelPreference(
          "Anthropic", std::optional<std::string>("claude-sonnet-4-5"),
          std::optional<api::ThinkingLevelView>(), kGeneration, kNowMs);
  EXPECT_EQ(identity.refusal, ProviderRequestRefusal::kProviderIdAlphabet);
  EXPECT_FALSE(identity.command);
}

// The composer's three bounds, each from both sides. They are the contract's
// own numbers rather than numbers chosen here, so a contract that moved one
// and a browser that did not is a failing test rather than a request the core
// refuses with nothing a person could read.
TEST(CoreApiProviderCustomTest, TheComposerRequestIsBoundedOnEveryField) {
  const std::string longest_prefix(api::kMaxComposerPrefixBytes, 'p');
  const std::string longest_suffix(api::kMaxComposerSuffixBytes, 's');
  const std::string longest_id(api::kMaxIdentifierBytes, 'r');

  EXPECT_TRUE(NewFactory()
                  .BuildRequestComposerCompletion(longest_id, longest_prefix,
                                                 longest_suffix, kGeneration,
                                                 kNowMs)
                  .has_value());
  // No suffix is a caret at the end of what has been typed, which is the
  // ordinary case rather than a missing field.
  EXPECT_TRUE(NewFactory()
                  .BuildRequestComposerCompletion(
                      "composer-1", "the quick brown ",
                      std::optional<std::string>(), kGeneration, kNowMs)
                  .has_value());

  EXPECT_FALSE(NewFactory()
                   .BuildRequestComposerCompletion(
                       "", "the quick brown ", std::optional<std::string>(),
                       kGeneration, kNowMs)
                   .has_value());
  EXPECT_FALSE(NewFactory()
                   .BuildRequestComposerCompletion(
                       std::string(api::kMaxIdentifierBytes + 1u, 'r'),
                       "the quick brown ", std::optional<std::string>(),
                       kGeneration, kNowMs)
                   .has_value());
  // An empty prefix is refused: there is nothing to continue, and asking a
  // model to continue nothing spends a turn to be told so.
  EXPECT_FALSE(NewFactory()
                   .BuildRequestComposerCompletion(
                       "composer-1", "", std::optional<std::string>(),
                       kGeneration, kNowMs)
                   .has_value());
  EXPECT_FALSE(NewFactory()
                   .BuildRequestComposerCompletion(
                       "composer-1",
                       std::string(api::kMaxComposerPrefixBytes + 1u, 'p'),
                       std::optional<std::string>(), kGeneration, kNowMs)
                   .has_value());
  EXPECT_FALSE(NewFactory()
                   .BuildRequestComposerCompletion(
                       "composer-1", "the quick brown ",
                       std::optional<std::string>(std::string(
                           api::kMaxComposerSuffixBytes + 1u, 's')),
                       kGeneration, kNowMs)
                   .has_value());
}

TEST(CoreApiProviderCustomTest, AComposerRequestCrossesBothContractsAsItself) {
  const std::optional<ProjectedCoreCommand> projected =
      NewFactory().BuildRequestComposerCompletion(
          "composer-1", "the quick brown ",
          std::optional<std::string>(" over the lazy dog"), kGeneration,
          kNowMs);

  ASSERT_TRUE(projected);
  EXPECT_EQ(projected->core_api_command->kind,
            api::CoreCommandKind::kRequestComposerCompletion);
  ASSERT_TRUE(projected->core_api_command->request_composer_completion);
  EXPECT_EQ(projected->core_service_command->kind,
            service::CoreServiceCommandKind::kRequestComposerCompletion);
  ASSERT_TRUE(projected->core_service_command->request_composer_completion);
  const auto &composing =
      *projected->core_service_command->request_composer_completion;
  EXPECT_EQ(composing.request_id, "composer-1");
  EXPECT_EQ(composing.prefix, "the quick brown ");
  EXPECT_EQ(composing.suffix, std::optional<std::string>(" over the lazy dog"));
}

} // namespace
} // namespace taffy
