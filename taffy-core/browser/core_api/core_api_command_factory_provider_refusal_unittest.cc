// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Every way a provider request is refused, and the name it is refused by —
// except the endpoint.
//
// Split from core_api_command_factory_provider_unittest.cc, which asserts what
// an accepted request becomes. The two halves are the same size and the file
// cap is 400 lines, so they are two files rather than one long one.
//
// The endpoint is a third file, core_api_command_factory_provider_endpoint_
// unittest.cc, and it is separate for a reason that is not size: there are two
// endpoint rules, decision 0096 section 2 says they must never be written in
// terms of each other, and a suite that asserted both beside the identity and
// handle rules would be the place somebody eventually shares a fixture between
// them.
//
// Two properties are asserted in every case and neither is decoration. The
// first is the reason: the wire answer is `INVALID_REQUEST` for all of them,
// so `ProviderRequestRefusal` is the only place a surface can learn which
// field was wrong, and a check that silently started naming a different field
// would be invisible on the wire. The second is that nothing was built — a
// refused request must leave no operation identity and no idempotency key
// behind it, because the browser journals an effect identity before it
// dispatches and refuses one it has already journaled.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

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

// A save with nothing declared on it. The roster and the detected server are
// whole-state fields this suite never varies, and naming them once keeps every
// case below about the one field it is testing.
std::vector<api::CustomModelSpecViewPtr> NoModels() {
  return std::vector<api::CustomModelSpecViewPtr>();
}

std::optional<api::ServerKindView> NoServer() {
  return std::optional<api::ServerKindView>();
}

// The six entry points, each reduced to the one field this suite varies.
ProviderCommandResult SaveCredentialFor(const std::string &provider_id) {
  return NewFactory().BuildSaveProviderCredential(
      provider_id, api::ProviderAuthMethodView::kApiKey, "vault-ref-7",
      kGeneration, kNowMs);
}

ProviderCommandResult ForgetCredentialFor(const std::string &provider_id) {
  return NewFactory().BuildForgetProviderCredential(provider_id, kGeneration,
                                                    kNowMs);
}

ProviderCommandResult ReportStateFor(const std::string &provider_id) {
  return NewFactory().BuildSetProviderCredentialState(
      provider_id, api::ProviderCredentialStateView::kNeedsSignIn, kGeneration,
      kNowMs);
}

ProviderCommandResult StartAuthFor(const std::string &provider_id) {
  return NewFactory().BuildStartProviderAuth(provider_id, kGeneration, kNowMs);
}

ProviderCommandResult SaveCustomFor(const std::string &provider_id) {
  return NewFactory().BuildSaveCustomProvider(
      provider_id, "My gateway", kEndpoint,
      api::ProviderWireApiView::kOpenAiResponses, std::optional<std::string>(),
      NoModels(), NoServer(), kGeneration, kNowMs);
}

ProviderCommandResult RemoveCustomFor(const std::string &provider_id) {
  return NewFactory().BuildRemoveCustomProvider(provider_id, kGeneration,
                                                kNowMs);
}

using ProviderEntryPoint = ProviderCommandResult (*)(const std::string &);

constexpr ProviderEntryPoint kEveryEntryPoint[] = {
    &SaveCredentialFor, &ForgetCredentialFor, &ReportStateFor, &StartAuthFor,
    &SaveCustomFor, &RemoveCustomFor};

ProviderCommandResult SaveCustomNamed(const std::string &display_name) {
  return NewFactory().BuildSaveCustomProvider(
      "my-gateway", display_name, kEndpoint,
      api::ProviderWireApiView::kOpenAiResponses, std::optional<std::string>(),
      NoModels(), NoServer(), kGeneration, kNowMs);
}

// The identity rule holds at every entry point, not only at the one it was
// written for. All six reach the same store, so a request one of them accepts
// and another refuses would be a provider a person can create and cannot
// delete.
TEST(CoreApiProviderRefusalTest, EveryEntryPointRefusesTheSameIdentities) {
  const struct {
    const char *provider_id;
    ProviderRequestRefusal refusal;
  } kCases[] = {
      {"", ProviderRequestRefusal::kEmptyProviderId},
      // The store's alphabet is [a-z0-9][a-z0-9-]{0,63}. Upper case, a leading
      // separator, an underscore and a space are each outside it, and the
      // leading separator matters most: an identity that sorts ahead of every
      // real one is an identity that can be confused with a prefix.
      {"Anthropic", ProviderRequestRefusal::kProviderIdAlphabet},
      {"-anthropic", ProviderRequestRefusal::kProviderIdAlphabet},
      {"my_gateway", ProviderRequestRefusal::kProviderIdAlphabet},
      {"my gateway", ProviderRequestRefusal::kProviderIdAlphabet},
      {"gateway.example.com", ProviderRequestRefusal::kProviderIdAlphabet}};

  for (const auto &entry_point : kEveryEntryPoint) {
    for (const auto &test_case : kCases) {
      const ProviderCommandResult result = entry_point(test_case.provider_id);
      EXPECT_EQ(result.refusal, test_case.refusal) << test_case.provider_id;
      EXPECT_FALSE(result.command) << test_case.provider_id;
    }
  }
}

// The provider identity is bounded by its own limit rather than by the generic
// identifier bound, because the Android store cannot file a record under a
// longer one. The boundary is asserted from both sides so that a change to
// either constant is a failure here rather than a refusal on a device.
TEST(CoreApiProviderRefusalTest, TheIdentityBoundIsTheStoresAndNotGeneric) {
  const std::string longest(api::kMaxProviderIdBytes, 'a');
  const std::string too_long(api::kMaxProviderIdBytes + 1u, 'a');

  for (const auto &entry_point : kEveryEntryPoint) {
    const ProviderCommandResult accepted = entry_point(longest);
    EXPECT_EQ(accepted.refusal, ProviderRequestRefusal::kNone);
    EXPECT_TRUE(accepted.command);

    const ProviderCommandResult refused = entry_point(too_long);
    EXPECT_EQ(refused.refusal, ProviderRequestRefusal::kProviderIdTooLong);
    EXPECT_FALSE(refused.command);
  }
}

// A handle is measured and never read. An empty one is refused rather than
// stored because it would reach the provider and be refused there, spending a
// person's attempt to learn something this process already knew.
//
// The bound is the core's 128 and not the contract's account bound of 256.
// Both numbers are named here so that the day either moves, this is a failing
// test rather than a handle the browser accepts and the core then refuses with
// nothing but `INVALID_COMMAND` — a refusal with no field in it, after an
// operation identity had already been spent on the request.
TEST(CoreApiProviderRefusalTest, ACredentialHandleIsBoundedAndNeverEmpty) {
  static_assert(kMaxProviderCredentialHandleBytes <
                api::kMaxAuthCredentialHandleBytes);

  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult empty = factory.BuildSaveProviderCredential(
      "anthropic", api::ProviderAuthMethodView::kApiKey, "", kGeneration,
      kNowMs);
  EXPECT_EQ(empty.refusal, ProviderRequestRefusal::kEmptyCredentialHandle);
  EXPECT_FALSE(empty.command);

  const std::string too_long(kMaxProviderCredentialHandleBytes + 1u, 'h');
  CoreApiCommandFactory second = NewFactory();
  const ProviderCommandResult oversized = second.BuildSaveProviderCredential(
      "anthropic", api::ProviderAuthMethodView::kApiKey, too_long, kGeneration,
      kNowMs);
  EXPECT_EQ(oversized.refusal,
            ProviderRequestRefusal::kCredentialHandleTooLong);
  EXPECT_FALSE(oversized.command);

  // The account bound is not this bound. A handle that only the wider one
  // admits is refused here, which is the regression this case exists for.
  CoreApiCommandFactory account_bound = NewFactory();
  const ProviderCommandResult borrowed =
      account_bound.BuildSaveProviderCredential(
          "anthropic", api::ProviderAuthMethodView::kApiKey,
          std::string(api::kMaxAuthCredentialHandleBytes, 'h'), kGeneration,
          kNowMs);
  EXPECT_EQ(borrowed.refusal,
            ProviderRequestRefusal::kCredentialHandleTooLong);
  EXPECT_FALSE(borrowed.command);

  const std::string longest(kMaxProviderCredentialHandleBytes, 'h');
  CoreApiCommandFactory third = NewFactory();
  const ProviderCommandResult accepted = third.BuildSaveProviderCredential(
      "anthropic", api::ProviderAuthMethodView::kApiKey, longest, kGeneration,
      kNowMs);
  EXPECT_EQ(accepted.refusal, ProviderRequestRefusal::kNone);
  EXPECT_TRUE(accepted.command);
}

// A custom provider's handle is optional, and absent is not the same as empty:
// absent means a provider reached without a key, empty means a key that is no
// key. The second is a request, so it is refused by name.
TEST(CoreApiProviderRefusalTest, AnOptionalHandleIsAbsentOrRealAndNeverEmpty) {
  CoreApiCommandFactory factory = NewFactory();
  const ProviderCommandResult result = factory.BuildSaveCustomProvider(
      "my-gateway", "My gateway", kEndpoint,
      api::ProviderWireApiView::kOpenAiResponses, std::optional<std::string>(""),
      NoModels(), NoServer(), kGeneration, kNowMs);

  EXPECT_EQ(result.refusal, ProviderRequestRefusal::kEmptyCredentialHandle);
  EXPECT_FALSE(result.command);
}

// A name of nothing but whitespace draws as nothing, which leaves a row a
// person cannot tell from the one above it. That is why blankness is checked
// rather than emptiness.
TEST(CoreApiProviderRefusalTest, ADisplayNameThatDrawsAsNothingIsRefused) {
  for (const char *blank : {"", " ", "\t", "   \t\r\n "}) {
    const ProviderCommandResult result = SaveCustomNamed(blank);
    EXPECT_EQ(result.refusal, ProviderRequestRefusal::kEmptyDisplayName);
    EXPECT_FALSE(result.command);
  }

  const std::string too_long(api::kMaxProviderDisplayNameBytes + 1u, 'n');
  const ProviderCommandResult oversized = SaveCustomNamed(too_long);
  EXPECT_EQ(oversized.refusal, ProviderRequestRefusal::kDisplayNameTooLong);
  EXPECT_FALSE(oversized.command);

  const std::string longest(api::kMaxProviderDisplayNameBytes, 'n');
  const ProviderCommandResult accepted = SaveCustomNamed(longest);
  EXPECT_EQ(accepted.refusal, ProviderRequestRefusal::kNone);
  EXPECT_TRUE(accepted.command);
}

// The allowlist that had to change with these handlers. It answers what this
// process can submit, and a provider kind left out of it would be a command
// built here and refused one hop later, with no reason a person could read.
TEST(CoreApiProviderRefusalTest, EveryProviderKindIsNowSubmittable) {
  using Kind = api::CoreCommandKind;
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kSaveProviderCredential));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(
      Kind::kForgetProviderCredential));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(
      Kind::kSetProviderCredentialState));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kStartProviderAuth));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kSaveCustomProvider));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kRemoveCustomProvider));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(
      Kind::kSetProviderModelPreference));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kProbeCustomEndpoint));
}

} // namespace
} // namespace taffy
