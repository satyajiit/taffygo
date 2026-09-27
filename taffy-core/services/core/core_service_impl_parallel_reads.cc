// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {
namespace mojom = core_service::mojom;
namespace {

constexpr size_t kMaxParallelSourceReads = 4u;

bool IsSourceRead(const mojom::TaskEffectBinding& effect) {
  if (effect.kind != mojom::TaskReducerEffectKind::kDispatchAction ||
      !effect.action || !effect.action->executable ||
      !effect.action->observation || !effect.operation) {
    return false;
  }
  const auto& action = *effect.action->executable;
  return effect.action->idempotency_key.starts_with("agent-observation-") &&
         action.operation_kind == mojom::TaskActionOperationKind::kDomRead &&
         action.action_class == mojom::PolicyActionClass::kObservePage &&
         action.tool_name == "browser.dom.read" && !action.node_id &&
         !action.tab_id.empty();
}

bool CanRunDuringSourceReads(const mojom::TaskEffectBinding& effect) {
  if (effect.kind == mojom::TaskReducerEffectKind::kRevokeAuthority ||
      effect.kind == mojom::TaskReducerEffectKind::kAwaitInFlightWork) {
    return true;
  }
  if (effect.kind != mojom::TaskReducerEffectKind::kAskPolicy ||
      !effect.policy) {
    return false;
  }
  const auto& policy = *effect.policy;
  return policy.idempotency_key.starts_with("agent-observation-") &&
         policy.operation_kind == mojom::TaskActionOperationKind::kDomRead &&
         policy.action_class == mojom::PolicyActionClass::kObservePage &&
         policy.tool_name == "browser.dom.read" && !policy.node_id;
}

bool SameRead(const mojom::TaskEffectBinding& expected,
              const mojom::TaskEffectBinding& actual,
              const mojom::TaskEffectCompletion& completion) {
  return expected.operation && actual.operation && completion.operation &&
         expected.operation.Equals(actual.operation) &&
         expected.operation.Equals(completion.operation) &&
         expected.effect_id == actual.effect_id &&
         expected.effect_id == completion.effect_id &&
         expected.task_id == actual.task_id &&
         expected.task_id == completion.task_id &&
         expected.kind == actual.kind && expected.kind == completion.kind;
}

}  // namespace

void CoreServiceImpl::DispatchReadySourceReads() {
  if (!ready_ || !host_.is_bound() || shutdown_started_) {
    return;
  }
  while (publication_queue_.HasTaskEffects()) {
    const auto* next = publication_queue_.FrontTaskEffect();
    if (!next) {
      FailStatePublication("missing-queued-task-effect");
      return;
    }
    if (!IsSourceRead(*next)) {
      return;
    }
    const std::string& tab = next->action->executable->tab_id;
    const bool tab_busy =
        std::ranges::any_of(parallel_source_reads_, [&tab](const auto& read) {
          return read.effect->action->executable->tab_id == tab;
        });
    if (parallel_source_reads_.size() >= kMaxParallelSourceReads || tab_busy) {
      return;
    }
    auto effect = publication_queue_.TakeTaskEffect();
    parallel_source_reads_.push_back({effect.Clone(), nullptr, false});
    auto callback_effect = effect.Clone();
    host_->ExecuteTaskEffect(
        std::move(effect),
        base::BindOnce(&CoreServiceImpl::OnParallelSourceReadExecuted,
                       weak_factory_.GetWeakPtr(), std::move(callback_effect)));
    RefreshDeadlineSchedule();
  }
}

bool CoreServiceImpl::PrepareTaskEffectDispatch() {
  DispatchReadySourceReads();
  if (!ready_ || !host_.is_bound() || shutdown_started_) {
    return false;
  }
  if (publication_queue_.HasTaskEffects()) {
    const auto* next = publication_queue_.FrontTaskEffect();
    if (!next) {
      FailStatePublication("missing-queued-task-effect");
      return false;
    }
    // A source read left at the front is waiting on the parallel/tab bound.
    if (!IsSourceRead(*next) &&
        (parallel_source_reads_.empty() || CanRunDuringSourceReads(*next))) {
      active_task_effect_ = publication_queue_.TakeTaskEffect();
      active_source_read_revision_.reset();
      return true;
    }
  }
  SettleNextSourceRead();
  return false;
}

void CoreServiceImpl::OnParallelSourceReadExecuted(
    mojom::TaskEffectBindingPtr effect,
    mojom::TaskEffectCompletionPtr completion) {
  if (!ready_ || shutdown_started_ || !host_.is_bound()) {
    return;
  }
  if (!effect || !completion) {
    FailStatePublication("invalid-source-read-callback");
    return;
  }
  auto found =
      std::ranges::find_if(parallel_source_reads_, [&](const auto& read) {
        return read.effect->effect_id == effect->effect_id;
      });
  if (found == parallel_source_reads_.end() || found->completion) {
    LOG(WARNING) << "[taffy_source_read_late_reply]";
    return;
  }
  if (!SameRead(*found->effect, *effect, *completion) ||
      mojom::TaskEffectCompletion::SerializeAsMessage(&completion)
              .payload_num_bytes() > mojom::kMaxEffectBytes) {
    FailStatePublication("invalid-source-read-terminal");
    return;
  }
  found->completion = std::move(completion);
  SettleNextSourceRead();
  RefreshDeadlineSchedule();
}

mojom::TaskEffectCompletionPtr CoreServiceImpl::ActivateNextSourceRead() {
  if (!ready_ || shutdown_started_ || !host_.is_bound() ||
      active_task_effect_ || task_effect_completion_commit_ ||
      state_registration_in_flight_ || publication_queue_.HasStates() ||
      parallel_source_reads_.empty() ||
      !parallel_source_reads_.front().completion) {
    return nullptr;
  }
  const auto& pending = parallel_source_reads_.front();
  const auto revision = published_task_revisions_.find(pending.effect->task_id);
  if (revision == published_task_revisions_.end() ||
      revision->second < pending.effect->operation->task_revision) {
    FailStatePublication("missing-source-read-commit-revision");
    return nullptr;
  }
  ParallelSourceRead read = std::move(parallel_source_reads_.front());
  parallel_source_reads_.pop_front();
  active_task_effect_ = read.effect.Clone();
  active_source_read_revision_ = revision->second;
  task_effect_completion_synthesized_ = read.synthesized;
  return std::move(read.completion);
}

void CoreServiceImpl::SettleNextSourceRead() {
  auto completion = ActivateNextSourceRead();
  if (completion) {
    OnTaskEffectExecuted(active_task_effect_.Clone(), std::move(completion));
  }
}

bool CoreServiceImpl::RememberTaskRevisions(
    const mojom::CoreStateBrowserBindings& bindings) {
  std::map<std::string, uint64_t> revisions;
  for (const auto& revision : bindings.task_revisions) {
    if (!revision || revision->service_generation != generation_ ||
        !revisions.emplace(revision->task_id, revision->task_revision).second) {
      return false;
    }
    const auto previous = published_task_revisions_.find(revision->task_id);
    if (previous != published_task_revisions_.end() &&
        revision->task_revision < previous->second) {
      return false;
    }
  }
  published_task_revisions_ = std::move(revisions);
  return true;
}

uint64_t CoreServiceImpl::SourceReadDeadline(uint64_t deadline) const {
  for (const auto& read : parallel_source_reads_) {
    if (read.completion) {
      continue;
    }
    const uint64_t candidate = read.effect->operation->deadline_monotonic_ms;
    if (candidate != 0u && (deadline == 0u || candidate < deadline)) {
      deadline = candidate;
    }
  }
  return deadline;
}

void CoreServiceImpl::ExpireSourceReads(uint64_t now) {
  for (auto& read : parallel_source_reads_) {
    if (read.completion ||
        read.effect->operation->deadline_monotonic_ms == 0u ||
        read.effect->operation->deadline_monotonic_ms > now) {
      continue;
    }
    auto completion = mojom::TaskEffectCompletion::New();
    completion->operation = read.effect->operation.Clone();
    completion->effect_id = read.effect->effect_id;
    completion->task_id = read.effect->task_id;
    completion->kind = read.effect->kind;
    completion->status = mojom::TaskEffectCompletionStatus::kOutcomeUnknown;
    read.completion = std::move(completion);
    read.synthesized = true;
  }
  SettleNextSourceRead();
}

}  // namespace taffy
