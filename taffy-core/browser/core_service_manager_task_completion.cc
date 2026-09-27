// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_effect.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;

bool OperationsMatch(const service_mojom::OperationEnvelope& left,
                     const service_mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

bool ObservationResultMatches(const service_mojom::TaskEffectBinding& binding,
                              const service_mojom::EffectResult& result,
                              bool private_profile) {
  if (!binding.operation || !binding.action || !binding.action->document ||
      !binding.action->executable || !binding.action->observation ||
      !result.operation || !result.observation ||
      result.kind != service_mojom::EffectKind::kPageObservation ||
      result.effect_id != binding.effect_id ||
      !OperationsMatch(*result.operation, *binding.operation)) {
    return false;
  }
  if (result.status != service_mojom::EffectStatus::kCompleted) {
    return true;
  }
  const service_mojom::TaskActionEffect& action = *binding.action;
  const service_mojom::ObservationEffectResult& observation =
      *result.observation;
  return observation.tab_id == action.executable->tab_id &&
         observation.frame_id == action.document->frame_id &&
         observation.page_epoch == action.document->page_epoch &&
         observation.origin == action.document->normalized_origin &&
         observation.graph_revision >= action.document->graph_revision &&
         observation.private_profile == private_profile &&
         observation.node_count <= action.observation->max_nodes &&
         observation.total_bytes <= action.observation->max_bytes &&
         observation.graph_payload.size() <= action.observation->max_bytes;
}

bool ModelResultMatches(const service_mojom::TaskEffectBinding& binding,
                        const service_mojom::EffectResult& result) {
  if (!binding.operation || !binding.model || !binding.model->request ||
      !result.operation || !result.model ||
      result.kind != service_mojom::EffectKind::kModelRequest ||
      result.effect_id != binding.effect_id ||
      !OperationsMatch(*result.operation, *binding.operation)) {
    return false;
  }
  if (result.status != service_mojom::EffectStatus::kCompleted) {
    return !result.model->failure ||
           (binding.model->request->wire_api !=
                service_mojom::ProviderWireApi::kManaged &&
            !binding.model->request->media_attachment_handle &&
            result.model->model_id == binding.model->request->model_id &&
            result.model->completion.empty() && !result.model->streamed);
  }
  return !result.model->failure &&
         result.model->model_id == binding.model->request->model_id;
}

// Whether a read's result is the refusal's own word and no page beside it.
//
// A refused observation is content-free by construction — the renderer
// answered with an empty envelope and the broker attaches the payload only to
// a result that matched its request — but the core rechecks the same five
// facts before it admits one, so this is the browser half of a rule both
// processes state rather than a claim the utility has to take on trust.
bool CarriesNoPage(const service_mojom::ObservationEffectResult& observation) {
  return observation.graph_payload.empty() && !observation.media &&
         observation.node_count == 0u && observation.total_bytes == 0u &&
         observation.graph_encoding == service_mojom::BipGraphEncoding::kNone;
}

service_mojom::TaskEffectCompletionStatus CompletionStatusFor(
    service_mojom::EffectStatus status) {
  switch (status) {
    case service_mojom::EffectStatus::kCompleted:
      return service_mojom::TaskEffectCompletionStatus::kSucceeded;
    case service_mojom::EffectStatus::kDenied:
    case service_mojom::EffectStatus::kInvalidResult:
      return service_mojom::TaskEffectCompletionStatus::kRefused;
    case service_mojom::EffectStatus::kCancelled:
      return service_mojom::TaskEffectCompletionStatus::kCancelled;
    case service_mojom::EffectStatus::kOutcomeUnknown:
      return service_mojom::TaskEffectCompletionStatus::kOutcomeUnknown;
    case service_mojom::EffectStatus::kDeadlineExceeded:
    case service_mojom::EffectStatus::kResourceLimit:
    case service_mojom::EffectStatus::kUnavailable:
      return service_mojom::TaskEffectCompletionStatus::kUnavailable;
  }
  return service_mojom::TaskEffectCompletionStatus::kRefused;
}

}  // namespace

void CoreServiceManager::OnTaskObservationCompleted(
    service_mojom::TaskEffectBindingPtr binding,
    ExecuteTaskEffectCallback callback,
    service_mojom::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  service_mojom::TaskEffectCompletionStatus status =
      result ? CompletionStatusFor(result->status)
             : service_mojom::TaskEffectCompletionStatus::kUnavailable;
  if (!binding || !result ||
      !ObservationResultMatches(*binding, *result, private_profile_)) {
    status = service_mojom::TaskEffectCompletionStatus::kRefused;
    result.reset();
  }
  service_mojom::TaskEffectCompletionPtr completion =
      MakeTaskEffectCompletion(binding.get(), status);
  // A failed read carries its own word and no page.
  //
  // It used to carry neither. `TaskEffectCompletionStatus` has one refusal
  // member, so `kStalePageEpoch` — a page that moved under the read, which is
  // the commonest thing a live site does — reached the reducer as the generic
  // policy denial, whose recovery is "this will be decided the same way again;
  // do not retry". The errand then handed the page back at whatever it
  // happened to be looking at. The exact code is already in this result; only
  // the snapshot may not cross, and `CarriesNoPage` is what says so.
  if (status == service_mojom::TaskEffectCompletionStatus::kSucceeded ||
      (status == service_mojom::TaskEffectCompletionStatus::kRefused &&
       result && result->observation && CarriesNoPage(*result->observation))) {
    completion->effect_result = std::move(result);
  }
  std::move(callback).Run(std::move(completion));
}

void CoreServiceManager::OnTaskModelCompleted(
    service_mojom::TaskEffectBindingPtr binding,
    ExecuteTaskEffectCallback callback,
    service_mojom::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  service_mojom::TaskEffectCompletionStatus status =
      result ? CompletionStatusFor(result->status)
             : service_mojom::TaskEffectCompletionStatus::kUnavailable;
  if (!binding || !result || !ModelResultMatches(*binding, *result)) {
    status = service_mojom::TaskEffectCompletionStatus::kRefused;
    result.reset();
  }
  service_mojom::TaskEffectCompletionPtr completion =
      MakeTaskEffectCompletion(binding.get(), status);
  // A definitive direct-provider failure also crosses: the browser reports
  // transport facts, while Rust alone applies the typed retry table. Generic
  // failures and unknown outcomes stay content-free.
  if (status == service_mojom::TaskEffectCompletionStatus::kSucceeded ||
      (result && result->model && result->model->failure)) {
    completion->effect_result = std::move(result);
  }
  std::move(callback).Run(std::move(completion));
}

}  // namespace taffy
