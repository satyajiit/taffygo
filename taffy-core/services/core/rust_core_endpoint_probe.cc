// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_endpoint_probe.h"

#include <stdint.h>

#include <string>
#include <utility>

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

mojom::EffectEnvelopePtr
ToMojoEndpointProbeEffect(const bridge::BridgeEndpointProbeEffect &in) {
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
  effect->kind = mojom::EffectKind::kProbeCustomEndpoint;
  // Stated here rather than carried, exactly as the listing fetch's class is,
  // and this is the opposite answer for the opposite reason. A listing is
  // read-only and safe to repeat; a probe may spend a credential handle that
  // was minted for one send, and a person who is still typing an address will
  // ask again themselves. There is one claim and one place that makes it, so
  // there is nothing here for a record to disagree with.
  effect->retry_class = mojom::RetryClass::kNever;
  effect->custom_endpoint_probe = mojom::CustomEndpointProbeEffect::New();
  effect->custom_endpoint_probe->provider_id = std::string(in.provider_id);
  effect->custom_endpoint_probe->endpoint = std::string(in.endpoint);
  effect->custom_endpoint_probe->wire_api =
      static_cast<mojom::ProviderWireApi>(in.wire_api);
  // Absence and emptiness are different answers: an endpoint that needs no key
  // is not an endpoint whose key is the empty string, and the flag is what
  // keeps the two apart across a bridge with no optional of its own.
  if (in.has_credential_handle) {
    effect->custom_endpoint_probe->credential_handle =
        std::string(in.credential_handle);
  }
  effect->custom_endpoint_probe->max_response_bytes = in.max_response_bytes;
  return effect;
}

std::optional<bridge::BridgeEndpointProbeResult>
ToBridgeEndpointProbeResult(const mojom::EffectResult &result) {
  if (!result.operation ||
      result.kind != mojom::EffectKind::kProbeCustomEndpoint ||
      !result.custom_endpoint_probe) {
    return std::nullopt;
  }
  const mojom::CustomEndpointProbeResult &probe = *result.custom_endpoint_probe;
  bridge::BridgeEndpointProbeResult out;
  out.operation.operation_id = result.operation->operation_id;
  out.operation.service_generation = result.operation->service_generation;
  out.operation.task_revision = result.operation->task_revision;
  out.operation.deadline_monotonic_ms =
      result.operation->deadline_monotonic_ms;
  out.operation.idempotency_key = result.operation->idempotency_key;
  out.effect_id = result.effect_id;
  out.provider_id = probe.provider_id;
  out.reached = probe.reached;
  // The wrapper is the absence. Mojo has no optional enumeration, which is why
  // the contract carries a one-field record here at all, and reading the
  // record's presence rather than substituting a member keeps "the server
  // named no runtime" from arriving as a claim about what it is.
  out.has_detected_server = !probe.detected_server.is_null();
  if (out.has_detected_server) {
    out.detected_server =
        static_cast<uint8_t>(probe.detected_server->server_kind);
  }
  // The count and the list are separate facts and stay separate. The count is
  // what the server named, before the bound; the list is what survived it.
  // Deriving one from the other here is exactly the silent truncation decision
  // 0096 section 5 refuses.
  out.model_count = probe.model_count;
  out.models.reserve(probe.models.size());
  for (const mojom::CustomModelSpecPtr &model : probe.models) {
    if (!model) {
      return std::nullopt;
    }
    bridge::BridgeCustomModel projected;
    projected.model_id = model->model_id;
    projected.display_name = model->display_name;
    projected.context_window = model->context_window;
    projected.max_output_tokens = model->max_output_tokens;
    projected.reasoning = model->reasoning;
    projected.tool_calling = model->tool_calling;
    out.models.push_back(std::move(projected));
  }
  out.has_proved_base = probe.proved_base.has_value();
  if (out.has_proved_base) {
    out.proved_base = *probe.proved_base;
  }
  return out;
}

} // namespace taffy::core_service_internal
