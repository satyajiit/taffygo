// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {

namespace mojom = core_service::mojom;

// A reply for an effect that is no longer the active one is late: its
// deadline arrived and the utility spoke for it, or the walk has moved on.
// Reading it as the active effect's answer would settle the wrong action,
// and failing the core for it took every task down over a reply that
// changed nothing. Dropping it, counted, is the whole of what is owed.
bool CoreServiceImpl::IsLateTaskEffectReply(
    const mojom::TaskEffectBinding& effect) const {
  return !active_task_effect_ || !active_task_effect_->operation ||
         !effect.operation ||
         active_task_effect_->effect_id != effect.effect_id ||
         active_task_effect_->operation->operation_id !=
             effect.operation->operation_id;
}

// The utility's own answer for the active effect, given once: when the
// bridge refused the browser's answer, or when the effect's deadline arrived
// with no answer at all. A policy ask is denied — unsupported for an answer
// the bridge could not read, expired for one that never came — which the
// reducer settles the action with and the model is told. Every other effect
// is recorded with the one status that says the browser cannot tell, except
// the discovery bootstrap, whose refusal is the fact the walk ends the task
// under. Nothing here invents an outcome the browser did not report.
void CoreServiceImpl::SynthesizeTaskEffectCompletion(TaskEffectSynthesis why) {
  if (!active_task_effect_ || !active_task_effect_->operation) {
    FailStatePublication("missing-active-task-effect-synthesis");
    return;
  }
  task_effect_completion_synthesized_ = true;
  const bool deadline = why == TaskEffectSynthesis::kDeadline;
  LOG(WARNING) << "[taffy_task_effect_completion_synthesized] deadline="
               << deadline
               << " kind=" << static_cast<uint32_t>(active_task_effect_->kind);
  if (active_task_effect_->kind == mojom::TaskReducerEffectKind::kAskPolicy) {
    auto result = mojom::PolicyEvaluationResult::New();
    result->operation_id = active_task_effect_->operation->operation_id;
    result->status = mojom::PolicyEvaluationStatus::kDenied;
    result->denial = mojom::PolicyDenial::New(
        deadline ? mojom::TaskActionResultCode::kCapabilityExpired
                 : mojom::TaskActionResultCode::kUnsupported);
    core_.AsyncCall(&RustCore::CompleteTaskPolicy)
        .WithArgs(active_task_effect_.Clone(), std::move(result),
                  NowMonotonicMillis())
        .Then(base::BindOnce(&CoreServiceImpl::OnTaskEffectCompletionSubmitted,
                             weak_factory_.GetWeakPtr()));
    return;
  }
  auto completion = mojom::TaskEffectCompletion::New();
  completion->operation = active_task_effect_->operation.Clone();
  completion->effect_id = active_task_effect_->effect_id;
  completion->task_id = active_task_effect_->task_id;
  completion->kind = active_task_effect_->kind;
  completion->status =
      active_task_effect_->kind ==
              mojom::TaskReducerEffectKind::kPrepareDiscoveryTab
          ? mojom::TaskEffectCompletionStatus::kRefused
          : mojom::TaskEffectCompletionStatus::kOutcomeUnknown;
  core_.AsyncCall(&RustCore::CompleteTaskEffect)
      .WithArgs(active_task_effect_.Clone(), std::move(completion),
                NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnTaskEffectCompletionSubmitted,
                           weak_factory_.GetWeakPtr()));
}

}  // namespace taffy
