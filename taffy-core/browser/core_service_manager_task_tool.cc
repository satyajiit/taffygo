// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/core_tool_validation.h"
#include "taffy/browser/profile_tool_artifact_broker.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

bool SameOperation(const service::OperationEnvelope& left,
                   const service::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

service::TaskEffectCompletionStatus CompletionStatus(
    service::EffectStatus status) {
  switch (status) {
    case service::EffectStatus::kCompleted:
      return service::TaskEffectCompletionStatus::kSucceeded;
    case service::EffectStatus::kDenied:
    case service::EffectStatus::kInvalidResult:
      return service::TaskEffectCompletionStatus::kRefused;
    case service::EffectStatus::kCancelled:
      return service::TaskEffectCompletionStatus::kCancelled;
    case service::EffectStatus::kOutcomeUnknown:
      return service::TaskEffectCompletionStatus::kOutcomeUnknown;
    case service::EffectStatus::kDeadlineExceeded:
    case service::EffectStatus::kResourceLimit:
    case service::EffectStatus::kUnavailable:
      return service::TaskEffectCompletionStatus::kUnavailable;
  }
  return service::TaskEffectCompletionStatus::kRefused;
}

bool ResultMatches(const service::TaskEffectBinding& binding,
                   const service::EffectResult& result) {
  if (!binding.operation || !binding.tool_job || !binding.tool_job->job ||
      !result.operation || result.effect_id != binding.effect_id ||
      result.kind != service::EffectKind::kToolJob ||
      !SameOperation(*binding.operation, *result.operation)) {
    return false;
  }
  if (result.status != service::EffectStatus::kCompleted) {
    return true;
  }
  return result.tool &&
         IsValidCoreToolResult(*result.tool, *binding.tool_job->job,
                               service::kMaxIdentifierBytes,
                               service::kMaxEffectBytes);
}

service::EffectResultPtr MediaProbeFactsResult(
    const service::TaskEffectBinding& binding,
    service::MediaProbeResultPtr media_probe) {
  if (!media_probe || !binding.operation || !binding.tool_job ||
      !binding.tool_job->job) {
    return nullptr;
  }
  auto result = service::EffectResult::New();
  result->operation = binding.operation.Clone();
  result->effect_id = binding.effect_id;
  result->status = service::EffectStatus::kCompleted;
  result->kind = service::EffectKind::kToolJob;
  result->tool = service::ToolEffectResult::New();
  result->tool->job_id = binding.tool_job->job->job_id;
  result->tool->status = service::ToolTerminalStatus::kCompleted;
  result->tool->success = service::ToolSuccess::New();
  result->tool->success->operation_kind = service::ToolOperation::kProbeMedia;
  result->tool->success->media_probe = std::move(media_probe);
  return result;
}

}  // namespace

void CoreServiceManager::ExecuteTaskToolJob(
    service::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  service::TaskEffectBindingPtr original = effect.Clone();
  tool_artifact_broker_->PrepareTaskToolJob(
      std::move(effect),
      base::BindOnce(&CoreServiceManager::OnTaskToolJobPrepared,
                     weak_factory_.GetWeakPtr(), std::move(original),
                     std::move(callback)));
}

void CoreServiceManager::OnTaskToolJobPrepared(
    service::TaskEffectBindingPtr original,
    ExecuteTaskEffectCallback callback,
    service::TaskEffectBindingPtr prepared) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!prepared || !prepared->operation || !prepared->tool_job ||
      !prepared->tool_job->job ||
      prepared->operation->service_generation != service_generation_) {
    if (original && original->tool_job) {
      tool_artifact_broker_->AbandonTaskToolJob(original->tool_job->job_id);
    }
    std::move(callback).Run(MakeTaskEffectCompletion(
        original.get(), service::TaskEffectCompletionStatus::kRefused));
    return;
  }
  auto envelope = service::EffectEnvelope::New();
  envelope->operation = prepared->operation.Clone();
  envelope->effect_id = prepared->effect_id;
  envelope->kind = service::EffectKind::kToolJob;
  envelope->retry_class = service::RetryClass::kConsequential;
  envelope->tool_job = prepared->tool_job->job.Clone();
  service::TaskEffectBindingPtr retained = prepared.Clone();
  effect_broker_->Dispatch(
      std::move(envelope),
      base::BindOnce(&CoreServiceManager::OnTaskToolJobCompleted,
                     weak_factory_.GetWeakPtr(), std::move(retained),
                     std::move(callback)));
}

void CoreServiceManager::OnTaskToolJobCompleted(
    service::TaskEffectBindingPtr binding,
    ExecuteTaskEffectCallback callback,
    service::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  service::TaskEffectCompletionStatus status =
      result ? CompletionStatus(result->status)
             : service::TaskEffectCompletionStatus::kUnavailable;
  if (!binding || !result || !ResultMatches(*binding, *result)) {
    status = service::TaskEffectCompletionStatus::kRefused;
  }
  if (status != service::TaskEffectCompletionStatus::kSucceeded || !result) {
    if (binding && binding->tool_job) {
      tool_artifact_broker_->AbandonTaskToolJob(binding->tool_job->job_id);
    }
    auto completion = MakeTaskEffectCompletion(binding.get(), status);
    std::move(callback).Run(std::move(completion));
    return;
  }
  service::MediaProbeResultPtr media_probe;
  if (binding->tool_job && binding->tool_job->job &&
      binding->tool_job->job->operation_kind ==
          service::ToolOperation::kProbeMedia &&
      result->tool && result->tool->success &&
      result->tool->success->media_probe) {
    media_probe = result->tool->success->media_probe.Clone();
  }
  tool_artifact_broker_->RetainTaskToolOutput(
      *binding, std::move(result),
      base::BindOnce(&CoreServiceManager::OnTaskToolOutputRetained,
                     weak_factory_.GetWeakPtr(), std::move(binding),
                     std::move(media_probe), std::move(callback)));
}

void CoreServiceManager::OnTaskToolOutputRetained(
    service::TaskEffectBindingPtr binding,
    service::MediaProbeResultPtr media_probe,
    ExecuteTaskEffectCallback callback,
    service::TaskToolOutputReceiptPtr receipt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const service::TaskEffectCompletionStatus status =
      receipt ? service::TaskEffectCompletionStatus::kSucceeded
              : service::TaskEffectCompletionStatus::kRefused;
  if (!receipt && binding && binding->tool_job) {
    tool_artifact_broker_->AbandonTaskToolJob(binding->tool_job->job_id);
  }
  auto completion = MakeTaskEffectCompletion(binding.get(), status);
  completion->tool_output = std::move(receipt);
  if (status == service::TaskEffectCompletionStatus::kSucceeded &&
      media_probe) {
    completion->effect_result =
        MediaProbeFactsResult(*binding, std::move(media_probe));
  }
  // No worker-produced byte array crosses back into Core Service. A probe's
  // five bounded scalars are reconstructed only after browser validation;
  // every other output remains represented by its receipt alone.
  std::move(callback).Run(std::move(completion));
}

}  // namespace taffy
