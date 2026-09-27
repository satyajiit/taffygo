// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_probe.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: the key probe crossing the process seam in each direction
// (decision 0083). Outbound, the flat bridge record the ordered core composed
// becomes the typed model effect the broker dispatches — probe-only by
// construction, task-less, header-less, and never on the managed wire.
// Inbound, one dispatch terminal becomes the two facts the Rust classifier
// reads: the coarse effect status, and the provider's own HTTP status, zero
// when the provider was never reached.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bridge::BridgeProbeEffect ProbeEffect() {
  bridge::BridgeProbeEffect effect{};
  effect.operation.operation_id = "operation-1";
  effect.operation.service_generation = 1u;
  effect.operation.task_revision = 0u;
  effect.operation.deadline_monotonic_ms = 1000u;
  effect.operation.idempotency_key = "key-1";
  effect.effect_id = "provider-probe-provider-1-1";
  effect.retry_class = 2u;  // NEVER
  effect.route_id = "probe";
  effect.model_id = "model-1";
  effect.disclosure = 0u;  // CONTENT_FREE
  effect.request_body = {'{', '}'};
  effect.max_output_bytes = 4096u;
  effect.provider_id = "provider-1";
  effect.wire_api = 0u;  // ANTHROPIC_MESSAGES
  effect.endpoint = "https://provider.taffy.test";
  effect.credential_handle = "provider-1";
  return effect;
}

TEST(RustCoreProbeConversionTest, ValidProbeEffectProjects) {
  const mojom::EffectEnvelopePtr projected = core_service_internal::
      ToMojoProbeEffect(ProbeEffect());
  ASSERT_TRUE(projected);
  EXPECT_EQ(projected->effect_id, "provider-probe-provider-1-1");
  EXPECT_EQ(projected->kind, mojom::EffectKind::kModelRequest);
  EXPECT_EQ(projected->retry_class, mojom::RetryClass::kNever);
  ASSERT_TRUE(projected->operation);
  EXPECT_EQ(projected->operation->operation_id, "operation-1");
  ASSERT_TRUE(projected->model_request);
  const mojom::ModelRequestEffect& request = *projected->model_request;
  EXPECT_EQ(request.route_id, "probe");
  EXPECT_EQ(request.model_id, "model-1");
  EXPECT_EQ(request.disclosure, mojom::DisclosureClass::kContentFree);
  EXPECT_EQ(request.wire_api, mojom::ProviderWireApi::kAnthropicMessages);
  EXPECT_EQ(request.endpoint, "https://provider.taffy.test");
  EXPECT_EQ(request.provider_id, "provider-1");
  EXPECT_EQ(request.credential_handle,
            std::optional<std::string>("provider-1"));
  // Probe-only by construction, not by copying: this channel carries nothing
  // else, so the pair the model-effect validation pins is set here.
  EXPECT_TRUE(request.probe);
  EXPECT_TRUE(request.task_id.empty());
  EXPECT_TRUE(request.static_headers.empty());
}

TEST(RustCoreProbeConversionTest, AnEmptyCredentialHandleProjectsAbsent) {
  bridge::BridgeProbeEffect effect = ProbeEffect();
  effect.credential_handle = "";
  const mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoProbeEffect(effect);
  ASSERT_TRUE(projected);
  // Absent, not empty: an empty credential would be a request that reaches
  // the provider and is refused there.
  EXPECT_FALSE(projected->model_request->credential_handle.has_value());
}

TEST(RustCoreProbeConversionTest, TheManagedWireIsRefused) {
  bridge::BridgeProbeEffect effect = ProbeEffect();
  effect.wire_api = 4u;  // MANAGED
  // A probe of the product's own service would spend the person's plan to
  // test the product; the broker refuses the same pair independently.
  EXPECT_FALSE(core_service_internal::ToMojoProbeEffect(effect));
}

TEST(RustCoreProbeConversionTest, UnknownEnumerationBytesAreRefused) {
  bridge::BridgeProbeEffect wire = ProbeEffect();
  wire.wire_api = 255u;
  EXPECT_FALSE(core_service_internal::ToMojoProbeEffect(wire));

  bridge::BridgeProbeEffect retry = ProbeEffect();
  retry.retry_class = 3u;
  EXPECT_FALSE(core_service_internal::ToMojoProbeEffect(retry));

  bridge::BridgeProbeEffect disclosure = ProbeEffect();
  disclosure.disclosure = 4u;
  EXPECT_FALSE(core_service_internal::ToMojoProbeEffect(disclosure));
}

TEST(RustCoreProbeConversionTest, AnEmptyOrUnboundedBodyIsRefused) {
  bridge::BridgeProbeEffect empty = ProbeEffect();
  empty.request_body.clear();
  EXPECT_FALSE(core_service_internal::ToMojoProbeEffect(empty));

  bridge::BridgeProbeEffect unbounded = ProbeEffect();
  unbounded.max_output_bytes = 0u;
  EXPECT_FALSE(core_service_internal::ToMojoProbeEffect(unbounded));
}

TEST(RustCoreProbeConversionTest, ProbeCompletionCarriesTheTwoFacts) {
  auto result = mojom::EffectResult::New();
  result->operation = mojom::OperationEnvelope::New();
  result->operation->operation_id = "operation-1";
  result->effect_id = "provider-probe-provider-1-1";
  result->kind = mojom::EffectKind::kModelRequest;
  result->status = mojom::EffectStatus::kDenied;
  result->model = mojom::ModelEffectResult::New();
  result->model->provider_http_status = 401u;

  const std::optional<bridge::BridgeProbeCompletion> completion =
      core_service_internal::ToBridgeProbeCompletion(*result);
  ASSERT_TRUE(completion);
  EXPECT_EQ(std::string(completion->effect_id),
            "provider-probe-provider-1-1");
  EXPECT_EQ(completion->status,
            static_cast<uint8_t>(mojom::EffectStatus::kDenied));
  // The effect vocabulary folds 401, 402 and 403 into one denial; this field
  // is what lets the classifier keep them apart.
  EXPECT_EQ(completion->provider_http_status, 401u);
}

TEST(RustCoreProbeConversionTest, ANonModelResultIsNotAProbeCompletion) {
  auto result = mojom::EffectResult::New();
  result->operation = mojom::OperationEnvelope::New();
  result->effect_id = "effect-1";
  result->kind = mojom::EffectKind::kNetworkRequest;
  result->status = mojom::EffectStatus::kCompleted;
  EXPECT_FALSE(core_service_internal::ToBridgeProbeCompletion(*result));
}

TEST(RustCoreProbeConversionTest, AModellessResultReportsZeroHttpStatus) {
  auto result = mojom::EffectResult::New();
  result->operation = mojom::OperationEnvelope::New();
  result->effect_id = "provider-probe-provider-1-2";
  result->kind = mojom::EffectKind::kModelRequest;
  result->status = mojom::EffectStatus::kUnavailable;

  const std::optional<bridge::BridgeProbeCompletion> completion =
      core_service_internal::ToBridgeProbeCompletion(*result);
  ASSERT_TRUE(completion);
  // Never reached: the classifier reads zero-with-unavailable as the network
  // verdict, which is honest about a provider nothing judged.
  EXPECT_EQ(completion->provider_http_status, 0u);
}

}  // namespace
}  // namespace taffy
