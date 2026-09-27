// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/core_service_impl.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace mojom = core_service::mojom;

CoreServiceImpl::CoreServiceImpl(
    mojo::PendingReceiver<mojom::TaffyCoreService> receiver)
    : receiver_(this, std::move(receiver)),
      core_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

CoreServiceImpl::~CoreServiceImpl() = default;

void CoreServiceImpl::Initialize(mojom::CoreBootstrapPtr bootstrap,
                                 mojo::PendingRemote<mojom::CoreHost> host,
                                 InitializeCallback callback) {
  if (initialize_in_flight_ || ready_ || shutdown_started_ || !bootstrap ||
      bootstrap->service_generation == 0u || !host.is_valid()) {
    auto result = mojom::CoreBootstrapResult::New();
    result->status = mojom::InitializationStatus::kInvalidBootstrap;
    result->accepted_generation = 0u;
    std::move(callback).Run(std::move(result));
    return;
  }

  initialize_in_flight_ = true;
  generation_ = bootstrap->service_generation;
  host_.Bind(std::move(host));
  core_.AsyncCall(&RustCore::Initialize)
      .WithArgs(std::move(bootstrap))
      .Then(base::BindOnce(&CoreServiceImpl::OnInitialized,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CoreServiceImpl::OpenSession(
    const std::string& session_id,
    mojo::PendingReceiver<mojom::CoreSession> receiver) {
  if (!ready_ || shutdown_started_ || session_id.empty() ||
      sessions_.size() >= kMaxSessions) {
    return;
  }
  sessions_.Add(this, std::move(receiver));
  if (initial_publication_pending_) {
    initial_publication_pending_ = false;
    OnStateBindingsRegistered(
        mojom::PendingApprovalRegistrationStatus::kRegistered);
    RefreshDeadlineSchedule();
  }
}

void CoreServiceImpl::PrepareForShutdown(PrepareForShutdownCallback callback) {
  if (shutdown_started_) {
    std::move(callback).Run(true);
    return;
  }
  shutdown_started_ = true;
  ready_ = false;
  initial_publication_pending_ = false;
  StopDeadlineSchedule();
  sessions_.Clear();
  publication_queue_.Clear();
  core_.AsyncCall(&RustCore::PrepareForShutdown)
      .Then(base::BindOnce(&CoreServiceImpl::OnShutdownPrepared,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CoreServiceImpl::Submit(mojom::CoreServiceCommandPtr command,
                             SubmitCallback callback) {
  if (!ready_ || shutdown_started_ || !command || !command->operation ||
      command->operation->service_generation != generation_) {
    std::move(callback).Run(UnavailableAdmission(command.get()));
    return;
  }
  if (!publication_queue_.TryReserveSubmission()) {
    std::move(callback).Run(BackpressureAdmission(command.get()));
    return;
  }
  const mojom::CoreServiceCommandKind kind = command->kind;
  core_.AsyncCall(&RustCore::Submit)
      .WithArgs(std::move(command), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnSubmitted,
                           weak_factory_.GetWeakPtr(), std::move(callback),
                           kind));
}

// `AccountTokenValidationResult` is a non-nullable response and
// RustCore::ValidateAccountTokenResponse returns nullptr twice: once when the
// request will not project, and once when ToMojoAccountTokenValidationResult
// refuses the answer's shape — which is the path that has just zeroed the
// token buffers, so it is the refusal most worth surviving. Forwarding the
// nullptr would fail receive-side validation and end the utility generation
// instead of this one sign-in attempt.
void CoreServiceImpl::OnAccountTokenValidationReply(
    std::string operation_id,
    mojom::AccountNetworkOperation operation_kind,
    mojom::AccountAuthMethod auth_method,
    uint64_t target_rotation,
    ValidateAccountTokenResponseCallback callback,
    mojom::AccountTokenValidationResultPtr result) {
  if (!ready_ || shutdown_started_ || !result) {
    result = mojom::AccountTokenValidationResult::New();
    result->status = mojom::AccountTokenValidationStatus::kInvalidResponse;
    result->operation_id = std::move(operation_id);
    result->operation_kind = operation_kind;
    result->auth_method = auth_method;
    result->target_rotation = target_rotation;
  }
  std::move(callback).Run(std::move(result));
}

void CoreServiceImpl::ValidateAccountTokenResponse(
    mojom::AccountTokenValidationRequestPtr request,
    ValidateAccountTokenResponseCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    auto result = mojom::AccountTokenValidationResult::New();
    result->status =
        request && request->operation
            ? mojom::AccountTokenValidationStatus::kStaleGeneration
            : mojom::AccountTokenValidationStatus::kInvalidResponse;
    if (request && request->operation) {
      result->operation_id = request->operation->operation_id;
      result->operation_kind = request->operation_kind;
      result->auth_method = request->expected_auth_method;
      result->target_rotation = request->target_rotation;
    }
    std::move(callback).Run(std::move(result));
    return;
  }
  std::string operation_id = request->operation->operation_id;
  const mojom::AccountNetworkOperation operation_kind = request->operation_kind;
  const mojom::AccountAuthMethod auth_method = request->expected_auth_method;
  const uint64_t target_rotation = request->target_rotation;
  core_.AsyncCall(&RustCore::ValidateAccountTokenResponse)
      .WithArgs(std::move(request), generation_, NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnAccountTokenValidationReply,
                           weak_factory_.GetWeakPtr(), std::move(operation_id),
                           operation_kind, auth_method, target_rotation,
                           std::move(callback)));
}

void CoreServiceImpl::EvaluatePolicy(mojom::PolicyEvaluationRequestPtr request,
                                     EvaluatePolicyCallback callback) {
  if (!ready_ || shutdown_started_ || !request || !request->operation ||
      request->operation->service_generation != generation_) {
    // INVALID_REQUEST is what the task engine records as `Deny(Unsupported)`
    // against the action. Four sites in this file answer it and none of them
    // said so, which is why a refused proposal and a malformed one read the
    // same in the journal (decision 0136 section 1).
    LOG(WARNING) << "[taffy_core_policy_refused] at=entry"
                 << " ready=" << ready_
                 << " shutdown=" << shutdown_started_
                 << " request=" << (request ? 1 : 0) << " generation="
                 << (request && request->operation
                         ? static_cast<int64_t>(
                               request->operation->service_generation)
                         : -1)
                 << " expected=" << static_cast<int64_t>(generation_);
    auto result = mojom::PolicyEvaluationResult::New();
    if (request && request->operation) {
      result->operation_id = request->operation->operation_id;
    }
    result->status = mojom::PolicyEvaluationStatus::kInvalidRequest;
    std::move(callback).Run(std::move(result));
    return;
  }
  core_.AsyncCall(&RustCore::EvaluatePolicy)
      .WithArgs(std::move(request))
      .Then(base::BindOnce(&CoreServiceImpl::OnPolicyEvaluated,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CoreServiceImpl::CompleteTaskSettlement(
    mojom::TaskSettlementBindingPtr settlement) {
  if (!ready_ || shutdown_started_ || !settlement ||
      settlement->service_generation != generation_) {
    return;
  }
  core_.AsyncCall(&RustCore::CompleteTaskSettlement)
      .WithArgs(std::move(settlement), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::PublishBatch,
                           weak_factory_.GetWeakPtr()));
}

void CoreServiceImpl::Cancel(mojom::OperationEnvelopePtr operation) {
  if (!ready_ || !operation || operation->service_generation != generation_) {
    return;
  }
  core_.AsyncCall(&RustCore::Cancel)
      .WithArgs(std::move(operation), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::PublishBatch,
                           weak_factory_.GetWeakPtr()));
}

void CoreServiceImpl::DeliverEffectResult(mojom::EffectResultPtr result) {
  if (!ready_ || !result || !result->operation ||
      result->operation->service_generation != generation_) {
    return;
  }
  EffectIdentity identity{
      .effect_id = result->effect_id,
      .operation_id = result->operation->operation_id,
      .service_generation = result->operation->service_generation,
      .task_revision = result->operation->task_revision,
      .deadline_monotonic_ms = result->operation->deadline_monotonic_ms,
      .idempotency_key = result->operation->idempotency_key,
  };
  const bool is_storage_result =
      result->kind == mojom::EffectKind::kStorageCommit && result->storage;
  core_.AsyncCall(&RustCore::DeliverEffectResult)
      .WithArgs(std::move(result), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnEffectResultDelivered,
                           weak_factory_.GetWeakPtr(), std::move(identity),
                           is_storage_result));
}

uint64_t CoreServiceImpl::NowMonotonicMillis() const {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

void CoreServiceImpl::OnInitialized(InitializeCallback callback,
                                    CoreInitializationBatch batch) {
  mojom::CoreBootstrapResultPtr result = std::move(batch.result);
  const bool valid =
      result && result->status == mojom::InitializationStatus::kReady &&
      result->accepted_generation == generation_ && batch.states.size() == 1u &&
      batch.states.front().state && batch.states.front().browser_bindings &&
      batch.states.front().state->service_generation == generation_ &&
      batch.states.front().browser_bindings->service_generation ==
          generation_ &&
      batch.states.front().state->sequence ==
          batch.states.front().browser_bindings->state_sequence;
  if (!valid || !host_.is_bound()) {
    initialize_in_flight_ = false;
    if (result) {
      result->status = mojom::InitializationStatus::kInvalidBootstrap;
      result->accepted_generation = 0u;
    } else {
      result = mojom::CoreBootstrapResult::New();
      result->status = mojom::InitializationStatus::kInvalidBootstrap;
    }
    host_.reset();
    std::move(callback).Run(std::move(result));
    return;
  }
  CoreStatePublication publication = std::move(batch.states.front());
  auto bindings = publication.browser_bindings.Clone();
  host_->RegisterPendingApprovals(
      std::move(bindings),
      base::BindOnce(&CoreServiceImpl::OnInitialBindingsRegistered,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(result), std::move(publication)));
}

void CoreServiceImpl::OnInitialBindingsRegistered(
    InitializeCallback callback,
    mojom::CoreBootstrapResultPtr result,
    CoreStatePublication publication,
    mojom::PendingApprovalRegistrationStatus status) {
  initialize_in_flight_ = false;
  ready_ = status == mojom::PendingApprovalRegistrationStatus::kRegistered &&
           host_.is_bound() && publication.state &&
           publication.browser_bindings && !shutdown_started_ &&
           RememberTaskRevisions(*publication.browser_bindings);
  if (!ready_) {
    result->status = mojom::InitializationStatus::kInvalidBootstrap;
    result->accepted_generation = 0u;
    host_.reset();
  } else {
    CoreResponseBatch initial;
    initial.states.push_back(std::move(publication));
    if (!publication_queue_.TryPushBatch(std::move(initial))) {
      ready_ = false;
      result->status = mojom::InitializationStatus::kInvalidBootstrap;
      result->accepted_generation = 0u;
      host_.reset();
      std::move(callback).Run(std::move(result));
      return;
    }
    publication_queue_.TakeBatchAndRetainStates();
    // Initialize's reply and CoreHost travel over different pipes. Sending
    // the reply first cannot make the browser ready before a restored effect
    // arrives. OpenSession is the browser's acknowledgment that it received
    // initialization success and bound the session. Release this already
    // registered publication there, preserving effect-before-state ordering.
    initial_publication_pending_ = true;
  }
  std::move(callback).Run(std::move(result));
}

void CoreServiceImpl::OnSubmitted(SubmitCallback callback,
                                  mojom::CoreServiceCommandKind kind,
                                  CoreResponseBatch batch) {
  mojom::AdmissionPtr admission = std::move(batch.admission);
  if (!admission) {
    admission = mojom::Admission::New();
    admission->status = mojom::AdmissionStatus::kInvalidCommand;
  }
  if (!ready_ || shutdown_started_ || !host_.is_bound()) {
    publication_queue_.ReleaseSubmissionReservation();
    admission->status = mojom::AdmissionStatus::kCoreUnavailable;
  } else if (!publication_queue_.TryPushSubmittedBatch(std::move(batch))) {
    admission->status = mojom::AdmissionStatus::kCoreUnavailable;
    FailStatePublication("submitted-batch-queue-refused");
  } else {
    RefreshDeadlineSchedule();
    DrainPublicationBatches();
    if (!ready_) {
      admission->status = mojom::AdmissionStatus::kCoreUnavailable;
    }
  }
  // The one place the ordered core's answer and the command it answered meet
  // in this process. A refusal names its kind and status and nothing else: the
  // bridge answers seven bare InvalidCommand statuses on the start path alone,
  // and a browser that only sees "invalid" cannot tell which command it was.
  if (admission->status != mojom::AdmissionStatus::kAccepted) {
    LOG(WARNING) << "[taffy_core_admission_refused] kind="
                 << static_cast<int>(kind)
                 << " status=" << static_cast<int>(admission->status);
  }
  std::move(callback).Run(std::move(admission));
}

void CoreServiceImpl::OnPolicyEvaluated(
    EvaluatePolicyCallback callback,
    mojom::PolicyEvaluationResultPtr result) {
  const bool granted =
      result && result->status == mojom::PolicyEvaluationStatus::kGranted;
  const bool direct_grant = granted && result->minted_grant &&
                            result->minted_grant->authority_subject &&
                            result->minted_grant->authority_subject->kind ==
                                mojom::AuthoritySubjectKind::kDirectUserIntent;
  if (!result || granted != !!result->minted_grant ||
      (!granted && !!result->direct_observation_effect) ||
      (granted && !result->minted_grant->authority_subject) ||
      (direct_grant != !!result->direct_observation_effect)) {
    LOG(WARNING) << "[taffy_core_policy_refused] at=result-shape"
                 << " status="
                 << (result ? static_cast<int>(result->status) : -1)
                 << " granted=" << granted
                 << " grant=" << (result && result->minted_grant ? 1 : 0);
    auto invalid = mojom::PolicyEvaluationResult::New();
    if (result) {
      invalid->operation_id = result->operation_id;
    }
    invalid->status = mojom::PolicyEvaluationStatus::kInvalidRequest;
    std::move(callback).Run(std::move(invalid));
    return;
  }
  if (result->status != mojom::PolicyEvaluationStatus::kGranted) {
    std::move(callback).Run(std::move(result));
    return;
  }
  if (!host_.is_bound()) {
    LOG(WARNING) << "[taffy_core_policy_refused] at=host-unbound";
    result->status = mojom::PolicyEvaluationStatus::kInvalidRequest;
    result->minted_grant.reset();
    result->direct_observation_effect.reset();
    std::move(callback).Run(std::move(result));
    return;
  }
  host_->RegisterCapability(
      result->minted_grant.Clone(),
      base::BindOnce(&CoreServiceImpl::OnCapabilityRegistered,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(result)));
}

void CoreServiceImpl::OnCapabilityRegistered(
    EvaluatePolicyCallback callback,
    mojom::PolicyEvaluationResultPtr result,
    mojom::CapabilityRegistrationStatus status) {
  // A duplicate is not accepted here: the service cannot prove the browser's
  // existing record is byte-for-byte the same grant. Callers must evaluate a
  // fresh proposal identity rather than treating identity collision as success.
  if (status != mojom::CapabilityRegistrationStatus::kRegistered) {
    // The ordered core granted and the browser would not take the grant. This
    // is the one refusal that discards a GRANT after policy said yes, and it
    // was the whole of why a first-move navigate read as `Deny(Unsupported)`
    // with no line anywhere naming a refusal.
    LOG(WARNING) << "[taffy_core_policy_refused] at=capability-registration"
                 << " status=" << static_cast<int>(status);
    result->status = mojom::PolicyEvaluationStatus::kInvalidRequest;
    result->minted_grant.reset();
    result->direct_observation_effect.reset();
  }
  std::move(callback).Run(std::move(result));
}

void CoreServiceImpl::OnShutdownPrepared(PrepareForShutdownCallback callback,
                                         CoreResponseBatch batch) {
  // The shutdown batch carries only cancellation state; no new effect may be
  // emitted once shutdown starts.
  publication_queue_.Clear();
  active_task_effect_.reset();
  parallel_source_reads_.clear();
  published_task_revisions_.clear();
  active_source_read_revision_.reset();
  task_effect_completion_commit_.reset();
  state_registration_in_flight_ = false;
  static_cast<void>(batch);
  host_.reset();
  std::move(callback).Run(true);
}

mojom::AdmissionPtr CoreServiceImpl::UnavailableAdmission(
    const mojom::CoreServiceCommand* command) const {
  auto admission = mojom::Admission::New();
  if (command && command->operation) {
    admission->operation_id = command->operation->operation_id;
  }
  admission->status = mojom::AdmissionStatus::kCoreUnavailable;
  return admission;
}

mojom::AdmissionPtr CoreServiceImpl::BackpressureAdmission(
    const mojom::CoreServiceCommand* command) const {
  mojom::AdmissionPtr admission = UnavailableAdmission(command);
  admission->status = mojom::AdmissionStatus::kBackpressure;
  return admission;
}

}  // namespace taffy
