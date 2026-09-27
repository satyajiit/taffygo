// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the browser's own preference file holds about a person's own model
// server, and what it refuses to hold.
//
// Two properties are the reason this file exists. The first is that what is
// stored is the typed string itself: a request naming a person's address is
// answered by comparing it with a row here byte for byte, and a normalized
// copy would be a comparison against something nobody typed. The second is
// that a row which does not read back as an address is dropped rather than
// repaired, because an address nobody typed must not become one the sandboxed
// core may name.

#include "taffy/browser/model/custom_provider_endpoint_store.h"

#include <stddef.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kLocalEndpoint[] = "http://192.168.1.9:11434/v1";

class CustomProviderEndpointStoreTest : public testing::Test {
protected:
  CustomProviderEndpointStoreTest() {
    profile_preferences::RegisterProfilePreferences(prefs_.registry());
  }

  // The stored dictionary as it actually is on disk, for the cases that are
  // about a value this store did not write.
  void PutRaw(base::DictValue stored) {
    prefs_.SetDict(profile_preferences::kCustomProviderEndpoints,
                   std::move(stored));
  }

  std::vector<CustomProviderEndpoint> Read() {
    return ReadCustomProviderEndpoints(prefs_);
  }

  TestingPrefServiceSimple prefs_;
};

// The string goes to disk and comes back unchanged. A store that parsed and
// re-serialized would answer a later byte comparison with a spelling nobody
// typed, and the comparison would fail on an address that is perfectly good.
TEST_F(CustomProviderEndpointStoreTest, TheTypedStringRoundTripsExactly) {
  ASSERT_TRUE(
      WriteCustomProviderEndpoint(&prefs_, "my-server", kLocalEndpoint));

  const std::vector<CustomProviderEndpoint> stored = Read();
  ASSERT_EQ(stored.size(), 1u);
  EXPECT_EQ(stored[0].provider_id, "my-server");
  EXPECT_EQ(stored[0].endpoint, kLocalEndpoint);
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, "my-server"),
            std::optional<std::string>(kLocalEndpoint));
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, "some-other-server"),
            std::nullopt);
}

// Registration is where the address policy is applied, and it is the only
// place it is applied. A save that would put an unacceptable address in the
// register is refused, and nothing enters that a later request could then be
// matched against.
TEST_F(CustomProviderEndpointStoreTest, ThePolicyDecidesWhatMayEnter) {
  EXPECT_FALSE(WriteCustomProviderEndpoint(&prefs_, "my-server",
                                           "http://models.example.test/v1"));
  EXPECT_FALSE(WriteCustomProviderEndpoint(&prefs_, "my-server",
                                           "https://a:b@example.test/v1"));
  EXPECT_FALSE(WriteCustomProviderEndpoint(&prefs_, "my-server", ""));
  EXPECT_TRUE(Read().empty());

  EXPECT_TRUE(WriteCustomProviderEndpoint(&prefs_, "my-server",
                                          "https://models.example.test/v1"));
}

// A write replaces the row rather than adding beside it: one provider has one
// address, and a register holding two for the same provider would accept a
// request naming either.
TEST_F(CustomProviderEndpointStoreTest, AWriteReplacesTheRow) {
  ASSERT_TRUE(
      WriteCustomProviderEndpoint(&prefs_, "my-server", kLocalEndpoint));
  ASSERT_TRUE(WriteCustomProviderEndpoint(&prefs_, "my-server",
                                          "http://127.0.0.1:8000/v1"));

  const std::vector<CustomProviderEndpoint> stored = Read();
  ASSERT_EQ(stored.size(), 1u);
  EXPECT_EQ(stored[0].endpoint, "http://127.0.0.1:8000/v1");
}

// The rows a file can hold that are not addresses at all: a value that is not
// a string, an empty one, and one past the contract's bound. Each is skipped
// rather than repaired, and the single lookup agrees with the listing about
// every one of them.
TEST_F(CustomProviderEndpointStoreTest, ARowThatIsNotAnAddressIsSkipped) {
  base::DictValue stored;
  stored.Set("not-a-string", 7);
  stored.Set("empty", "");
  stored.Set("too-long",
             std::string(static_cast<size_t>(mojom::kMaxProviderEndpointBytes) +
                             1u,
                         'a'));
  stored.Set("real", kLocalEndpoint);
  PutRaw(std::move(stored));

  const std::vector<CustomProviderEndpoint> kept = Read();
  ASSERT_EQ(kept.size(), 1u);
  EXPECT_EQ(kept[0].provider_id, "real");
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, "not-a-string"), std::nullopt);
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, "empty"), std::nullopt);
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, "too-long"), std::nullopt);
}

// A row that the policy would refuse today but which is already on disk is
// still read back. Judging it here would be a second authority over the
// address, and two authorities that can disagree are how an endpoint a person
// saved becomes one they cannot use — the byte comparison at send time is
// what decides, and it decides against the register as it stands.
TEST_F(CustomProviderEndpointStoreTest, AStoredRowIsNotJudgedAgainOnTheWayOut) {
  base::DictValue stored;
  stored.Set("hand-edited", "http://models.example.test/v1");
  PutRaw(std::move(stored));

  const std::vector<CustomProviderEndpoint> kept = Read();
  ASSERT_EQ(kept.size(), 1u);
  EXPECT_EQ(kept[0].endpoint, "http://models.example.test/v1");
}

// The custom-provider bound refuses a new provider and never an existing one.
TEST_F(CustomProviderEndpointStoreTest, TheBoundRefusesOnlyNewRows) {
  const size_t bound = static_cast<size_t>(mojom::kMaxCustomProviders);
  for (size_t index = 0; index < bound; ++index) {
    ASSERT_TRUE(WriteCustomProviderEndpoint(
        &prefs_, "server-" + std::to_string(index), kLocalEndpoint))
        << index;
  }
  EXPECT_EQ(Read().size(), bound);

  EXPECT_FALSE(
      WriteCustomProviderEndpoint(&prefs_, "one-too-many", kLocalEndpoint));
  EXPECT_TRUE(WriteCustomProviderEndpoint(&prefs_, "server-0",
                                          "http://127.0.0.1:8000/v1"));
  EXPECT_EQ(Read().size(), bound);
}

// Forgetting succeeds even for a provider that never had an address: the state
// afterwards is the state that was asked for.
TEST_F(CustomProviderEndpointStoreTest, ForgettingLeavesNothingBehind) {
  ASSERT_TRUE(
      WriteCustomProviderEndpoint(&prefs_, "my-server", kLocalEndpoint));

  EXPECT_TRUE(ForgetCustomProviderEndpoint(&prefs_, "my-server"));
  EXPECT_TRUE(Read().empty());
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, "my-server"), std::nullopt);
  EXPECT_TRUE(ForgetCustomProviderEndpoint(&prefs_, "never-registered"));
}

// The lookup the sender installs answers from this same file, and a profile
// with no preference service yields no lookup at all — which the sender reads
// as a refusal, never as an empty register that accepts nothing by accident.
TEST_F(CustomProviderEndpointStoreTest, TheLookupIsThisFileAndNothingElse) {
  ASSERT_TRUE(
      WriteCustomProviderEndpoint(&prefs_, "my-server", kLocalEndpoint));

  const RegisteredEndpointLookup lookup =
      CustomProviderEndpointLookup(&prefs_);
  ASSERT_FALSE(lookup.is_null());
  EXPECT_EQ(lookup.Run("my-server"),
            std::optional<std::string>(kLocalEndpoint));
  EXPECT_EQ(lookup.Run("some-other-server"), std::nullopt);

  EXPECT_TRUE(CustomProviderEndpointLookup(nullptr).is_null());
}

// Two refusals that are about the caller rather than about the address: a
// profile with no preference service, and an identity a dictionary cannot be
// keyed by.
TEST_F(CustomProviderEndpointStoreTest, AWriteWithNowhereToGoAnswersFalse) {
  EXPECT_FALSE(
      WriteCustomProviderEndpoint(nullptr, "my-server", kLocalEndpoint));
  EXPECT_FALSE(WriteCustomProviderEndpoint(&prefs_, "", kLocalEndpoint));
  EXPECT_FALSE(ForgetCustomProviderEndpoint(nullptr, "my-server"));
  EXPECT_TRUE(Read().empty());
}

// The whole definition, which is what a new core generation has to be told to
// know the provider exists at all (decision 0117).
TEST_F(CustomProviderEndpointStoreTest, ADefinitionRoundTripsWhole) {
  CustomProviderDefinition definition;
  definition.provider_id = "bench";
  definition.display_name = "Bench";
  definition.endpoint = kLocalEndpoint;
  definition.wire_api = core_api::mojom::ProviderWireApiView::kOpenAiCompletions;
  definition.credential_handle = "bench";
  definition.detected_server = core_api::mojom::ServerKindView::kVllm;
  definition.models.push_back(
      CustomProviderModel{"local-reasoner", "local-reasoner", 32768u, 4096u,
                          /*reasoning=*/true, /*tool_calling=*/false});
  ASSERT_TRUE(WriteCustomProviderDefinition(&prefs_, definition));

  const std::vector<CustomProviderDefinition> defined =
      ReadCustomProviderDefinitions(prefs_);
  ASSERT_EQ(1u, defined.size());
  EXPECT_EQ("bench", defined[0].provider_id);
  EXPECT_EQ("Bench", defined[0].display_name);
  EXPECT_EQ(kLocalEndpoint, defined[0].endpoint);
  EXPECT_EQ(core_api::mojom::ProviderWireApiView::kOpenAiCompletions,
            defined[0].wire_api);
  EXPECT_EQ(std::optional<std::string>("bench"), defined[0].credential_handle);
  ASSERT_TRUE(defined[0].detected_server);
  EXPECT_EQ(core_api::mojom::ServerKindView::kVllm,
            *defined[0].detected_server);
  ASSERT_EQ(1u, defined[0].models.size());
  EXPECT_EQ("local-reasoner", defined[0].models[0].model_id);
  EXPECT_EQ(32768u, defined[0].models[0].context_window);
  EXPECT_EQ(4096u, defined[0].models[0].max_output_tokens);
  EXPECT_TRUE(defined[0].models[0].reasoning);
  EXPECT_FALSE(defined[0].models[0].tool_calling);

  // And the address is the same row's, so the register still answers the one
  // question a model request asks it.
  EXPECT_EQ(ReadCustomProviderEndpoint(prefs_, "bench"),
            std::optional<std::string>(kLocalEndpoint));
}

// A model server on somebody's own machine often needs no key at all, and
// absent is not the empty string.
TEST_F(CustomProviderEndpointStoreTest, ADefinitionMayCarryNoCredential) {
  CustomProviderDefinition definition;
  definition.provider_id = "bench";
  definition.display_name = "Bench";
  definition.endpoint = kLocalEndpoint;
  definition.wire_api = core_api::mojom::ProviderWireApiView::kOpenAiCompletions;
  ASSERT_TRUE(WriteCustomProviderDefinition(&prefs_, definition));

  const std::vector<CustomProviderDefinition> defined =
      ReadCustomProviderDefinitions(prefs_);
  ASSERT_EQ(1u, defined.size());
  EXPECT_FALSE(defined[0].credential_handle);
  EXPECT_FALSE(defined[0].detected_server);
  EXPECT_TRUE(defined[0].models.empty());
}

// The address policy decides this write too. A definition carrying an address
// the register may not hold is refused whole rather than filed without one.
TEST_F(CustomProviderEndpointStoreTest, ADefinitionIsHeldToTheAddressPolicy) {
  CustomProviderDefinition definition;
  definition.provider_id = "bench";
  definition.display_name = "Bench";
  definition.endpoint = "http://models.example.com/v1";
  definition.wire_api = core_api::mojom::ProviderWireApiView::kOpenAiCompletions;
  EXPECT_FALSE(WriteCustomProviderDefinition(&prefs_, definition));
  EXPECT_TRUE(Read().empty());
  EXPECT_TRUE(ReadCustomProviderDefinitions(prefs_).empty());
}

// Registering an address alone over a definition keeps the rest of the row.
// Replacing it with a bare address would leave the provider reachable in this
// generation and gone from the next one, with nothing to say why.
TEST_F(CustomProviderEndpointStoreTest, AnAddressWriteKeepsTheDefinition) {
  CustomProviderDefinition definition;
  definition.provider_id = "bench";
  definition.display_name = "Bench";
  definition.endpoint = kLocalEndpoint;
  definition.wire_api = core_api::mojom::ProviderWireApiView::kOpenAiCompletions;
  ASSERT_TRUE(WriteCustomProviderDefinition(&prefs_, definition));

  ASSERT_TRUE(WriteCustomProviderEndpoint(&prefs_, "bench",
                                          "http://127.0.0.1:8000/v1"));
  const std::vector<CustomProviderDefinition> defined =
      ReadCustomProviderDefinitions(prefs_);
  ASSERT_EQ(1u, defined.size());
  EXPECT_EQ("Bench", defined[0].display_name);
  EXPECT_EQ("http://127.0.0.1:8000/v1", defined[0].endpoint);
}

// A row carrying only an address registers what it registers and is not a
// definition. Completing it here would file a provider a person can select and
// nothing can route to.
TEST_F(CustomProviderEndpointStoreTest, AnAddressAloneIsNotADefinition) {
  ASSERT_TRUE(WriteCustomProviderEndpoint(&prefs_, "bench", kLocalEndpoint));
  EXPECT_EQ(1u, Read().size());
  EXPECT_TRUE(ReadCustomProviderDefinitions(prefs_).empty());
}

// Every one of these parsed. Repairing any of them would replay a provider
// nobody defined, or one missing the model they set it up for.
TEST_F(CustomProviderEndpointStoreTest, ATornDefinitionIsDropped) {
  base::DictValue stored;
  base::DictValue no_name;
  no_name.Set("endpoint", kLocalEndpoint);
  no_name.Set("wire_api", "OPEN_AI_COMPLETIONS");
  no_name.Set("models", base::ListValue());
  stored.Set("no-name", std::move(no_name));

  base::DictValue unknown_wire;
  unknown_wire.Set("endpoint", kLocalEndpoint);
  unknown_wire.Set("display_name", "Bench");
  unknown_wire.Set("wire_api", "COHERE_CHAT");
  unknown_wire.Set("models", base::ListValue());
  stored.Set("unknown-wire", std::move(unknown_wire));

  base::DictValue unknown_runtime;
  unknown_runtime.Set("endpoint", kLocalEndpoint);
  unknown_runtime.Set("display_name", "Bench");
  unknown_runtime.Set("wire_api", "OPEN_AI_COMPLETIONS");
  unknown_runtime.Set("detected_server", "MLX");
  unknown_runtime.Set("models", base::ListValue());
  stored.Set("unknown-runtime", std::move(unknown_runtime));

  base::ListValue torn_models;
  base::DictValue torn_model;
  torn_model.Set("model_id", "local-reasoner");
  torn_model.Set("display_name", "local-reasoner");
  torn_model.Set("context_window", -1);
  torn_model.Set("max_output_tokens", 0);
  torn_model.Set("reasoning", false);
  torn_model.Set("tool_calling", false);
  torn_models.Append(std::move(torn_model));
  base::DictValue torn_row;
  torn_row.Set("endpoint", kLocalEndpoint);
  torn_row.Set("display_name", "Bench");
  torn_row.Set("wire_api", "OPEN_AI_COMPLETIONS");
  torn_row.Set("models", std::move(torn_models));
  stored.Set("torn-model", std::move(torn_row));

  base::DictValue whole;
  whole.Set("endpoint", kLocalEndpoint);
  whole.Set("display_name", "Bench");
  whole.Set("wire_api", "OPEN_AI_COMPLETIONS");
  whole.Set("models", base::ListValue());
  stored.Set("bench", std::move(whole));
  PutRaw(std::move(stored));

  const std::vector<CustomProviderDefinition> defined =
      ReadCustomProviderDefinitions(prefs_);
  ASSERT_EQ(1u, defined.size());
  EXPECT_EQ("bench", defined[0].provider_id);
  // Every torn row still registered its address, because that question is a
  // different one and this file is still the authority over it.
  EXPECT_EQ(5u, Read().size());
}

// A number on disk is an ordinal, and a renumbered enumeration would read
// every row back as a different member with nothing to say so.
TEST_F(CustomProviderEndpointStoreTest, NeitherEnumerationIsANumber) {
  CustomProviderDefinition definition;
  definition.provider_id = "bench";
  definition.display_name = "Bench";
  definition.endpoint = kLocalEndpoint;
  definition.wire_api = core_api::mojom::ProviderWireApiView::kOpenAiResponses;
  definition.detected_server = core_api::mojom::ServerKindView::kLmStudio;
  ASSERT_TRUE(WriteCustomProviderDefinition(&prefs_, definition));

  const base::DictValue &stored =
      prefs_.GetDict(profile_preferences::kCustomProviderEndpoints);
  const base::DictValue *row = stored.FindDict("bench");
  ASSERT_TRUE(row);
  const std::string *wire_api = row->FindString("wire_api");
  const std::string *detected = row->FindString("detected_server");
  ASSERT_TRUE(wire_api);
  ASSERT_TRUE(detected);
  EXPECT_EQ("OPEN_AI_RESPONSES", *wire_api);
  EXPECT_EQ("LM_STUDIO", *detected);
}

} // namespace
} // namespace taffy
