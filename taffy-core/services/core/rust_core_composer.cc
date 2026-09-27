// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_composer.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

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

bridge::BridgeComposerOperation
ToBridgeComposerOperation(const mojom::OperationEnvelope &in) {
  bridge::BridgeComposerOperation out;
  out.operation_id = in.operation_id;
  out.service_generation = in.service_generation;
  out.task_revision = in.task_revision;
  out.deadline_monotonic_ms = in.deadline_monotonic_ms;
  out.idempotency_key = in.idempotency_key;
  return out;
}

mojom::OperationEnvelopePtr
ToMojoOperation(const bridge::BridgeComposerOperation &in) {
  auto out = mojom::OperationEnvelope::New();
  out->operation_id = std::string(in.operation_id);
  out->service_generation = in.service_generation;
  out->task_revision = in.task_revision;
  out->deadline_monotonic_ms = in.deadline_monotonic_ms;
  out->idempotency_key = std::string(in.idempotency_key);
  return out;
}

bool ValidOperationIdentity(const bridge::BridgeComposerOperation &in) {
  return ValidIdentifier(in.operation_id) &&
         ValidIdentifier(in.idempotency_key) && in.service_generation != 0u;
}

// The stricter form, for an envelope that is about to be dispatched: work
// that leaves the process needs a deadline somebody can enforce. The push
// back to a surface is not dispatched anywhere and carries the deadline of
// the call it answers, so holding it to this would refuse a suggestion over a
// field nothing reads — and refuse it after the flight had already been
// settled, which is the one way a composer can be left waiting forever.
bool ValidDispatchOperation(const bridge::BridgeComposerOperation &in) {
  return ValidOperationIdentity(in) && in.deadline_monotonic_ms != 0u;
}

} // namespace

std::optional<bridge::BridgeComposerCommand>
ToBridgeComposerCommand(const mojom::CoreServiceCommand &command) {
  if (!command.operation ||
      command.kind !=
          mojom::CoreServiceCommandKind::kRequestComposerCompletion ||
      !command.request_composer_completion) {
    return std::nullopt;
  }
  const mojom::RequestComposerCompletionCommand &body =
      *command.request_composer_completion;
  if (body.request_id.empty() ||
      body.request_id.size() > mojom::kMaxIdentifierBytes ||
      body.prefix.empty() || body.prefix.size() > mojom::kMaxComposerPrefixBytes ||
      (body.suffix.has_value() &&
       body.suffix->size() > mojom::kMaxComposerSuffixBytes)) {
    return std::nullopt;
  }
  bridge::BridgeComposerCommand out;
  out.operation = ToBridgeComposerOperation(*command.operation);
  out.request_id = body.request_id;
  out.prefix = body.prefix;
  out.has_suffix = body.suffix.has_value();
  if (out.has_suffix) {
    out.suffix = *body.suffix;
  }
  return out;
}

std::optional<bridge::BridgeComposerCancel>
ToBridgeComposerCancel(const mojom::CoreServiceCommand &command) {
  if (!command.operation ||
      command.kind !=
          mojom::CoreServiceCommandKind::kCancelComposerCompletion ||
      !command.cancel_composer_completion) {
    return std::nullopt;
  }
  const mojom::CancelComposerCompletionCommand &body =
      *command.cancel_composer_completion;
  if (body.request_id.empty() ||
      body.request_id.size() > mojom::kMaxIdentifierBytes) {
    return std::nullopt;
  }
  bridge::BridgeComposerCancel out;
  out.operation = ToBridgeComposerOperation(*command.operation);
  out.request_id = body.request_id;
  return out;
}

mojom::EffectEnvelopePtr
ToMojoComposerEffect(const bridge::BridgeComposerEffect &in) {
  const std::optional<mojom::RetryClass> retry_class =
      wire::RetryClassFromWire(in.retry_class);
  const std::optional<mojom::DisclosureClass> disclosure =
      wire::DisclosureClassFromWire(in.disclosure);
  const std::optional<mojom::ProviderWireApi> wire_api =
      wire::ProviderWireApiFromWire(in.wire_api);
  const std::optional<mojom::ModelEndpointKind> endpoint_kind =
      wire::ModelEndpointKindFromWire(in.endpoint_kind);
  if (!retry_class || !disclosure || !wire_api || !endpoint_kind ||
      *wire_api == mojom::ProviderWireApi::kManaged ||
      *disclosure != mojom::DisclosureClass::kUserSelectedContent ||
      !ValidIdentifier(in.effect_id) ||
      !ValidDispatchOperation(in.operation) ||
      !ValidIdentifier(in.route_id) || !ValidIdentifier(in.model_id) ||
      in.provider_id.empty() ||
      in.provider_id.size() > mojom::kMaxProviderIdBytes ||
      in.endpoint.empty() ||
      in.endpoint.size() > mojom::kMaxProviderEndpointBytes ||
      in.request_body.empty() ||
      in.request_body.size() > mojom::kMaxEffectBytes ||
      in.max_output_bytes == 0u ||
      in.max_output_bytes > mojom::kMaxComposerCompletionBytes ||
      !ValidIdentifier(in.credential_handle)) {
    return nullptr;
  }
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = ToMojoOperation(in.operation);
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
  // Task-less and not a probe, by construction rather than by copying: no
  // task owns a suggestion (decision 0097 section 1), which is what makes it
  // unjournalled and unauditable rather than merely unjournalled so far.
  request->task_id = std::string();
  request->probe = false;
  request->provider_id = std::string(in.provider_id);
  request->wire_api = *wire_api;
  request->endpoint = std::string(in.endpoint);
  // Carried rather than stated here, unlike the probe's. A field that says
  // which revalidation rule is being claimed must never arrive by default,
  // and the claim belongs to whoever chose the address: a suggestion is
  // composed against whichever configured route the person's own credential
  // reaches, and that may be a provider they defined themselves. Stating the
  // catalog rule on the core's behalf would make a claim nobody made, and it
  // would make it in the direction that sends a person's own endpoint under
  // the wrong check.
  request->endpoint_kind = *endpoint_kind;
  request->credential_handle = std::string(in.credential_handle);
  effect->model_request = std::move(request);
  return effect;
}

std::optional<bridge::BridgeComposerCompletion>
ToBridgeComposerCompletion(const mojom::EffectResult &result) {
  if (result.kind != mojom::EffectKind::kModelRequest || !result.operation) {
    return std::nullopt;
  }
  bridge::BridgeComposerCompletion out;
  out.operation = ToBridgeComposerOperation(*result.operation);
  out.effect_id = result.effect_id;
  out.status = static_cast<uint8_t>(result.status);
  if (result.model) {
    if (result.model->completion.size() > mojom::kMaxEffectBytes) {
      return std::nullopt;
    }
    out.completion.reserve(result.model->completion.size());
    for (uint8_t byte : result.model->completion) {
      out.completion.push_back(byte);
    }
  }
  return out;
}

mojom::EffectEnvelopePtr
ToMojoComposerDelivery(const bridge::BridgeComposerDelivery &in) {
  const std::optional<mojom::RetryClass> retry_class =
      wire::RetryClassFromWire(in.retry_class);
  if (!in.claimed || !retry_class || !ValidIdentifier(in.effect_id) ||
      !ValidOperationIdentity(in.operation) || !ValidIdentifier(in.request_id) ||
      (in.has_text && (in.text.empty() ||
                       in.text.size() > mojom::kMaxComposerCompletionBytes)) ||
      (!in.has_text && !in.text.empty())) {
    return nullptr;
  }
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = ToMojoOperation(in.operation);
  effect->effect_id = std::string(in.effect_id);
  effect->kind = mojom::EffectKind::kDeliverComposerCompletion;
  effect->retry_class = *retry_class;
  auto push = mojom::ComposerCompletionEffect::New();
  push->request_id = std::string(in.request_id);
  if (in.has_text) {
    push->text = std::string(in.text);
  }
  effect->composer_completion = std::move(push);
  return effect;
}

CoreResponseBatch
ToComposerSubmissionBatch(bridge::BridgeComposerSubmission submission) {
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->operation_id =
      std::string(submission.admission.operation_id);
  batch.admission->status =
      wire::AdmissionStatusFromWire(submission.admission.status)
          .value_or(mojom::AdmissionStatus::kInvalidCommand);
  if (submission.has_superseded) {
    batch.superseded_effect_id = std::string(submission.superseded_effect_id);
  }
  if (!submission.has_effect) {
    return batch;
  }
  mojom::EffectEnvelopePtr projected = ToMojoComposerEffect(submission.effect);
  if (!projected) {
    batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
    return batch;
  }
  batch.effects.push_back(std::move(projected));
  return batch;
}

CoreResponseBatch SubmitComposerCommand(bridge::ServiceBridge &runtime,
                                        const mojom::CoreServiceCommand &command,
                                        uint64_t now_monotonic_ms,
                                        uint64_t now_utc_millis) {
  std::optional<bridge::BridgeComposerCommand> projected =
      ToBridgeComposerCommand(command);
  if (!projected) {
    CoreResponseBatch batch;
    batch.admission = mojom::Admission::New();
    if (command.operation) {
      batch.admission->operation_id = command.operation->operation_id;
    }
    batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
    return batch;
  }
  return ToComposerSubmissionBatch(bridge::SubmitComposer(
      runtime, std::move(*projected), now_monotonic_ms, now_utc_millis));
}

CoreResponseBatch CancelComposerCommand(bridge::ServiceBridge &runtime,
                                        const mojom::CoreServiceCommand &command,
                                        uint64_t now_monotonic_ms,
                                        uint64_t now_utc_millis) {
  std::optional<bridge::BridgeComposerCancel> projected =
      ToBridgeComposerCancel(command);
  if (!projected) {
    CoreResponseBatch batch;
    batch.admission = mojom::Admission::New();
    if (command.operation) {
      batch.admission->operation_id = command.operation->operation_id;
    }
    batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
    return batch;
  }
  // The same batch projection a submission uses. A withdrawal has no effect to
  // carry, so `has_effect` false takes it straight past the effect leg and the
  // `superseded_effect_id` it did fill is what the caller acts on.
  return ToComposerSubmissionBatch(bridge::CancelComposer(
      runtime, std::move(*projected), now_monotonic_ms, now_utc_millis));
}

std::optional<CoreResponseBatch>
DeliverComposerTerminal(bridge::ServiceBridge &runtime,
                        const mojom::EffectResult &result,
                        uint64_t now_utc_millis) {
  std::optional<bridge::BridgeComposerCompletion> projected =
      ToBridgeComposerCompletion(result);
  if (!projected) {
    return std::nullopt;
  }
  const bridge::BridgeComposerDelivery delivered = bridge::DeliverComposerCompletion(
      runtime, std::move(*projected), now_utc_millis);
  if (!delivered.claimed) {
    return std::nullopt;
  }
  return ToComposerDeliveryBatch(delivered);
}

CoreResponseBatch
ToComposerDeliveryBatch(const bridge::BridgeComposerDelivery &delivery) {
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->operation_id = std::string(delivery.operation.operation_id);
  batch.admission->status = mojom::AdmissionStatus::kAccepted;
  mojom::EffectEnvelopePtr projected = ToMojoComposerDelivery(delivery);
  if (!projected) {
    batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
    return batch;
  }
  batch.effects.push_back(std::move(projected));
  return batch;
}

} // namespace taffy::core_service_internal
