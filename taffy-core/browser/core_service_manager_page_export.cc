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

#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
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

namespace api = core_api::mojom;
namespace service = core_service::mojom;

uint64_t ExportNowMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

api::PageSnapshotExportResultPtr ExportResult(
    api::PageSnapshotExportAvailability availability) {
  auto result = api::PageSnapshotExportResult::New();
  result->availability = availability;
  return result;
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

void AppendIdentityField(std::string_view value, std::string* canonical) {
  canonical->append(base::NumberToString(value.size()));
  canonical->push_back(':');
  canonical->append(value);
  canonical->push_back('|');
}

std::string PageExportIdempotencyKey(std::string_view browser_session_id,
                                     std::string_view request_id) {
  std::string canonical;
  AppendIdentityField("TAFFY_PAGE_SNAPSHOT_EXPORT_REPLAY_V1", &canonical);
  AppendIdentityField(browser_session_id, &canonical);
  AppendIdentityField(request_id, &canonical);
  const std::string digest = base::ToLowerASCII(
      base::HexEncode(crypto::SHA256HashString(canonical)));
  return "page-export-replay-v1-" + digest;
}

}  // namespace

void CoreServiceManager::ExportPageSnapshot(
    content::WebContents* web_contents,
    const std::string& request_id,
    const std::string& document_id,
    api::PageSnapshotExportFormat format,
    CoreServicePageExportCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!web_contents || web_contents->GetBrowserContext() != browser_context_ ||
      request_id.empty() || request_id.size() > api::kMaxIdentifierBytes ||
      document_id != "selected-page") {
    std::move(callback).Run(ExportResult(
        web_contents ? api::PageSnapshotExportAvailability::kInvalidResponse
                     : api::PageSnapshotExportAvailability::kNoSelectedPage));
    return;
  }
  if (private_profile_) {
    std::move(callback).Run(
        ExportResult(api::PageSnapshotExportAvailability::kPrivateProfile));
    return;
  }
  if (active_page_export_requests_.contains(request_id)) {
    std::move(callback).Run(
        ExportResult(api::PageSnapshotExportAvailability::kReplayConflict));
    return;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const std::optional<DirectObservationContext> context =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!context ||
      pending_page_exports_.size() >= service::kMaxInFlightPerProfile) {
    std::move(callback).Run(ExportResult(
        context ? api::PageSnapshotExportAvailability::kBackpressure
                : api::PageSnapshotExportAvailability::kDocumentUnavailable));
    return;
  }
  const std::string nonce = base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto operation = service::OperationEnvelope::New();
  operation->operation_id = "page-snapshot-export-" + nonce;
  operation->service_generation = service_generation_;
  operation->deadline_monotonic_ms = ExportNowMillis() + 5'000u;
  operation->idempotency_key =
      PageExportIdempotencyKey(browser_session_id_, request_id);
  const std::string operation_id = operation->operation_id;
  PendingPageExport pending;
  pending.web_contents = web_contents->GetWeakPtr();
  pending.request_id = request_id;
  pending.document_id = document_id;
  pending.format = format;
  pending.identity = Identity(*context);
  pending.operation = std::move(operation);
  pending.callback = std::move(callback);
  const auto [pending_position, pending_inserted] =
      pending_page_exports_.emplace(operation_id, std::move(pending));
  CHECK(pending_inserted);
  const auto [request_position, request_inserted] =
      active_page_export_requests_.emplace(request_id, operation_id);
  CHECK(request_inserted);
  static_cast<void>(pending_position);
  static_cast<void>(request_position);
  RefreshIdleTeardown();
  PrepareForCoreApi(
      base::BindOnce(&CoreServiceManager::OnPageExportCorePrepared,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void CoreServiceManager::OnPageExportCorePrepared(std::string operation_id,
                                                  bool ready) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!pending_page_exports_.contains(operation_id)) {
    return;
  }
  if (!ready) {
    FinishPageExport(
        operation_id,
        ExportResult(api::PageSnapshotExportAvailability::kCoreUnavailable));
    return;
  }
  BeginPageExportPolicyEvaluation(operation_id);
}

void CoreServiceManager::BeginPageExportPolicyEvaluation(
    const std::string& operation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto pending = pending_page_exports_.find(operation_id);
  if (pending == pending_page_exports_.end() || !pending->second.web_contents) {
    FinishPageExport(
        operation_id,
        ExportResult(api::PageSnapshotExportAvailability::kNoSelectedPage));
    return;
  }
  TaffyPageIntelligenceHost* host = TaffyPageIntelligenceHost::FromWebContents(
      pending->second.web_contents.get());
  const std::optional<DirectObservationContext> context =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!context || !SameDocument(*context, pending->second.identity)) {
    FinishPageExport(
        operation_id,
        ExportResult(api::PageSnapshotExportAvailability::kStaleDocument));
    return;
  }
  const std::string nonce = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const std::string intent_id = "direct-intent-" + nonce;
  const std::string policy_operation_id =
      "direct-export-observation-" + nonce;
  const uint64_t now = ExportNowMillis();
  const ActorLeaseResult lease = actor_leases_.IssueDirectObservation(
      intent_id, TabId{context->tab_id},
      static_cast<uint32_t>(service::kMaxDirectObservationLeaseMs),
      base::TimeTicks::Now());
  if (lease.code != ActorLeaseResultCode::kIssued) {
    FinishPageExport(
        operation_id,
        ExportResult(api::PageSnapshotExportAvailability::kPolicyDenied));
    return;
  }
  auto subject = service::AuthoritySubject::New(
      service::AuthoritySubjectKind::kDirectUserIntent, intent_id);
  auto request = service::PolicyEvaluationRequest::New();
  request->operation = service::OperationEnvelope::New(
      policy_operation_id, service_generation_, 0u,
      now + service::kMaxDirectObservationDeadlineMs, policy_operation_id);
  request->now_monotonic_ms = now;
  request->now_utc_ms = static_cast<uint64_t>(
      std::max<int64_t>(0, base::Time::Now().InMillisecondsSinceUnixEpoch()));
  request->principal = service::PolicyPrincipal::New();
  request->principal->kind = service::PolicyPrincipalKind::kAssistant;
  request->action_class = service::PolicyActionClass::kObservePage;
  request->proposal_digest = ComputeDirectObservationDigest(
      *context, browser_profile_id_, intent_id, policy_operation_id,
      policy_operation_id, service_generation_,
      FixedDirectObservationDigestOperands());
  request->operation_kind = service::TaskActionOperationKind::kDomRead;
  const auto digest =
      crypto::SHA256Hash(base::as_byte_span(request->proposal_digest));
  request->canonical_intent_digest.assign(digest.begin(), digest.end());
  request->scope = service::PolicyCapabilityScope::New();
  request->scope->profile_id = browser_profile_id_;
  request->scope->tab_id = context->tab_id;
  request->scope->frame_id = context->frame_id;
  request->scope->page_epoch = context->page_epoch;
  request->scope->origin = service::PolicyOrigin::New();
  request->scope->origin->kind = service::PolicyOriginKind::kTuple;
  request->scope->origin->serialization = context->origin;
  request->data_classes = {service::BipSensitivity::kNotSensitive};
  request->context_risk = service::PolicyRiskClass::kLocalRead;
  request->expires_at_monotonic_ms = std::min(
      lease.expires_at_monotonic_ms, request->operation->deadline_monotonic_ms);
  request->actor_lease = service::ActorLeaseFact::New();
  request->actor_lease->lease_id = lease.lease_id.value;
  request->actor_lease->service_generation = service_generation_;
  request->actor_lease->profile_id = browser_profile_id_;
  request->actor_lease->tab_id = context->tab_id;
  request->actor_lease->control_mode = service::TaskControlMode::kUser;
  request->actor_lease->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
  request->actor_lease->authority_subject = subject.Clone();
  request->context = service::PolicyEvaluationContext::kDirectUserObservation;
  request->authority_subject = std::move(subject);
  request->policy_version = 1u;
  EvaluatePolicy(
      std::move(request),
      base::BindOnce(&CoreServiceManager::OnPageExportPolicyEvaluated,
                     weak_factory_.GetWeakPtr(), operation_id,
                     lease.lease_id));
}

void CoreServiceManager::OnPageExportPolicyEvaluated(
    std::string operation_id,
    ActorLeaseId lease_id,
    service::PolicyEvaluationResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto pending = pending_page_exports_.find(operation_id);
  if (pending == pending_page_exports_.end()) {
    actor_leases_.Release(lease_id);
    return;
  }
  if (!result || result->status != service::PolicyEvaluationStatus::kGranted ||
      !result->minted_grant || !result->direct_observation_effect) {
    actor_leases_.Release(lease_id);
    FinishPageExport(
        operation_id,
        ExportResult(
            result && result->status ==
                          service::PolicyEvaluationStatus::kCoreUnavailable
                ? api::PageSnapshotExportAvailability::kCoreUnavailable
                : api::PageSnapshotExportAvailability::kPolicyDenied));
    return;
  }
  pending->second.effect_id = result->direct_observation_effect->effect_id;
  page_observation_broker_->DispatchForExport(
      std::move(result->direct_observation_effect),
      base::BindOnce(&CoreServiceManager::OnPageExportObservationCompleted,
                     weak_factory_.GetWeakPtr(), operation_id,
                     std::move(lease_id)));
}

}  // namespace taffy
