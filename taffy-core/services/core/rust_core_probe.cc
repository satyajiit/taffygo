// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_probe.h"

#include <stdint.h>

#include <string>

#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;
namespace wire = core_service::wire;

namespace {

bool ValidIdentifier(const rust::String &value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

} // namespace

mojom::EffectEnvelopePtr
ToMojoProbeEffect(const bridge::BridgeProbeEffect &in) {
  const std::optional<mojom::RetryClass> retry_class =
      wire::RetryClassFromWire(in.retry_class);
  const std::optional<mojom::DisclosureClass> disclosure =
      wire::DisclosureClassFromWire(in.disclosure);
  const std::optional<mojom::ProviderWireApi> wire_api =
      wire::ProviderWireApiFromWire(in.wire_api);
  if (!retry_class || !disclosure || !wire_api ||
      *wire_api == mojom::ProviderWireApi::kManaged ||
      !ValidIdentifier(in.effect_id) ||
      !ValidIdentifier(in.operation.operation_id) ||
      !ValidIdentifier(in.operation.idempotency_key) ||
      in.operation.service_generation == 0u ||
      in.operation.deadline_monotonic_ms == 0u ||
      !ValidIdentifier(in.route_id) || !ValidIdentifier(in.model_id) ||
      in.provider_id.empty() ||
      in.provider_id.size() > mojom::kMaxProviderIdBytes ||
      in.endpoint.empty() ||
      in.endpoint.size() > mojom::kMaxProviderEndpointBytes ||
      in.request_body.empty() ||
      in.request_body.size() > mojom::kMaxEffectBytes ||
      in.max_output_bytes == 0u ||
      in.max_output_bytes > mojom::kMaxEffectBytes ||
      (!in.credential_handle.empty() &&
       !ValidIdentifier(in.credential_handle))) {
    return nullptr;
  }
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = std::string(in.operation.operation_id);
  effect->operation->service_generation = in.operation.service_generation;
  effect->operation->task_revision = in.operation.task_revision;
  effect->operation->deadline_monotonic_ms =
      in.operation.deadline_monotonic_ms;
  effect->operation->idempotency_key =
      std::string(in.operation.idempotency_key);
  effect->effect_id = std::string(in.effect_id);
  effect->kind = mojom::EffectKind::kModelRequest;
  effect->retry_class = *retry_class;
  auto request = mojom::ModelRequestEffect::New();
  request->route_id = std::string(in.route_id);
  request->model_id = std::string(in.model_id);
  request->disclosure = *disclosure;
  request->request_body.reserve(in.request_body.size());
  for (uint8_t byte : in.request_body) {
    request->request_body.push_back(byte);
  }
  request->max_output_bytes = in.max_output_bytes;
  // Task-less and marked as the probe it is, by construction rather than by
  // copying: this channel carries nothing else, and the model-effect
  // validation pins the pair.
  request->task_id = std::string();
  request->probe = true;
  request->provider_id = std::string(in.provider_id);
  request->wire_api = *wire_api;
  request->endpoint = std::string(in.endpoint);
  // Stated rather than left to value initialization. A probe speaks to the
  // provider's own catalog endpoint, so the browser checks the address against
  // its copy of the catalog; the rule for an address a person typed is a
  // different one and is claimed by a different command (decision 0096). A
  // field that says which rule is being claimed must never arrive by default,
  // because the default would then be a claim nobody made.
  request->endpoint_kind = mojom::ModelEndpointKind::kCatalogOrigin;
  if (!in.credential_handle.empty()) {
    request->credential_handle = std::string(in.credential_handle);
  }
  effect->model_request = std::move(request);
  return effect;
}

std::optional<bridge::BridgeProbeCompletion>
ToBridgeProbeCompletion(const mojom::EffectResult &result) {
  if (result.kind != mojom::EffectKind::kModelRequest) {
    return std::nullopt;
  }
  bridge::BridgeProbeCompletion out;
  out.effect_id = result.effect_id;
  out.status = static_cast<uint8_t>(result.status);
  out.provider_http_status =
      result.model ? result.model->provider_http_status : 0u;
  return out;
}

} // namespace taffy::core_service_internal
