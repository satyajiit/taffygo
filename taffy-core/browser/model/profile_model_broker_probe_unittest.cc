// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/test/bind.h"
#include "net/http/http_status_code.h"
#include "taffy/browser/model/profile_model_broker_test_support.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using model_broker_test::kCredential;
using model_broker_test::kTaskId;
using model_broker_test::kTransientHandle;
using model_broker_test::ManagedEffect;
using model_broker_test::ModelEffect;
using model_broker_test::ProbeEffect;
using model_broker_test::ProfileModelBrokerTest;

constexpr char kCustomBase[] = "http://localhost:11434/v1";

mojom::EffectEnvelopePtr EndpointProbeEffect() {
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->kind = mojom::EffectKind::kProbeCustomEndpoint;
  effect->retry_class = mojom::RetryClass::kNever;
  effect->model_request.reset();
  effect->custom_endpoint_probe = mojom::CustomEndpointProbeEffect::New(
      "own-provider", kCustomBase, mojom::ProviderWireApi::kOpenAiCompletions,
      std::nullopt, mojom::kMaxProviderListingBytes);
  return effect;
}

// Drive the shipping reader and its contract projection together: a parser
// test alone would not catch the broker replacing both capabilities with false.
TEST_F(ProfileModelBrokerTest, EndpointProbePreservesAdvertisedModelFacts) {
  factory_.AddResponse("http://localhost:11434/v1/models", R"({"data":[{
    "id":"local-thinker","context_length":16384,
    "top_provider":{"max_completion_tokens":2048},
    "supported_parameters":["tools","reasoning"]
  },{
    "id":"local-quick","supported_parameters":["tools"]
  },{
    "id":"local-unknown","tool_calling":true,"reasoning":true
  }]})");
  factory_.AddResponse("http://localhost:11434/api/tags", "",
                       net::HTTP_NOT_FOUND);
  factory_.AddResponse("http://localhost:11434/props", "", net::HTTP_NOT_FOUND);

  mojom::EffectResultPtr result;
  broker_->DispatchEndpointProbe(
      EndpointProbeEffect(),
      base::BindLambdaForTesting([&result](mojom::EffectResultPtr given) {
        result = std::move(given);
      }));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(result->custom_endpoint_probe);
  const mojom::CustomEndpointProbeResult& probe =
      *result->custom_endpoint_probe;
  EXPECT_TRUE(probe.reached);
  EXPECT_EQ(probe.provider_id, "own-provider");
  EXPECT_EQ(probe.proved_base, std::optional<std::string>(kCustomBase));
  ASSERT_EQ(probe.models.size(), 3u);
  EXPECT_EQ(probe.model_count, 3u);
  const mojom::CustomModelSpec& thinker = *probe.models[0];
  EXPECT_EQ(thinker.model_id, "local-thinker");
  EXPECT_EQ(thinker.display_name, "local-thinker");
  EXPECT_EQ(thinker.context_window, 16384u);
  EXPECT_EQ(thinker.max_output_tokens, 2048u);
  EXPECT_TRUE(thinker.reasoning);
  EXPECT_TRUE(thinker.tool_calling);
  EXPECT_FALSE(probe.models[1]->reasoning);
  EXPECT_TRUE(probe.models[1]->tool_calling);
  EXPECT_EQ(probe.models[2]->context_window, 0u);
  EXPECT_EQ(probe.models[2]->max_output_tokens, 0u);
  EXPECT_FALSE(probe.models[2]->reasoning);
  EXPECT_FALSE(probe.models[2]->tool_calling);
  EXPECT_TRUE(asked_handle_.empty());
  EXPECT_EQ(factory_.NumPending(), 0);
}

// The probe half of the suite (decision 0083). What these tests pin is the
// handle fork — the durable store answers a handle that names the provider
// itself, the one-shot transient seam answers everything else — and the two
// facts the verdict is classified from: the coarse status and the provider's
// own HTTP status, zero when the provider was never reached.
class ProfileModelBrokerProbeTest : public ProfileModelBrokerTest {
 protected:
  void SetUp() override {
    ProfileModelBrokerTest::SetUp();
    InstallConsumer();
  }

  void InstallConsumer() {
    broker_->SetTransientCredentialConsumer(base::BindLambdaForTesting(
        [this](const std::string& handle,
               ProfileModelBroker::CredentialCallback callback) {
          ++consumed_count_;
          consumed_handle_ = handle;
          std::move(callback).Run(transient_);
        }));
  }

  int consumed_count_ = 0;
  std::string consumed_handle_;
  std::optional<std::string> transient_ = std::string("pasted-draft-key");
};

TEST_F(ProfileModelBrokerProbeTest,
       ADurableHandleProbeResolvesThroughTheStore) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       R"({"content":[{"text":"ok"}]})");

  const mojom::EffectResultPtr& result = Run(ProbeEffect("fixture-provider"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  // The stored key, through the same phase-one seam a task turn uses.
  EXPECT_EQ(asked_handle_, "fixture-provider");
  EXPECT_EQ(consumed_count_, 0);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("x-api-key"),
            std::optional<std::string>(kCredential));
  // The classifier's fact: the provider answered, and said 200.
  EXPECT_EQ(result->model->provider_http_status, 200u);
}

TEST_F(ProfileModelBrokerProbeTest, ATransientHandleProbeSpendsTheConsumer) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       R"({"content":[{"text":"ok"}]})");

  const mojom::EffectResultPtr& result = Run(ProbeEffect(kTransientHandle));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  EXPECT_EQ(consumed_count_, 1);
  EXPECT_EQ(consumed_handle_, kTransientHandle);
  // The durable store was never asked: a pasted draft never enters it.
  EXPECT_TRUE(asked_handle_.empty());
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("x-api-key"),
            std::optional<std::string>("pasted-draft-key"));
}

TEST_F(ProfileModelBrokerProbeTest,
       WithNoConsumerInstalledATransientProbeIsUnavailable) {
  broker_->SetTransientCredentialConsumer(
      ProfileModelBroker::TransientCredentialConsumer());

  const mojom::EffectResultPtr& result = Run(ProbeEffect(kTransientHandle));
  ASSERT_TRUE(result);
  // What is missing is the browser's seam to its own transient store, not
  // anything about the person's key.
  EXPECT_EQ(result->status, mojom::EffectStatus::kUnavailable);
  EXPECT_EQ(result->model->provider_http_status, 0u);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerProbeTest, ASpentTransientIsUnavailableNotDenied) {
  transient_ = std::nullopt;

  const mojom::EffectResultPtr& result = Run(ProbeEffect(kTransientHandle));
  ASSERT_TRUE(result);
  // Absent or already spent: nothing was judged, so the verdict must read
  // "never reached" and not "the key is wrong".
  EXPECT_EQ(result->status, mojom::EffectStatus::kUnavailable);
  EXPECT_EQ(result->model->provider_http_status, 0u);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerProbeTest, AnEmptyTransientIsUnavailableNotSent) {
  transient_ = std::string();

  const mojom::EffectResultPtr& result = Run(ProbeEffect(kTransientHandle));
  ASSERT_TRUE(result);
  // An empty credential would reach the provider and be refused there,
  // spending an attempt to learn something this process already knew.
  EXPECT_EQ(result->status, mojom::EffectStatus::kUnavailable);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerProbeTest, AProbeOnTheManagedWireIsDenied) {
  mojom::EffectEnvelopePtr effect = ManagedEffect();
  effect->model_request->probe = true;
  effect->model_request->task_id = std::string();

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  // A probe of the product's own service would spend the person's plan to
  // test the product.
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerProbeTest, AProbeBoundToATaskIsDenied) {
  mojom::EffectEnvelopePtr effect = ProbeEffect(kTransientHandle);
  effect->model_request->task_id = kTaskId;

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(consumed_count_, 0);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerProbeTest, TheProvidersOwnRefusalRidesTheResult) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       R"({"error":{"type":"authentication_error"}})",
                       net::HTTP_UNAUTHORIZED);

  const mojom::EffectResultPtr& result = Run(ProbeEffect("fixture-provider"));
  ASSERT_TRUE(result);
  // The effect vocabulary folds 401, 402 and 403 into one denial; the HTTP
  // status is what lets the Rust classifier keep them apart.
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(result->model->provider_http_status, 401u);
}

TEST_F(ProfileModelBrokerProbeTest, ATaskTurnNeverSpendsTheTransientSeam) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       R"({"content":[]})");
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->credential_handle = kTransientHandle;

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  // Not a probe, so the handle goes to the durable resolver whatever its
  // spelling: the one-shot seam is the probe's alone.
  EXPECT_EQ(consumed_count_, 0);
  EXPECT_EQ(asked_handle_, kTransientHandle);
}

}  // namespace
}  // namespace taffy
