// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/policy_response_validation.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool IsLowerHexDigest(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool DigestMatches(const std::vector<uint8_t>& wire, std::string_view local) {
  return wire.size() == crypto::kSHA256Length &&
         local.size() == crypto::kSHA256Length &&
         std::ranges::equal(wire, base::as_byte_span(local));
}

bool IsDirectIntentId(std::string_view value) {
  return value.starts_with("direct-intent-") &&
         value.size() <= service_mojom::kMaxAuthoritySubjectIdBytes;
}

bool IsIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= service_mojom::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool IsValidTaskAuthority(
    const service_mojom::PolicyEvaluationRequest& request) {
  return request.authority_subject->kind ==
             service_mojom::AuthoritySubjectKind::kTask &&
         IsIdentifier(request.task_id) && !IsDirectIntentId(request.task_id) &&
         request.task_id == request.authority_subject->authority_subject_id &&
         request.actor_lease->task_id == request.task_id &&
         IsIdentifier(request.action_id);
}

service_mojom::PolicyEvaluationResultPtr MakePolicyResult(
    std::string operation_id,
    service_mojom::PolicyEvaluationStatus status) {
  auto result = service_mojom::PolicyEvaluationResult::New();
  result->operation_id = std::move(operation_id);
  result->status = status;
  return result;
}

// Whether one request may be admitted, and — when a task-context scope was
// refused for a reason that names itself — the code the proposal settles
// under. `refusal` stays empty for a malformed record, which is not a decision
// about the proposal and stays `kInvalidRequest`.
bool ValidatePolicyRequest(
    const service_mojom::PolicyEvaluationRequest& request,
    std::string_view browser_profile_id,
    std::string_view browser_session_id,
    uint64_t generation,
    const ActorLeaseRegistry& leases,
    base::TimeTicks now,
    std::optional<service_mojom::TaskActionResultCode>* refusal) {
  if (!request.operation || !request.principal || !request.scope ||
      !request.scope->origin || !request.actor_lease ||
      !request.authority_subject || !request.actor_lease->authority_subject ||
      request.operation->operation_id.empty() ||
      request.operation->operation_id.size() >
          service_mojom::kMaxOperationIdBytes ||
      request.operation->idempotency_key.empty() ||
      request.operation->idempotency_key.size() >
          service_mojom::kMaxIdempotencyKeyBytes ||
      request.authority_subject->authority_subject_id.empty() ||
      request.authority_subject->authority_subject_id.size() >
          service_mojom::kMaxAuthoritySubjectIdBytes ||
      !IsLowerHexDigest(request.proposal_digest) ||
      request.scope->profile_id != browser_profile_id ||
      request.scope->tab_id != request.actor_lease->tab_id ||
      request.actor_lease->profile_id != browser_profile_id ||
      request.actor_lease->authority_subject->kind !=
          request.authority_subject->kind ||
      request.actor_lease->authority_subject->authority_subject_id !=
          request.authority_subject->authority_subject_id ||
      request.actor_lease->service_generation != generation ||
      request.policy_version == 0u || request.now_utc_ms == 0u ||
      request.operation->deadline_monotonic_ms <= request.now_monotonic_ms ||
      request.expires_at_monotonic_ms <= request.now_monotonic_ms ||
      request.expires_at_monotonic_ms >
          request.operation->deadline_monotonic_ms ||
      request.expires_at_monotonic_ms >
          request.actor_lease->expires_at_monotonic_ms) {
    return false;
  }
  switch (request.context) {
    case service_mojom::PolicyEvaluationContext::kTask: {
      if (!IsValidTaskAuthority(request) || request.discovery) {
        return false;
      }
      const std::optional<service_mojom::TaskActionResultCode> scope_refusal =
          TaskScopeRefusalForOperation(request);
      if (scope_refusal) {
        if (refusal) {
          *refusal = scope_refusal;
        }
        return false;
      }
      break;
    }
    case service_mojom::PolicyEvaluationContext::kTaskDiscovery:
      if (!IsValidTaskAuthority(request) ||
          !TaskDiscoveryPolicyRequestHasExactShape(request,
                                                   browser_session_id)) {
        return false;
      }
      break;
    case service_mojom::PolicyEvaluationContext::kDirectUserObservation:
      if (request.authority_subject->kind !=
              service_mojom::AuthoritySubjectKind::kDirectUserIntent ||
          !IsDirectIntentId(request.authority_subject->authority_subject_id) ||
          !request.task_id.empty() || !request.actor_lease->task_id.empty() ||
          !request.action_id.empty() ||
          request.operation->task_revision != 0u ||
          request.principal->kind !=
              service_mojom::PolicyPrincipalKind::kAssistant ||
          request.principal->skill_version_id.has_value() ||
          request.action_class !=
              service_mojom::PolicyActionClass::kObservePage ||
          request.discovery ||
          request.context_risk != service_mojom::PolicyRiskClass::kLocalRead ||
          request.data_classes.size() != 1u ||
          request.data_classes.front() !=
              service_mojom::BipSensitivity::kNotSensitive ||
          request.approval ||
          request.actor_lease->control_mode !=
              service_mojom::TaskControlMode::kUser ||
          request.scope->origin->kind !=
              service_mojom::PolicyOriginKind::kTuple ||
          !request.scope->origin->serialization ||
          request.scope->origin->opaque_id ||
          request.operation_kind !=
              service_mojom::TaskActionOperationKind::kDomRead ||
          !DigestMatches(request.canonical_intent_digest,
                         crypto::SHA256HashString(request.proposal_digest)) ||
          request.scope->node_id || request.scope->destination_scope ||
          request.scope->destination_address ||
          !request.scope->allowed_redirects.empty() ||
          request.scope->required_graph_revision != 0u ||
          request.operation->deadline_monotonic_ms - request.now_monotonic_ms >
              service_mojom::kMaxDirectObservationDeadlineMs ||
          request.expires_at_monotonic_ms - request.now_monotonic_ms >
              service_mojom::kMaxDirectObservationLeaseMs) {
        return false;
      }
      break;
  }
  if (request.approval &&
      (request.approval->service_generation != generation ||
       request.approval->receipt_reference.empty() ||
       request.approval->receipt_reference.size() >
           service_mojom::kMaxIdentifierBytes ||
       request.approval->proposal_digest != request.proposal_digest ||
       request.approval->expires_at_utc_ms <= request.now_utc_ms ||
       request.approval->browser_session_id.empty() ||
       request.approval->browser_session_id.size() >
           service_mojom::kMaxIdentifierBytes ||
       request.approval->expires_at_monotonic_ms <
           request.expires_at_monotonic_ms)) {
    return false;
  }
  return leases.IsValidForGrant(
      ActorLeaseId{request.actor_lease->lease_id},
      request.authority_subject->kind,
      request.authority_subject->authority_subject_id,
      TabId{request.scope->tab_id}, browser_profile_id, generation,
      base::TimeTicks() + base::Milliseconds(request.expires_at_monotonic_ms),
      now);
}

}  // namespace

void CoreServiceManager::EvaluatePolicy(
    service_mojom::PolicyEvaluationRequestPtr request,
    CoreServicePolicyEvaluationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string operation_id = request && request->operation
                                       ? request->operation->operation_id
                                       : std::string();
  if (shutdown_started_ || recovery_policy_.circuit_open()) {
    LOG(WARNING) << "[taffy_policy_request_refused] at=core-unavailable"
                 << " shutdown=" << (shutdown_started_ ? 1 : 0)
                 << " circuit=" << (recovery_policy_.circuit_open() ? 1 : 0);
    std::move(callback).Run(MakePolicyResult(
        operation_id, service_mojom::PolicyEvaluationStatus::kCoreUnavailable));
    return;
  }
  if (!request || !request->operation) {
    LOG(WARNING) << "[taffy_policy_request_refused] at=no-request";
    std::move(callback).Run(MakePolicyResult(
        operation_id, service_mojom::PolicyEvaluationStatus::kInvalidRequest));
    return;
  }
  request->operation->service_generation = service_generation_;
  request->now_monotonic_ms = NowMonotonicMillis();
  request->now_utc_ms = NowUtcMillis();
  const bool duplicate_idempotency = std::any_of(
      pending_policy_evaluations_.begin(), pending_policy_evaluations_.end(),
      [&request](const auto& entry) {
        return entry.second.request && entry.second.request->operation &&
               entry.second.request->operation->idempotency_key ==
                   request->operation->idempotency_key;
      });
  std::optional<service_mojom::TaskActionResultCode> scope_refusal;
  const bool admitted =
      ValidatePolicyRequestForCurrentGeneration(*request, &scope_refusal);
  // Five discriminants, and only the first is about the proposal. The other
  // four are the browser saying it is already busy with something, and each
  // of them answered INVALID_REQUEST with nothing written down - which the
  // task engine stamps as `Deny(Unsupported)` against an action that was
  // never judged. Naming them is what tells a refused move from a collided
  // one.
  const bool at_capacity =
      pending_admissions_.size() + pending_policy_evaluations_.size() >=
      service_mojom::kMaxInFlightPerProfile;
  const bool same_admission = pending_admissions_.contains(operation_id);
  const bool same_evaluation =
      pending_policy_evaluations_.contains(operation_id);
  if (!admitted || at_capacity || same_admission || same_evaluation ||
      duplicate_idempotency) {
    if (admitted) {
      LOG(WARNING)
          << "[taffy_policy_request_collided]"
          << " at="
          << (same_admission      ? "pending-admission"
              : same_evaluation   ? "pending-evaluation"
              : at_capacity       ? "in-flight-cap"
                                  : "idempotency")
          << " ctx=" << static_cast<int>(request->context)
          << " op=" << static_cast<int>(request->operation_kind)
          << " discovery=" << (request->discovery ? 1 : 0)
          << " admissions=" << pending_admissions_.size()
          << " evaluations=" << pending_policy_evaluations_.size()
          << " key=" << request->operation->idempotency_key
          << " op_id=" << operation_id;
      for (const auto& entry : pending_policy_evaluations_) {
        LOG(WARNING) << "[taffy_policy_request_collided] held op_id="
                     << entry.first << " key="
                     << (entry.second.request && entry.second.request->operation
                             ? entry.second.request->operation->idempotency_key
                             : std::string())
                     << " ctx="
                     << (entry.second.request
                             ? static_cast<int>(entry.second.request->context)
                             : -1);
      }
    }
    // A scope this build understood and refused is a decision about the
    // proposal, so it settles the action under its own code and the model is
    // told what was refused (decision 0136 sections 1 and 5). Only a record
    // the browser could not read at all stays `kInvalidRequest`, which the
    // core records as `Unsupported`.
    if (!admitted) {
      // Which of this function's five contexts refused, and the facts that
      // tell the task cases apart. Without it a scope clause and a malformed
      // record reach the journal as the same word.
      LOG(WARNING)
          << "[taffy_policy_request_refused]"
          << " ctx=" << static_cast<int>(request->context)
          << " op=" << static_cast<int>(request->operation_kind)
          << " class=" << static_cast<int>(request->action_class)
          << " discovery=" << (request->discovery ? 1 : 0)
          << " named=" << (scope_refusal ? 1 : 0)
          << " code=" << (scope_refusal ? static_cast<int>(*scope_refusal) : -1)
          << " origin_kind="
          << (request->scope && request->scope->origin
                  ? static_cast<int>(request->scope->origin->kind)
                  : -1)
          << " dest_scope="
          << (request->scope && request->scope->destination_scope ? 1 : 0)
          << " dest_addr="
          << (request->scope && request->scope->destination_address ? 1 : 0)
          << " node=" << (request->scope && request->scope->node_id ? 1 : 0);
    }
    if (!admitted && scope_refusal) {
      auto denied = MakePolicyResult(
          operation_id, service_mojom::PolicyEvaluationStatus::kDenied);
      denied->denial = service_mojom::PolicyDenial::New();
      denied->denial->code = *scope_refusal;
      std::move(callback).Run(std::move(denied));
      return;
    }
    std::move(callback).Run(MakePolicyResult(
        operation_id, service_mojom::PolicyEvaluationStatus::kInvalidRequest));
    return;
  }
  pending_policy_evaluations_.emplace(
      operation_id,
      PendingPolicyEvaluation{std::move(request), std::move(callback), false});
  RefreshIdleTeardown();
  EnsureStarted();
  DispatchQueuedCommands();
}

bool CoreServiceManager::ValidatePolicyRequestForCurrentGeneration(
    const service_mojom::PolicyEvaluationRequest& request,
    std::optional<service_mojom::TaskActionResultCode>* refusal) const {
  return ValidatePolicyRequest(request, browser_profile_id_,
                               browser_session_id_, service_generation_,
                               actor_leases_, base::TimeTicks::Now(), refusal);
}

const service_mojom::PolicyEvaluationRequest*
CoreServiceManager::FindPendingPolicyRequestForGrant(
    const service_mojom::MintedCapabilityGrant& grant) const {
  const service_mojom::PolicyEvaluationRequest* match = nullptr;
  for (const auto& entry : pending_policy_evaluations_) {
    const PendingPolicyEvaluation& pending = entry.second;
    if (!pending.sent || !pending.request || !pending.request->operation ||
        pending.request->operation->idempotency_key != grant.idempotency_key) {
      continue;
    }
    if (match) {
      return nullptr;
    }
    match = pending.request.get();
  }
  return match;
}

void CoreServiceManager::OnPolicyEvaluated(
    uint64_t generation,
    std::string operation_id,
    service_mojom::PolicyEvaluationResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_policy_evaluations_.find(operation_id);
  if (generation != service_generation_ ||
      it == pending_policy_evaluations_.end()) {
    ++late_reply_count_;
    return;
  }
  const service_mojom::PolicyEvaluationRequest* request =
      it->second.request.get();
  if (!request || !result || result->operation_id != operation_id ||
      (result->status == service_mojom::PolicyEvaluationStatus::kGranted) !=
          !!result->minted_grant ||
      (result->minted_grant && !result->minted_grant->authority_subject) ||
      (!result->minted_grant && !!result->direct_observation_effect) ||
      (result->minted_grant &&
       (result->minted_grant->authority_subject->kind ==
        service_mojom::AuthoritySubjectKind::kDirectUserIntent) !=
           !!result->direct_observation_effect) ||
      (result->minted_grant &&
       !PolicyGrantMatchesRequest(*result->minted_grant, *request)) ||
      (result->direct_observation_effect &&
       !DirectObservationEffectMatchesRequest(
           *result->direct_observation_effect, *result->minted_grant,
           *request))) {
    // The core answered and the browser would not take the answer. This is
    // the one refusal on the path that can discard a GRANT, and it read as
    // `Unsupported` in the journal with nothing said, so a granted navigate
    // and a refused navigate were the same word.
    LOG(WARNING)
        << "[taffy_policy_request_refused] at=evaluated"
        << " have_request=" << (request ? 1 : 0)
        << " have_result=" << (result ? 1 : 0)
        << " status=" << (result ? static_cast<int>(result->status) : -1)
        << " grant=" << (result && result->minted_grant ? 1 : 0)
        << " grant_matches="
        << (result && result->minted_grant && request
                ? PolicyGrantMatchesRequest(*result->minted_grant, *request)
                : -1)
        << " op=" << (request ? static_cast<int>(request->operation_kind) : -1);
    result = MakePolicyResult(
        operation_id, service_mojom::PolicyEvaluationStatus::kInvalidRequest);
  }
  CoreServicePolicyEvaluationCallback callback = std::move(it->second.callback);
  pending_policy_evaluations_.erase(it);
  RefreshIdleTeardown();
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy
