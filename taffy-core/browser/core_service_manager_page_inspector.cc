// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/direct_observation_digest.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace api_mojom = core_api::mojom;
namespace service_mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

CorePageObservationIdentity Identity(const DirectObservationContext& context) {
  return {.tab_id = context.tab_id,
          .frame_id = context.frame_id,
          .page_epoch = context.page_epoch,
          .origin = context.origin,
          .graph_revision = context.graph_revision};
}

bool SameDocument(const DirectObservationContext& live,
                  const CorePageObservationIdentity& expected) {
  return live.tab_id == expected.tab_id && live.frame_id == expected.frame_id &&
         live.page_epoch == expected.page_epoch &&
         live.origin == expected.origin;
}

api_mojom::PageInspectorSnapshotResultPtr MakeSnapshotResult(
    api_mojom::PageInspectorAvailability availability) {
  auto result = api_mojom::PageInspectorSnapshotResult::New();
  result->availability = availability;
  return result;
}

}  // namespace

api_mojom::PageInspectorDocumentsViewPtr
CoreServiceManager::GetPageInspectorDocuments(
    content::WebContents* web_contents) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto result = api_mojom::PageInspectorDocumentsView::New();
  if (!web_contents ||
      web_contents->GetBrowserContext() != browser_context_.get()) {
    result->availability =
        api_mojom::PageInspectorAvailability::kNoSelectedPage;
    return result;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const std::optional<DirectObservationContext> context =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!context ||
      context->host.size() > api_mojom::kMaxPageInspectorHostBytes) {
    result->availability =
        api_mojom::PageInspectorAvailability::kDocumentUnavailable;
    return result;
  }
  result->availability = api_mojom::PageInspectorAvailability::kAvailable;
  auto document = api_mojom::PageInspectorDocumentView::New();
  document->document_id = "selected-page";
  document->kind = api_mojom::PageInspectorDocumentKind::kSemanticPage;
  document->host = context->host;
  document->claims = {
      api_mojom::PageInspectorClaim::kBrowserValidated,
      api_mojom::PageInspectorClaim::kBounded,
  };
  result->documents.push_back(std::move(document));
  return result;
}

void CoreServiceManager::ObservePageForInspector(
    content::WebContents* web_contents,
    CoreServicePageInspectorCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Offers are capabilities to select nothing, but keeping an older one live
  // beside a newer preview would let a surface splice two consent moments.
  if (!web_contents ||
      web_contents->GetBrowserContext() != browser_context_.get()) {
    std::move(callback).Run(MakeSnapshotResult(
        api_mojom::PageInspectorAvailability::kNoSelectedPage));
    return;
  }
  if (shutdown_started_ || recovery_policy_.circuit_open()) {
    std::move(callback).Run(MakeSnapshotResult(
        api_mojom::PageInspectorAvailability::kCoreUnavailable));
    return;
  }
  if (availability_ != Availability::kReady || !session_.is_bound()) {
    if (pending_admissions_.size() + pending_policy_evaluations_.size() +
            pending_page_inspections_.size() +
            site_skill_offers_.pending_count() >=
        service_mojom::kMaxInFlightPerProfile) {
      std::move(callback).Run(MakeSnapshotResult(
          api_mojom::PageInspectorAvailability::kBackpressure));
      return;
    }
    pending_page_inspections_.push_back(
        PendingPageInspection{web_contents->GetWeakPtr(), std::move(callback)});
    RefreshIdleTeardown();
    EnsureStarted();
    return;
  }
  BeginPageInspectorPolicyEvaluation(web_contents, std::move(callback));
}

void CoreServiceManager::BeginPageInspectorPolicyEvaluation(
    content::WebContents* web_contents,
    CoreServicePageInspectorCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!web_contents ||
      web_contents->GetBrowserContext() != browser_context_.get()) {
    std::move(callback).Run(MakeSnapshotResult(
        api_mojom::PageInspectorAvailability::kNoSelectedPage));
    return;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const std::optional<DirectObservationContext> context =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!context) {
    std::move(callback).Run(MakeSnapshotResult(
        api_mojom::PageInspectorAvailability::kDocumentUnavailable));
    return;
  }

  const std::string nonce = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string direct_intent_id = "direct-intent-" + nonce;
  const std::string operation_id = "direct-observation-" + nonce;
  const std::string idempotency_key = operation_id;
  const uint64_t now = NowMonotonicMillis();
  const ActorLeaseResult lease = actor_leases_.IssueDirectObservation(
      direct_intent_id, TabId{context->tab_id},
      static_cast<uint32_t>(service_mojom::kMaxDirectObservationLeaseMs),
      base::TimeTicks::Now());
  if (lease.code != ActorLeaseResultCode::kIssued) {
    std::move(callback).Run(MakeSnapshotResult(
        api_mojom::PageInspectorAvailability::kPolicyDenied));
    return;
  }

  auto subject = service_mojom::AuthoritySubject::New();
  subject->kind = service_mojom::AuthoritySubjectKind::kDirectUserIntent;
  subject->authority_subject_id = direct_intent_id;
  auto request = service_mojom::PolicyEvaluationRequest::New();
  request->operation = service_mojom::OperationEnvelope::New();
  request->operation->operation_id = operation_id;
  request->operation->service_generation = service_generation_;
  request->operation->task_revision = 0u;
  request->operation->deadline_monotonic_ms =
      now + service_mojom::kMaxDirectObservationDeadlineMs;
  request->operation->idempotency_key = idempotency_key;
  request->now_monotonic_ms = now;
  request->now_utc_ms = static_cast<uint64_t>(
      std::max<int64_t>(0, base::Time::Now().InMillisecondsSinceUnixEpoch()));
  request->principal = service_mojom::PolicyPrincipal::New();
  request->principal->kind = service_mojom::PolicyPrincipalKind::kAssistant;
  request->action_class = service_mojom::PolicyActionClass::kObservePage;
  request->proposal_digest = ComputeDirectObservationDigest(
      *context, browser_profile_id_, direct_intent_id, operation_id,
      idempotency_key, service_generation_,
      FixedDirectObservationDigestOperands());
  request->operation_kind = service_mojom::TaskActionOperationKind::kDomRead;
  // Direct page inspection has no reducer ActionIntent record. Its canonical
  // intent is the already domain-separated, browser-created proposal; hashing
  // that value gives this request a fixed 32-byte binding which policy must
  // echo and no page or isolated process can choose.
  const auto canonical_intent_digest =
      crypto::SHA256Hash(base::as_byte_span(request->proposal_digest));
  request->canonical_intent_digest.assign(canonical_intent_digest.begin(),
                                          canonical_intent_digest.end());
  request->scope = service_mojom::PolicyCapabilityScope::New();
  request->scope->profile_id = browser_profile_id_;
  request->scope->tab_id = context->tab_id;
  request->scope->frame_id = context->frame_id;
  request->scope->page_epoch = context->page_epoch;
  request->scope->origin = service_mojom::PolicyOrigin::New();
  request->scope->origin->kind = service_mojom::PolicyOriginKind::kTuple;
  request->scope->origin->serialization = context->origin;
  request->scope->required_graph_revision = 0u;
  request->data_classes = {service_mojom::BipSensitivity::kNotSensitive};
  request->context_risk = service_mojom::PolicyRiskClass::kLocalRead;
  request->expires_at_monotonic_ms = std::min(
      lease.expires_at_monotonic_ms, request->operation->deadline_monotonic_ms);
  request->actor_lease = service_mojom::ActorLeaseFact::New();
  request->actor_lease->lease_id = lease.lease_id.value;
  request->actor_lease->service_generation = service_generation_;
  request->actor_lease->profile_id = browser_profile_id_;
  request->actor_lease->tab_id = context->tab_id;
  request->actor_lease->control_mode = service_mojom::TaskControlMode::kUser;
  request->actor_lease->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
  request->actor_lease->authority_subject = subject.Clone();
  request->context =
      service_mojom::PolicyEvaluationContext::kDirectUserObservation;
  request->authority_subject = std::move(subject);
  // The direct-read contract is compiled with the same policy bundle as task
  // creation. This is a binding checked against the Rust-minted grant, not a
  // browser-selected policy decision.
  request->policy_version = 1u;

  EvaluatePolicy(std::move(request),
                 base::BindOnce(&CoreServiceManager::OnDirectPolicyEvaluated,
                                weak_factory_.GetWeakPtr(),
                                CoreDirectObservationRequest{
                                    web_contents->GetWeakPtr(),
                                    Identity(*context), lease.lease_id,
                                    std::move(callback)}));
}

void CoreServiceManager::QueuePendingPageInspectorPolicies() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<PendingPageInspection> pending =
      std::move(pending_page_inspections_);
  pending_page_inspections_.clear();
  RefreshIdleTeardown();
  for (PendingPageInspection& inspection : pending) {
    if (!inspection.web_contents) {
      std::move(inspection.callback)
          .Run(MakeSnapshotResult(
              api_mojom::PageInspectorAvailability::kNoSelectedPage));
      continue;
    }
    BeginPageInspectorPolicyEvaluation(inspection.web_contents.get(),
                                       std::move(inspection.callback));
  }
}

void CoreServiceManager::ResolvePendingPageInspectionsUnavailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<PendingPageInspection> pending =
      std::move(pending_page_inspections_);
  pending_page_inspections_.clear();
  RefreshIdleTeardown();
  for (PendingPageInspection& inspection : pending) {
    std::move(inspection.callback)
        .Run(MakeSnapshotResult(
            api_mojom::PageInspectorAvailability::kCoreUnavailable));
  }
  site_skill_offers_.ResolveUnavailable();
}

void CoreServiceManager::OnDirectPolicyEvaluated(
    CoreDirectObservationRequest request,
    service_mojom::PolicyEvaluationResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result ||
      result->status != service_mojom::PolicyEvaluationStatus::kGranted ||
      !result->minted_grant || !result->direct_observation_effect) {
    actor_leases_.Release(request.lease_id);
    const api_mojom::PageInspectorAvailability availability =
        result && result->status ==
                      service_mojom::PolicyEvaluationStatus::kCoreUnavailable
            ? api_mojom::PageInspectorAvailability::kCoreUnavailable
            : (result && (result->status ==
                              service_mojom::PolicyEvaluationStatus::kDenied ||
                          result->status ==
                              service_mojom::PolicyEvaluationStatus::
                                  kApprovalRequired)
                   ? api_mojom::PageInspectorAvailability::kPolicyDenied
                   : api_mojom::PageInspectorAvailability::kInvalidResponse);
    std::move(request.callback).Run(MakeSnapshotResult(availability));
    return;
  }
  const std::string effect_id = result->direct_observation_effect->effect_id;
  effect_broker_->Dispatch(
      std::move(result->direct_observation_effect),
      base::BindOnce(&CoreServiceManager::OnDirectObservationCompleted,
                     weak_factory_.GetWeakPtr(), std::move(request),
                     effect_id));
}

void CoreServiceManager::OnDirectObservationCompleted(
    CoreDirectObservationRequest request,
    std::string effect_id,
    service_mojom::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  actor_leases_.Release(request.lease_id);
  api_mojom::PageInspectorSnapshotViewPtr projection =
      page_observation_broker_->TakeDirectProjection(effect_id);
  if (result && result->status == service_mojom::EffectStatus::kCompleted &&
      result->observation && !result->observation->media && projection &&
      request.web_contents) {
    TaffyPageIntelligenceHost* host =
        TaffyPageIntelligenceHost::FromWebContents(
            request.web_contents.get());
    const std::optional<DirectObservationContext> live =
        host ? host->BuildDirectObservationContext() : std::nullopt;
    const service_mojom::ObservationEffectResult& observed =
        *result->observation;
    if (!live || !SameDocument(*live, request.identity) ||
        live->graph_revision != observed.graph_revision ||
        (request.identity.graph_revision != 0u &&
         request.identity.graph_revision != observed.graph_revision)) {
      std::move(request.callback).Run(MakeSnapshotResult(
          api_mojom::PageInspectorAvailability::kStaleDocument));
      return;
    }
    if (!session_.is_bound() ||
        site_skill_offers_.pending_count() >=
            service_mojom::kMaxInFlightPerProfile) {
      projection->site_skill_offer_availability =
          api_mojom::SiteSkillOfferAvailability::kCoreUnavailable;
      auto response =
          MakeSnapshotResult(api_mojom::PageInspectorAvailability::kAvailable);
      response->snapshot = std::move(projection);
      std::move(request.callback).Run(std::move(response));
      return;
    }
    const std::string nonce =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    const std::string operation_id = "site-skill-match-" + nonce;
    auto operation = service_mojom::OperationEnvelope::New(
        operation_id, service_generation_, 0u,
        NowMonotonicMillis() + service_mojom::kMaxDirectObservationDeadlineMs,
        operation_id);
    request.identity.graph_revision = observed.graph_revision;
    auto command = service_mojom::SiteSkillMatchCommand::New();
    command->operation = operation.Clone();
    command->expected_tab_id = request.identity.tab_id;
    command->expected_frame_id = request.identity.frame_id;
    command->expected_page_epoch = request.identity.page_epoch;
    command->expected_graph_revision = request.identity.graph_revision;
    command->expected_origin = request.identity.origin;
    command->observation = std::move(result->observation);
    auto match_callback = site_skill_offers_.TrackMatch(
        service_generation_, operation_id, std::move(request.web_contents),
        std::move(request.identity), std::move(operation),
        std::move(projection), std::move(request.callback));
    session_->MatchSiteSkills(
        std::move(command), std::move(match_callback));
    return;
  }
  const api_mojom::PageInspectorAvailability availability =
      !result ? api_mojom::PageInspectorAvailability::kInvalidResponse
      : result->status == service_mojom::EffectStatus::kUnavailable
          ? api_mojom::PageInspectorAvailability::kCoreUnavailable
      : result->status == service_mojom::EffectStatus::kResourceLimit
          ? api_mojom::PageInspectorAvailability::kBackpressure
      : result->status == service_mojom::EffectStatus::kDenied ||
              result->status == service_mojom::EffectStatus::kCancelled ||
              result->status == service_mojom::EffectStatus::kDeadlineExceeded
          ? api_mojom::PageInspectorAvailability::kStaleDocument
          : api_mojom::PageInspectorAvailability::kInvalidResponse;
  std::move(request.callback).Run(MakeSnapshotResult(availability));
}

}  // namespace taffy
