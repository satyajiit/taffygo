// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_entitlement.h"

#include <string>

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

std::optional<bridge::BridgeEntitlementFetchResult>
ToBridgeEntitlementFetchResult(const mojom::EffectResult &result) {
  if (!result.operation || result.kind != mojom::EffectKind::kNetworkRequest ||
      !result.network ||
      result.network->operation_kind !=
          mojom::AccountNetworkOperation::kFetchEntitlement) {
    return std::nullopt;
  }
  bridge::BridgeEntitlementFetchResult out;
  out.operation.operation_id = result.operation->operation_id;
  out.operation.service_generation = result.operation->service_generation;
  out.operation.task_revision = result.operation->task_revision;
  out.operation.deadline_monotonic_ms = result.operation->deadline_monotonic_ms;
  out.operation.idempotency_key = result.operation->idempotency_key;
  out.effect_id = result.effect_id;
  // A summary is a definitive answer and travels only on a COMPLETED result;
  // anything else is the transport-failure shape, and the flattened summary
  // fields stay at their zero values because the flag says they carry
  // nothing.
  const mojom::EntitlementSummaryResultPtr &summary =
      result.network->entitlement_summary;
  out.has_summary =
      result.status == mojom::EffectStatus::kCompleted && !!summary;
  out.summary.definitive_absent = false;
  out.summary.window_seconds = 0u;
  out.summary.requests_remaining = 0u;
  out.summary.credits_granted = 0u;
  out.summary.credits_remaining = 0u;
  out.summary.credit_unit_micros = 0u;
  out.summary.next_renewal_epoch_seconds = 0u;
  out.summary.valid_until_epoch_seconds = 0u;
  out.summary.minted_at_utc_ms = 0u;
  if (out.has_summary) {
    out.summary.definitive_absent = summary->definitive_absent;
    out.summary.plan_id = summary->plan_id;
    out.summary.model_ids.reserve(summary->model_ids.size());
    for (const std::string &model_id : summary->model_ids) {
      out.summary.model_ids.push_back(model_id);
    }
    out.summary.window_seconds = summary->window_seconds;
    out.summary.requests_remaining = summary->requests_remaining;
    out.summary.credits_granted = summary->credits_granted;
    out.summary.credits_remaining = summary->credits_remaining;
    out.summary.credit_unit_micros = summary->credit_unit_micros;
    out.summary.next_renewal_epoch_seconds =
        summary->next_renewal_epoch_seconds;
    out.summary.valid_until_epoch_seconds = summary->valid_until_epoch_seconds;
    out.summary.minted_at_utc_ms = summary->minted_at_utc_ms;
    out.summary.worker_host = summary->worker_host;
    out.summary.gateway_host = summary->gateway_host;
  }
  return out;
}

mojom::EffectEnvelopePtr ToMojoEntitlementFetchEffect(
    const bridge::BridgeEntitlementFetchEffect &in) {
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
  effect->kind = mojom::EffectKind::kNetworkRequest;
  // Safe to repeat: a mint that runs twice produces two summaries saying the
  // same thing, and the worker's own rails bound how often it will say it.
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->network_request = mojom::NetworkRequestEffect::New();
  effect->network_request->operation_kind =
      mojom::AccountNetworkOperation::kFetchEntitlement;
  effect->network_request->max_response_bytes = in.max_response_bytes;
  effect->network_request->fetch_entitlement =
      mojom::FetchEntitlementRequest::New();
  effect->network_request->fetch_entitlement->reason =
      static_cast<mojom::EntitlementFetchReason>(in.reason);
  return effect;
}

} // namespace taffy::core_service_internal
