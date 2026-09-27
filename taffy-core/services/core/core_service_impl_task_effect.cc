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

namespace {

bool IsDeferredSurfaceKind(mojom::TaskReducerEffectKind kind) {
  return kind == mojom::TaskReducerEffectKind::kRequestApproval ||
         kind == mojom::TaskReducerEffectKind::kRequestPermission ||
         kind == mojom::TaskReducerEffectKind::kRequestFieldValues;
}

// The state the bridge publishes beside a chained command is the one its
// commit just made durable: the task bound once, in this generation, at
// exactly the revision the chained command expects. A state naming the
// staged revision instead describes a commit that has not happened yet.
bool StateIsDurableBeforeContinuation(const CoreStatePublication& state,
                                      const mojom::StorageCommitEffect& commit,
                                      uint64_t generation) {
  if (!state.browser_bindings || state.acknowledges_task_effect_commit) {
    return false;
  }
  size_t matches = 0u;
  for (const auto& revision : state.browser_bindings->task_revisions) {
    if (!revision || revision->task_id != commit.task_id) {
      continue;
    }
    if (revision->service_generation != generation ||
        revision->task_revision != commit.expected_revision) {
      return false;
    }
    ++matches;
  }
  return matches == 1u;
}

}  // namespace

void CoreServiceImpl::DispatchNextTaskEffect() {
  if (active_task_effect_ || task_effect_completion_commit_ ||
      !ready_ || !host_.is_bound() ||
      shutdown_started_) {
    return;
  }
  if (!PrepareTaskEffectDispatch()) {
    return;
  }
  task_effect_completion_synthesized_ = false;
  if (!active_task_effect_ || !active_task_effect_->operation) {
    FailStatePublication("invalid-task-effect-shape");
    return;
  }
  if (active_task_effect_->kind ==
      mojom::TaskReducerEffectKind::kAwaitInFlightWork) {
    // The browser-owned settlement binding is the sole executor for this
    // effect. RegisterPendingApprovals has already revoked/cancelled the exact
    // task work and sent CompleteTaskSettlement before acknowledging the
    // state. Sending ExecuteTaskEffect as well would race two reducer inputs.
    active_task_effect_.reset();
    active_source_read_revision_.reset();
    DispatchNextTaskEffect();
    return;
  }
  if (active_task_effect_->kind == mojom::TaskReducerEffectKind::kAskPolicy) {
    if (!active_task_effect_->policy) {
      FailStatePublication("missing-task-policy-effect");
      return;
    }
    host_->EvaluateTaskPolicy(
        active_task_effect_->policy.Clone(),
        base::BindOnce(&CoreServiceImpl::OnTaskPolicyEvaluated,
                       weak_factory_.GetWeakPtr(),
                       active_task_effect_.Clone()));
    // The effect's browser-owned deadline is now the utility's to watch too.
    RefreshDeadlineSchedule();
    return;
  }
  host_->ExecuteTaskEffect(
      active_task_effect_.Clone(),
      base::BindOnce(&CoreServiceImpl::OnTaskEffectExecuted,
                     weak_factory_.GetWeakPtr(), active_task_effect_.Clone()));
  RefreshDeadlineSchedule();
}

void CoreServiceImpl::OnTaskPolicyEvaluated(
    mojom::TaskEffectBindingPtr effect,
    mojom::PolicyEvaluationResultPtr result) {
  if (!ready_) {
    return;
  }
  if (!effect || !result) {
    FailStatePublication("invalid-task-policy-callback");
    return;
  }
  if (IsLateTaskEffectReply(*effect)) {
    LOG(WARNING) << "[taffy_task_effect_late_reply] kind="
                 << static_cast<uint32_t>(effect->kind);
    return;
  }
  core_.AsyncCall(&RustCore::CompleteTaskPolicy)
      .WithArgs(std::move(effect), std::move(result), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnTaskEffectCompletionSubmitted,
                           weak_factory_.GetWeakPtr()));
}

void CoreServiceImpl::OnTaskEffectExecuted(
    mojom::TaskEffectBindingPtr effect,
    mojom::TaskEffectCompletionPtr completion) {
  if (!ready_) {
    return;
  }
  if (!effect || !completion) {
    FailStatePublication("invalid-task-effect-callback");
    return;
  }
  if (IsLateTaskEffectReply(*effect)) {
    LOG(WARNING) << "[taffy_task_effect_late_reply] kind="
                 << static_cast<uint32_t>(effect->kind);
    return;
  }
  // Content-free: the effect kind, the completion status and, for a model
  // call, the provider's HTTP status and failure class. The bridge answers
  // a refused completion with one admission status and no reason, so this
  // line is what says which terminal it was refusing.
  uint32_t provider_http_status = 0u;
  int failure_class = -1;
  bool has_body = false;
  if (completion->effect_result && completion->effect_result->model) {
    const mojom::ModelEffectResult& model = *completion->effect_result->model;
    provider_http_status = model.provider_http_status;
    has_body = !model.completion.empty();
    if (model.failure) {
      failure_class = static_cast<int>(model.failure->error_class);
    }
  }
  LOG(INFO) << "[taffy_task_effect_completed] kind="
            << static_cast<uint32_t>(effect->kind)
            << " status=" << static_cast<uint32_t>(completion->status)
            << " http=" << provider_http_status << " failure=" << failure_class
            << " body=" << has_body;
  core_.AsyncCall(&RustCore::CompleteTaskEffect)
      .WithArgs(std::move(effect), std::move(completion), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnTaskEffectCompletionSubmitted,
                           weak_factory_.GetWeakPtr()));
}

void CoreServiceImpl::OnTaskEffectCompletionSubmitted(CoreResponseBatch batch) {
  if (!active_task_effect_) {
    FailStatePublication("missing-active-task-effect-completion");
    return;
  }
  if (!batch.admission) {
    FailStatePublication("missing-task-effect-completion-admission");
    return;
  }
  if (batch.admission->status != mojom::AdmissionStatus::kAccepted) {
    LOG(ERROR) << "[taffy_task_effect_completion_refused] admission="
               << static_cast<uint32_t>(batch.admission->status)
               << " kind=" << static_cast<uint32_t>(active_task_effect_->kind)
               << " synthesized=" << task_effect_completion_synthesized_;
    // A refused completion is answered once more by the utility itself, so
    // the action is settled and the walk goes on. Only the second refusal of
    // the same effect ends the core.
    if (!task_effect_completion_synthesized_) {
      SynthesizeTaskEffectCompletion(TaskEffectSynthesis::kRefusedCompletion);
      return;
    }
    FailStatePublication("refused-task-effect-completion");
    return;
  }
  if (!batch.states.empty()) {
    const bool readiness_state =
        IsDeferredSurfaceKind(active_task_effect_->kind) &&
        batch.states.size() == 1u && batch.effects.empty() &&
        batch.states.front().task_effects.empty();
    if (!readiness_state) {
      LOG(ERROR) << "[taffy_task_effect_completion_refused] shape=state kind="
                 << static_cast<uint32_t>(active_task_effect_->kind)
                 << " states=" << batch.states.size()
                 << " effects=" << batch.effects.size();
      FailStatePublication("premature-task-effect-completion-state");
      return;
    }
    // This state does not claim a reducer commit. It advances only the public
    // state sequence and repeats the exact pending binding after Rust accepted
    // the surface terminal. The browser may now submit a command it already
    // owns; the Core Service still supplies the only admission result.
    active_task_effect_.reset();
    active_source_read_revision_.reset();
    PublishBatch(std::move(batch));
    return;
  }
  if (batch.effects.size() > 1u) {
    FailStatePublication("multiple-task-effect-completion-commits");
    return;
  }
  if (!batch.effects.empty()) {
    const mojom::EffectEnvelopePtr& effect = batch.effects.front();
    if (!effect || !SetPendingTaskEffectCommit(*effect)) {
      FailStatePublication("invalid-task-effect-commit");
      return;
    }
    PublishBatch(std::move(batch));
    return;
  }
  active_task_effect_.reset();
  active_source_read_revision_.reset();
  DispatchNextTaskEffect();
  if (!active_task_effect_) {
    RegisterNextState();
  }
}

void CoreServiceImpl::OnEffectResultDelivered(EffectIdentity identity,
                                              bool is_storage_result,
                                              CoreResponseBatch batch) {
  if (!IsPendingTaskEffectCommit(identity)) {
    // A refused storage completion publishes nothing, and the two ways it
    // happens are both otherwise silent: the commit answered a start whose
    // open the bridge had already expired, or it named an operation the
    // bridge never held. Either way a task the browser journaled has gone
    // nowhere, so say so; the status is the whole of what is known here.
    if (is_storage_result && batch.admission &&
        batch.admission->status != mojom::AdmissionStatus::kAccepted) {
      LOG(WARNING) << "[taffy_core_completion_refused] status="
                   << static_cast<int>(batch.admission->status);
    }
    PublishBatch(std::move(batch));
    return;
  }
  if (!is_storage_result || !active_task_effect_ || !batch.admission ||
      batch.admission->status != mojom::AdmissionStatus::kAccepted) {
    FailStatePublication("invalid-task-effect-commit-result");
    return;
  }

  const mojom::EffectEnvelope* continuation = nullptr;
  for (const auto& effect : batch.effects) {
    if (!effect || !effect->storage_commit ||
        effect->kind != mojom::EffectKind::kStorageCommit ||
        effect->storage_commit->operation_kind !=
            mojom::StorageOperation::kAppendTaskCommit ||
        effect->storage_commit->task_id != active_task_effect_->task_id) {
      continue;
    }
    if (continuation) {
      FailStatePublication("duplicate-task-effect-continuation");
      return;
    }
    continuation = effect.get();
  }

  // Some effect terminals start the next durable reducer command before they
  // expose a state. Keep following that exact storage identity. The bridge
  // publishes the state the commit just made durable beside that chained
  // command, so a surface sees every hop of the walk rather than only the
  // hops that leave the core. That state is still not the acknowledgment:
  // begin_submit has staged the new revision in memory, and the continuation
  // is not durable until its own browser terminal returns. So it must carry
  // the task at the revision the continuation expects, and one carrying the
  // staged revision fails closed before anything is published.
  if (continuation) {
    if (batch.effects.size() != 1u || batch.states.size() > 1u) {
      FailStatePublication("invalid-task-effect-continuation-shape");
      return;
    }
    if (!batch.states.empty() &&
        !StateIsDurableBeforeContinuation(
            batch.states.front(), *continuation->storage_commit, generation_)) {
      FailStatePublication("speculative-task-effect-continuation-state");
      return;
    }
    if (!SetPendingTaskEffectCommit(*continuation)) {
      FailStatePublication("invalid-task-effect-continuation");
      return;
    }
    PublishBatch(std::move(batch));
    return;
  }

  if (batch.states.size() != 1u || !task_effect_completion_commit_ ||
      task_effect_completion_commit_->operation_kind !=
          mojom::StorageOperation::kAppendTaskCommit ||
      task_effect_completion_commit_->task_id != active_task_effect_->task_id) {
    FailStatePublication("missing-task-effect-commit-state");
    return;
  }
  batch.states.front().acknowledges_task_effect_commit = true;
  PublishBatch(std::move(batch));
}

bool CoreServiceImpl::SetPendingTaskEffectCommit(
    const mojom::EffectEnvelope& effect) {
  if (!active_task_effect_ || !active_task_effect_->operation ||
      !effect.operation || effect.effect_id.empty() ||
      effect.operation->operation_id.empty() ||
      effect.operation->idempotency_key.empty() ||
      effect.operation->service_generation != generation_ ||
      effect.kind != mojom::EffectKind::kStorageCommit ||
      !effect.storage_commit) {
    return false;
  }
  const mojom::StorageCommitEffect& commit = *effect.storage_commit;
  const bool appends_task =
      commit.operation_kind == mojom::StorageOperation::kAppendTaskCommit;
  if (appends_task) {
    const uint64_t expected_task_revision =
        task_effect_completion_commit_ &&
                task_effect_completion_commit_->operation_kind ==
                    mojom::StorageOperation::kAppendTaskCommit
            ? task_effect_completion_commit_->resulting_revision
            : active_source_read_revision_.value_or(
                  active_task_effect_->operation->task_revision);
    if (effect.effect_id != effect.operation->operation_id ||
        commit.task_id != active_task_effect_->task_id ||
        commit.expected_revision != expected_task_revision ||
        commit.resulting_revision <= commit.expected_revision ||
        effect.operation->task_revision != commit.resulting_revision) {
      return false;
    }
  } else {
    const bool is_library_mutation =
        commit.operation_kind == mojom::StorageOperation::kUpsertLibraryEntry ||
        commit.operation_kind == mojom::StorageOperation::kRemoveLibraryEntry;
    const bool is_memory_mutation =
        commit.operation_kind == mojom::StorageOperation::kUpsertMemory ||
        commit.operation_kind == mojom::StorageOperation::kDeleteMemory;
    const bool matches_active_effect =
        (active_task_effect_->kind ==
             mojom::TaskReducerEffectKind::kRunLibraryTool &&
         is_library_mutation) ||
        (active_task_effect_->kind ==
             mojom::TaskReducerEffectKind::kRunMemoryTool &&
         is_memory_mutation);
    if (!matches_active_effect || task_effect_completion_commit_ ||
        effect.operation->task_revision !=
            active_task_effect_->operation->task_revision) {
      // A non-task storage mutation may be the first durable leg of a Library
      // or Memory task effect. Every continuation after it is an append of the
      // action outcome to the task journal.
      return false;
    }
  }
  task_effect_completion_commit_ = PendingTaskEffectCommit{
      .identity =
          {
              .effect_id = effect.effect_id,
              .operation_id = effect.operation->operation_id,
              .service_generation = effect.operation->service_generation,
              .task_revision = effect.operation->task_revision,
              .deadline_monotonic_ms = effect.operation->deadline_monotonic_ms,
              .idempotency_key = effect.operation->idempotency_key,
          },
      .operation_kind = commit.operation_kind,
      .task_id = commit.task_id,
      .expected_revision = commit.expected_revision,
      .resulting_revision = commit.resulting_revision,
  };
  return true;
}

bool CoreServiceImpl::IsPendingTaskEffectCommit(
    const EffectIdentity& identity) const {
  if (!task_effect_completion_commit_) {
    return false;
  }
  const EffectIdentity& expected = task_effect_completion_commit_->identity;
  return identity.effect_id == expected.effect_id &&
         identity.operation_id == expected.operation_id &&
         identity.service_generation == expected.service_generation &&
         identity.task_revision == expected.task_revision &&
         identity.deadline_monotonic_ms == expected.deadline_monotonic_ms &&
         identity.idempotency_key == expected.idempotency_key;
}

bool CoreServiceImpl::AcknowledgeCommittedTaskEffect(
    bool acknowledges_task_effect_commit,
    const mojom::CoreStateBrowserBindings& bindings) {
  if (!acknowledges_task_effect_commit) {
    return true;
  }
  if (!active_task_effect_ || !active_task_effect_->operation ||
      !task_effect_completion_commit_) {
    LOG(ERROR)
        << "[taffy_task_effect_ack_refused] reason=missing-active-effect";
    return false;
  }
  const PendingTaskEffectCommit& pending = *task_effect_completion_commit_;
  if (pending.operation_kind != mojom::StorageOperation::kAppendTaskCommit ||
      pending.task_id != active_task_effect_->task_id ||
      pending.identity.task_revision != pending.resulting_revision ||
      pending.resulting_revision <=
          active_task_effect_->operation->task_revision) {
    LOG(ERROR) << "[taffy_task_effect_ack_refused] "
                  "reason=invalid-pending-commit";
    return false;
  }
  size_t matches = 0;
  size_t task_matches = 0;
  size_t generation_matches = 0;
  uint64_t matched_task_revision = 0u;
  for (const auto& revision : bindings.task_revisions) {
    if (!revision || revision->task_id != active_task_effect_->task_id) {
      continue;
    }
    ++task_matches;
    matched_task_revision = revision->task_revision;
    if (revision->service_generation != generation_) {
      continue;
    }
    ++generation_matches;
    if (revision->task_revision == pending.resulting_revision) {
      ++matches;
    }
  }
  if (matches != 1u) {
    LOG(ERROR) << "[taffy_task_effect_ack_refused] reason=revision-mismatch"
               << " active_kind="
               << static_cast<uint32_t>(active_task_effect_->kind)
               << " active_revision="
               << active_task_effect_->operation->task_revision
               << " expected_revision=" << pending.expected_revision
               << " resulting_revision=" << pending.resulting_revision
               << " published_revision=" << matched_task_revision
               << " task_matches=" << task_matches
               << " generation_matches=" << generation_matches
               << " advanced_matches=" << matches
               << " binding_count=" << bindings.task_revisions.size();
    return false;
  }
  task_effect_completion_commit_.reset();
  active_task_effect_.reset();
  active_source_read_revision_.reset();
  return true;
}

}  // namespace taffy
