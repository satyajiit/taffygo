// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <utility>

#include "base/time/time.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/browser/model/profile_entitlement_cache.h"
#include "taffy/browser/model/profile_provider_listing_fetcher.h"
#include "taffy/browser/profile_model_register.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {

void CoreServiceManager::OnMojoDisconnect(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_) {
    ++late_reply_count_;
    return;
  }
  const bool expected = teardown_reason_ != TeardownReason::kNone;
  HandleDisconnect(/*unexpected=*/!expected);
}

void CoreServiceManager::HandleDisconnect(bool unexpected) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (disconnect_handled_) {
    return;
  }
  disconnect_handled_ = true;
  const uint64_t lost_generation = service_generation_;
  // Observed link destinations are browser memory, not durable task state.
  // Revoke them before tearing down the old remote or advancing generation so
  // restored work cannot reuse a handle minted by the lost core process.
  InvalidateTaskObservedLinks(browser_context_);
  session_.reset();
  backup_protocol_->OnServiceDisconnected();
  host_receiver_.reset();
  service_.remote().reset();
  launch_in_progress_ = false;
  // Native provider flows are live work owned by the portable pending record
  // in this exact generation. Once that record is gone, carrying the vendor
  // leg forward would make later cancellation impossible: the successor core
  // must refuse an identity it never admitted. Abort before advancing and
  // publish one exact unavailable event so Android drops the same attempt.
  if (provider_auth_broker_) {
    provider_auth_broker_->AbortGeneration(lost_generation);
  }
  // The listing bypasses the durable effect broker because it is live,
  // read-only work. Cancel its exact generation here before advancing so a
  // late vault or network answer cannot be correlated with the successor.
  provider_listing_fetcher_->CancelGeneration(lost_generation);
  if (entitlement_cache_) {
    entitlement_cache_->CancelPendingMint();
  }
  entitlement_fetch_in_flight_ = false;
  pending_live_effect_ids_.clear();
  model_register_->Withdraw();
  effect_broker_->OnGenerationDisconnected(lost_generation);
  // Broker-backed task effects can settle synchronously above and remove
  // their own custody. Everything left belongs to a purpose-built browser
  // executor whose late callback has no authority in the next generation.
  // A discovery bootstrap still waiting for a window is answered while its
  // identity is still tracked, so the answer settles it rather than
  // counting as a late reply.
  ResolveDeferredTaskDiscoveryBootstrapsUnavailable();
  pending_host_task_effect_ids_.clear();
  actor_leases_.RevokeGeneration(lost_generation);
  capabilities_.RevokeGeneration(lost_generation);
  value_references_.RevokeGeneration(lost_generation);
  ResolvePendingAdmissionsUnavailable();
  ResolvePendingPolicyUnavailable();
  ResolvePendingPageInspectionsUnavailable();
  ResolvePendingPageExportsUnavailable();
  ResolvePendingTaskSurfacesUnavailable();
  ResolvePendingCoreApiPreparations(false);
  AbandonAllOpenHandovers();
  emitted_permission_requests_.clear();
  emitted_field_value_requests_.clear();
  preapproved_form_actions_.clear();
  task_answer_sequences_.clear();
  completed_task_answer_order_.clear();
  active_task_answer_count_ = 0;
  if (field_value_requests_) {
    // The values this generation held are gone with it, so the sheets that
    // were collecting more of them are closed rather than left open over a
    // vault that can no longer hold what they collect.
    field_value_requests_->CloseAllRequests();
  }
  completed_task_settlements_.clear();
  const bool restores_same_browser_session =
      !shutdown_started_ &&
      (unexpected || teardown_reason_ == TeardownReason::kIdle);
  if (restores_same_browser_session) {
    accepted_approvals_.ResetForServiceGenerationLoss();
  } else {
    accepted_approvals_.Reset();
  }
  state_bindings_.Reset();
  state_cache_.Reset();
  AdvanceGeneration();

  if (shutdown_started_) {
    SetAvailability(Availability::kShuttingDown);
    return;
  }

  if (unexpected) {
    const CoreServiceRecoveryPolicy::Decision decision =
        recovery_policy_.RecordUnexpectedDisconnect(base::TimeTicks::Now());
    launch_not_before_ = base::TimeTicks::Now() + decision.delay;
    SetAvailability(decision.schedule_automatic_restart
                        ? Availability::kUnavailable
                        : Availability::kCircuitOpen);
  } else {
    launch_not_before_ = base::TimeTicks();
    SetAvailability(Availability::kStopped);
  }
  teardown_reason_ = TeardownReason::kNone;
}

void CoreServiceManager::AdvanceGeneration() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (service_generation_ == std::numeric_limits<uint64_t>::max()) {
    // Exhaustion is fail closed for this browser session.
    recovery_policy_.OpenCircuitForSession();
    return;
  }
  ++service_generation_;
  tool_supervisor_->SetActiveGeneration(service_generation_);
  tool_artifact_broker_->SetActiveGeneration(service_generation_);
}

void CoreServiceManager::SetAvailability(Availability availability) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (availability_ == availability) {
    return;
  }
  availability_ = availability;
  if (availability == Availability::kReady) {
    RefreshIdleTeardown();
  } else {
    idle_teardown_timer_.Stop();
    // These cadences belong to a live session. Leaving them armed after an
    // idle release would keep waking a profile whose service is intentionally
    // asleep; the next lazy launch starts both again.
    entitlement_poke_timer_.Stop();
  }
  for (Observer& observer : observers_) {
    observer.OnCoreAvailabilityChanged(availability);
  }
}

void CoreServiceManager::ResolvePendingCoreApiPreparations(bool ready) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto callbacks = std::move(pending_core_api_preparations_);
  pending_core_api_preparations_.clear();
  RefreshIdleTeardown();
  for (auto& callback : callbacks) {
    std::move(callback).Run(ready);
  }
}

void CoreServiceManager::OnServiceLaunched(
    const content::ServiceProcessInfo& info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observed_service_process_ =
      std::make_pair(info.service_process_id(), service_generation_);
}

void CoreServiceManager::OnServiceTerminatedNormally(
    const content::ServiceProcessInfo& info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!observed_service_process_ ||
      observed_service_process_->first != info.service_process_id()) {
    return;
  }
  const uint64_t observed_generation = observed_service_process_->second;
  observed_service_process_.reset();
  if (observed_generation != service_generation_) {
    return;
  }
  const bool expected = teardown_reason_ != TeardownReason::kNone;
  HandleDisconnect(/*unexpected=*/!expected);
}

void CoreServiceManager::OnServiceCrashed(
    const content::ServiceProcessInfo& info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!observed_service_process_ ||
      observed_service_process_->first != info.service_process_id()) {
    return;
  }
  const uint64_t observed_generation = observed_service_process_->second;
  observed_service_process_.reset();
  if (observed_generation != service_generation_) {
    return;
  }
  HandleDisconnect(/*unexpected=*/true);
}

}  // namespace taffy
