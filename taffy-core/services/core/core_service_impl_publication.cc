// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {

namespace mojom = core_service::mojom;

// The publication sequence, and the one failure that ends a generation.
//
// A state reaches a surface only after the browser has acknowledged the
// approval bindings that state carries, so publication is a two-step
// conversation rather than a send: register, then emit and publish. Every step
// that cannot be completed lands in FailStatePublication, which drops the host
// rather than publishing a state whose bindings nobody holds — a surface
// waiting on an approval it can never be told about is worse than a generation
// that ends.

void CoreServiceImpl::PublishBatch(CoreResponseBatch batch) {
  if (!ready_ || !host_.is_bound() || shutdown_started_) {
    return;
  }
  if (!publication_queue_.TryPushBatch(std::move(batch))) {
    FailStatePublication("batch-queue-refused");
    return;
  }
  RefreshDeadlineSchedule();
  DrainPublicationBatches();
}

void CoreServiceImpl::DrainPublicationBatches() {
  while (!answer_publication_in_flight_ && publication_queue_.HasBatches() &&
         ready_ && host_.is_bound() && !shutdown_started_) {
    CoreResponseBatch& next = publication_queue_.FrontBatch();
    if (next.task_answer_events.empty()) {
      PublishBatchContents();
      continue;
    }
    answer_publication_in_flight_ = true;
    host_->PublishTaskAnswerEvents(
        std::move(next.task_answer_events),
        base::BindOnce(&CoreServiceImpl::OnTaskAnswerEventsPublished,
                       weak_factory_.GetWeakPtr()));
    return;
  }
}

void CoreServiceImpl::OnTaskAnswerEventsPublished(bool accepted) {
  answer_publication_in_flight_ = false;
  if (!accepted || !ready_ || !host_.is_bound() || shutdown_started_ ||
      !publication_queue_.HasBatches()) {
    FailStatePublication("answer-publication-refused");
    return;
  }
  PublishBatchContents();
  DrainPublicationBatches();
}

void CoreServiceImpl::PublishBatchContents() {
  if (!publication_queue_.HasBatches()) {
    FailStatePublication("missing-publication-batch");
    return;
  }
  for (const auto& state : publication_queue_.FrontBatch().states) {
    if (!state.state || !state.browser_bindings ||
        state.state->service_generation != generation_ ||
        state.browser_bindings->service_generation != generation_ ||
        state.state->sequence != state.browser_bindings->state_sequence) {
      FailStatePublication("invalid-state-publication-shape");
      return;
    }
  }
  CoreResponseBatch batch = publication_queue_.TakeBatchAndRetainStates();
  for (auto& effect : batch.effects) {
    host_->EmitEffect(std::move(effect));
  }
  if (!active_task_effect_ || task_effect_completion_commit_) {
    RegisterNextState();
  }
}

void CoreServiceImpl::RegisterNextState() {
  if (state_registration_in_flight_ || !publication_queue_.HasStates() ||
      !ready_ || !host_.is_bound() || shutdown_started_) {
    return;
  }
  state_registration_in_flight_ = true;
  host_->RegisterPendingApprovals(
      publication_queue_.FrontState().browser_bindings.Clone(),
      base::BindOnce(&CoreServiceImpl::OnStateBindingsRegistered,
                     weak_factory_.GetWeakPtr()));
}

void CoreServiceImpl::OnStateBindingsRegistered(
    mojom::PendingApprovalRegistrationStatus status) {
  state_registration_in_flight_ = false;
  if (status != mojom::PendingApprovalRegistrationStatus::kRegistered ||
      !publication_queue_.HasStates() || !ready_ || !host_.is_bound() ||
      shutdown_started_) {
    FailStatePublication("state-binding-registration-refused");
    return;
  }
  CoreStatePublication publication =
      publication_queue_.TakeStateAndRetainTaskEffects();
  for (auto& effect : publication.effects) {
    host_->EmitEffect(std::move(effect));
  }
  if (!AcknowledgeCommittedTaskEffect(
          publication.acknowledges_task_effect_commit,
          *publication.browser_bindings)) {
    FailStatePublication("task-effect-acknowledgment-refused");
    return;
  }
  if (!RememberTaskRevisions(*publication.browser_bindings)) {
    FailStatePublication("invalid-published-task-revision");
    return;
  }
  mojom::CoreStateUpdatePtr state = std::move(publication.state);
  // ExecuteTaskEffect must precede PublishState for the same sequence.
  // Both are methods on the one CoreHost pipe, so the browser sees them in
  // send order. An approval or permission surface is settled only when the
  // state carrying it reaches the native owner, and no later sequence can be
  // registered while the effect is outstanding -- publishing first would
  // therefore leave the surface waiting on a publication that already
  // happened and can never repeat. The browser refuses to stall on that
  // ordering as well (CoreServiceManager::QueueTaskSurface); the two guards
  // are independent on purpose, and neither is the other's fallback.
  // Bootstrap reads carried by this durable state must leave before another
  // state can register a newer task revision. A chained next-source proposal
  // still owns active_task_effect_, so the ordinary dispatcher alone would
  // hold these already-authorized reads until their exact revision was stale.
  DispatchReadySourceReads();
  DispatchNextTaskEffect();
  if (!ready_ || !host_.is_bound()) {
    return;
  }
  host_->PublishState(std::move(state));
  if (task_effect_completion_commit_ || !active_task_effect_) {
    RegisterNextState();
  }
}

void CoreServiceImpl::FailStatePublication(const char* reason) {
  LOG(ERROR) << "[taffy_core_publication_refused] reason=" << reason;
  publication_queue_.Clear();
  active_task_effect_.reset();
  parallel_source_reads_.clear();
  published_task_revisions_.clear();
  active_source_read_revision_.reset();
  task_effect_completion_commit_.reset();
  answer_publication_in_flight_ = false;
  state_registration_in_flight_ = false;
  ready_ = false;
  initial_publication_pending_ = false;
  StopDeadlineSchedule();
  sessions_.Clear();
  host_.reset();
}

}  // namespace taffy
