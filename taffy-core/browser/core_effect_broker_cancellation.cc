// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace mojom = core_service::mojom;
namespace {

std::optional<std::string_view> EffectTaskId(
    const mojom::EffectEnvelope& effect) {
  if (effect.storage_commit && !effect.storage_commit->task_id.empty()) {
    return effect.storage_commit->task_id;
  }
  if (effect.page_observation && !effect.page_observation->task_id.empty()) {
    return effect.page_observation->task_id;
  }
  // A model request is the one effect here that may already have cost a person
  // money by the time a cancellation arrives, so it is also the one a cancel
  // that could not reach it would fail on most expensively: the call would run
  // to completion and be billed against a task nobody is waiting for any more.
  if (effect.model_request && !effect.model_request->task_id.empty()) {
    return effect.model_request->task_id;
  }
  if (effect.permission_request &&
      !effect.permission_request->task_id.empty()) {
    return effect.permission_request->task_id;
  }
  // A tool job is the effect that keeps costing after the cancel: a worker
  // process holds its memory, its CPU budget and the descriptors the browser
  // opened for it until something tells it to stop. Reading the task here is
  // what lets CancelTask claim the effect and what lets the supervisor kill
  // the exact process behind it.
  if (effect.tool_job && !effect.tool_job->task_id.empty()) {
    return effect.tool_job->task_id;
  }
  return std::nullopt;
}

}  // namespace

void CoreEffectBroker::OnGenerationDisconnected(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (handlers_.revoke_capabilities) {
    handlers_.revoke_capabilities.Run(generation);
  }

  std::vector<std::string> lost;
  lost.reserve(pending_.size());
  for (const auto& [effect_id, pending] : pending_) {
    if (pending.effect->operation->service_generation == generation) {
      lost.push_back(effect_id);
    }
  }

  // The generation counter restarts at one with every browser process. The
  // broker's own pending set is the authoritative set for this incarnation,
  // so storage settles these exact identities instead of sweeping every
  // durable row that happens to carry the same non-persistent counter.
  if (handlers_.cancel_generation) {
    handlers_.cancel_generation.Run(generation, std::vector<std::string>(lost));
  }

  for (const std::string& effect_id : lost) {
    auto it = pending_.find(effect_id);
    if (it == pending_.end()) {
      continue;
    }
    const mojom::EffectStatus status =
        it->second.effect->retry_class == mojom::RetryClass::kConsequential
            ? mojom::EffectStatus::kOutcomeUnknown
            : mojom::EffectStatus::kUnavailable;
    OnAdapterCompleted(effect_id, MakeTerminal(*it->second.effect, status));
  }
}

void CoreEffectBroker::CancelTask(std::string_view task_id,
                                  uint64_t generation) {
  CancelTask(task_id, generation, base::DoNothing());
}

void CoreEffectBroker::CancelTask(std::string_view task_id,
                                  uint64_t generation,
                                  base::OnceClosure settled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!settled) {
    settled = base::DoNothing();
  }
  if (task_id.empty() || generation != active_generation_) {
    std::move(settled).Run();
    return;
  }
  std::vector<std::string> claimed;
  for (const auto& [effect_id, pending] : pending_) {
    if (pending.effect->operation->service_generation == generation &&
        EffectTaskId(*pending.effect) == task_id) {
      claimed.push_back(effect_id);
    }
  }

  if (claimed.empty()) {
    std::move(settled).Run();
    return;
  }

  // Wrap every callback before starting a terminal commit. A synchronous
  // journal adapter is allowed to re-enter this broker from the original
  // callback, and the drain signal must still cover the whole claimed set.
  base::RepeatingClosure one_settled =
      base::BarrierClosure(claimed.size(), std::move(settled));
  for (const std::string& effect_id : claimed) {
    auto it = pending_.find(effect_id);
    if (it == pending_.end()) {
      one_settled.Run();
      continue;
    }
    CompletionCallback original = std::move(it->second.callback);
    it->second.callback = base::BindOnce(
        [](CompletionCallback callback, base::RepeatingClosure drain,
           mojom::EffectResultPtr result) {
          std::move(callback).Run(std::move(result));
          drain.Run();
        },
        std::move(original), one_settled);
  }

  // Ask typed adapters to stop their exact work before synthesizing a
  // terminal. A synchronous adapter cancellation may claim and journal its
  // own exact CANCELLED result; anything that cannot be stopped falls through
  // to the conservative terminal below.
  if (handlers_.cancel_task) {
    handlers_.cancel_task.Run(task_id, generation);
  }

  for (const std::string& effect_id : claimed) {
    auto it = pending_.find(effect_id);
    if (it == pending_.end() || it->second.terminal_claimed) {
      continue;
    }
    // A storage commit is one atomic SQLite transaction. There is no adapter
    // cancellation for it, and fabricating CANCELLED here can disagree with
    // a row that committed just before its callback crossed back. Keep it in
    // the claimed drain, but let only its real terminal settle it. This is
    // especially load-bearing for Pause: the later RevokeAuthority effect
    // must not cancel the PauseSettled commit that it is waiting to publish.
    if (it->second.effect->kind == mojom::EffectKind::kStorageCommit) {
      continue;
    }
    const mojom::EffectStatus status =
        it->second.effect->retry_class == mojom::RetryClass::kConsequential
            ? mojom::EffectStatus::kOutcomeUnknown
            : mojom::EffectStatus::kCancelled;
    OnAdapterCompleted(effect_id, MakeTerminal(*it->second.effect, status));
  }
}

}  // namespace taffy
