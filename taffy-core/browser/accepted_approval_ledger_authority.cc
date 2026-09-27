// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>

#include "base/logging.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_task_policy_destination.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

}  // namespace

bool AcceptedApprovalLedger::IsTaskSourceAuthorized(
    const mojom::TaskPolicyEffect& effect,
    const std::string& live_normalized_origin,
    uint64_t service_generation) const {
  return effect.operation &&
         IsTaskSourceAuthorized(effect.task_id, effect.tab_id,
                                *effect.operation, live_normalized_origin,
                                service_generation);
}

bool AcceptedApprovalLedger::IsTaskSourceAuthorized(
    const std::string& task_id,
    const std::string& tab_id,
    const mojom::OperationEnvelope& operation,
    const std::string& live_normalized_origin,
    uint64_t service_generation) const {
  const auto consent = accepted_consents_.find(task_id);
  if (consent == accepted_consents_.end()) {
    return false;
  }
  const ConsentRecord& record = consent->second;
  const auto source = record.sources_by_tab.find(tab_id);
  // The live document is this source when it is the same site, not only when
  // it is the byte-identical origin. `TaskSourceOriginStillNamesTheSite`
  // states the rule and why; the short version is that a site is free to
  // answer on a sibling host of its own registrable domain, and refusing the
  // read then refuses every move on a page the task was authorized to open.
  return record.submission.service_generation == service_generation &&
         operation.service_generation == service_generation &&
         operation.task_revision >= record.submission.resulting_revision &&
         source != record.sources_by_tab.end() &&
         TaskSourceOriginStillNamesTheSite(source->second.normalized_origin,
                                           live_normalized_origin);
}

bool AcceptedApprovalLedger::IsTaskTabAuthorized(
    const std::string& task_id,
    const std::string& tab_id,
    const mojom::OperationEnvelope& operation,
    uint64_t service_generation) const {
  const auto consent = accepted_consents_.find(task_id);
  if (consent == accepted_consents_.end()) {
    return false;
  }
  const ConsentRecord& record = consent->second;
  return record.submission.service_generation == service_generation &&
         operation.service_generation == service_generation &&
         operation.task_revision >= record.submission.resulting_revision &&
         record.sources_by_tab.contains(tab_id);
}

bool AcceptedApprovalLedger::IsTaskTabAuthorized(
    const mojom::TaskPolicyEffect& effect,
    uint64_t service_generation) const {
  if (!effect.operation) {
    return false;
  }
  const auto consent = accepted_consents_.find(effect.task_id);
  if (consent == accepted_consents_.end()) {
    return false;
  }
  const ConsentRecord& record = consent->second;
  return record.submission.service_generation == service_generation &&
         effect.operation->service_generation == service_generation &&
         effect.operation->task_revision >=
             record.submission.resulting_revision &&
         record.sources_by_tab.contains(effect.tab_id);
}

AcceptedApprovalLedger::TaskTabAuthorityReason
AcceptedApprovalLedger::DescribeTaskTabAuthority(
    const mojom::TaskPolicyEffect& effect,
    uint64_t service_generation) const {
  TaskTabAuthorityReason reason;
  if (!effect.operation) {
    return reason;
  }
  const auto consent = accepted_consents_.find(effect.task_id);
  if (consent == accepted_consents_.end()) {
    return reason;
  }
  const ConsentRecord& record = consent->second;
  reason.has_record = true;
  reason.generation_matches =
      record.submission.service_generation == service_generation &&
      effect.operation->service_generation == service_generation;
  reason.revision_is_current =
      effect.operation->task_revision >= record.submission.resulting_revision;
  reason.tab_is_held = record.sources_by_tab.contains(effect.tab_id);
  return reason;
}

std::optional<std::string> AcceptedApprovalLedger::FindSingleConsentedTab(
    const std::string& task_id,
    uint64_t service_generation) const {
  const auto consent = accepted_consents_.find(task_id);
  if (consent == accepted_consents_.end()) {
    return std::nullopt;
  }
  const ConsentRecord& record = consent->second;
  if (record.submission.service_generation != service_generation ||
      record.sources_by_tab.size() != 1u) {
    return std::nullopt;
  }
  return record.sources_by_tab.begin()->first;
}

mojom::TaskConsentSourcePtr AcceptedApprovalLedger::FindTaskSourceForDisplay(
    const std::string& task_id,
    const std::string& tab_id,
    uint64_t current_task_revision,
    uint64_t service_generation) const {
  const auto consent = accepted_consents_.find(task_id);
  if (consent == accepted_consents_.end() ||
      consent->second.submission.service_generation != service_generation ||
      current_task_revision < consent->second.submission.resulting_revision) {
    return nullptr;
  }
  const auto source = consent->second.sources_by_tab.find(tab_id);
  if (source == consent->second.sources_by_tab.end()) {
    return nullptr;
  }
  return mojom::TaskConsentSource::New(source->second.source_id, tab_id,
                                       source->second.normalized_origin,
                                       source->second.canonical_locator);
}

bool AcceptedApprovalLedger::ConsumeExactApproval(
    const mojom::TaskPolicyEffect& effect,
    uint64_t service_generation,
    const std::string& browser_session_id,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms) {
  ApprovalKey key(effect.task_id, effect.action_id);
  auto found = committed_approvals_.find(key);
  if (!effect.approval) {
    return found == committed_approvals_.end() &&
           !staged_approvals_.contains(key);
  }
  if (!effect.operation) {
    LOG(ERROR) << "[taffy_approval_consumption_refused] "
                  "reason=missing-operation";
    return false;
  }
  if (found == committed_approvals_.end()) {
    LOG(ERROR) << "[taffy_approval_consumption_refused] "
                  "reason=missing-committed-record staged="
               << staged_approvals_.contains(key);
    return false;
  }

  const ApprovalRecord& record = found->second;
  if (record.expires_at_monotonic_ms <= now_monotonic_ms ||
      record.expires_at_utc_ms <= now_utc_ms ||
      record.browser_session_id != browser_session_id) {
    LOG(ERROR) << "[taffy_approval_consumption_refused] "
                  "reason=expired-or-session-mismatch monotonic_expired="
               << (record.expires_at_monotonic_ms <= now_monotonic_ms)
               << " utc_expired=" << (record.expires_at_utc_ms <= now_utc_ms)
               << " session_mismatch="
               << (record.browser_session_id != browser_session_id);
    committed_approvals_.erase(found);
    return false;
  }
  const bool exact =
      record.submission.service_generation == service_generation &&
      effect.operation->service_generation == service_generation &&
      effect.operation->task_revision == record.submission.resulting_revision &&
      effect.task_id == record.submission.task_id &&
      effect.action_id == record.action_id &&
      effect.proposal_digest == record.proposal_digest &&
      effect.approval->receipt_reference == record.receipt_reference &&
      effect.approval->proposal_digest == record.proposal_digest &&
      effect.approval->service_generation ==
          record.submission.service_generation &&
      effect.approval->expires_at_monotonic_ms ==
          record.expires_at_monotonic_ms &&
      effect.approval->expires_at_utc_ms == record.expires_at_utc_ms &&
      effect.approval->browser_session_id == record.browser_session_id;
  if (exact) {
    committed_approvals_.erase(found);
  } else {
    LOG(ERROR)
        << "[taffy_approval_consumption_refused] "
           "reason=committed-tuple-mismatch generation="
        << (record.submission.service_generation == service_generation &&
            effect.operation->service_generation == service_generation)
        << " revision="
        << (effect.operation->task_revision ==
            record.submission.resulting_revision)
        << " task=" << (effect.task_id == record.submission.task_id)
        << " action=" << (effect.action_id == record.action_id)
        << " proposal=" << (effect.proposal_digest == record.proposal_digest)
        << " receipt="
        << (effect.approval->receipt_reference == record.receipt_reference)
        << " approval_proposal="
        << (effect.approval->proposal_digest == record.proposal_digest)
        << " approval_generation="
        << (effect.approval->service_generation ==
            record.submission.service_generation)
        << " monotonic_expiry="
        << (effect.approval->expires_at_monotonic_ms ==
            record.expires_at_monotonic_ms)
        << " utc_expiry="
        << (effect.approval->expires_at_utc_ms == record.expires_at_utc_ms)
        << " approval_session="
        << (effect.approval->browser_session_id == record.browser_session_id);
  }
  return exact;
}

void AcceptedApprovalLedger::RevokeAction(const std::string& task_id,
                                          const std::string& action_id) {
  ApprovalKey key(task_id, action_id);
  staged_approvals_.erase(key);
  committed_approvals_.erase(key);
  generation_loss_approval_candidates_.erase(key);
}

void AcceptedApprovalLedger::RevokeTask(const std::string& task_id,
                                        uint64_t service_generation) {
  RevokeTaskApprovals(task_id, service_generation);

  const auto staged = staged_consents_.find(task_id);
  if (staged != staged_consents_.end() &&
      staged->second.submission.service_generation == service_generation) {
    staged_consents_.erase(staged);
  }
  const auto accepted = accepted_consents_.find(task_id);
  if (accepted != accepted_consents_.end() &&
      accepted->second.submission.service_generation == service_generation) {
    accepted_consents_.erase(accepted);
  }
  generation_loss_consent_candidates_.erase(task_id);
  suspended_consents_.erase(task_id);
}

void AcceptedApprovalLedger::RevokeTaskApprovals(const std::string& task_id,
                                                 uint64_t service_generation) {
  auto revoke_approvals = [&task_id, service_generation](auto* records) {
    for (auto it = records->begin(); it != records->end();) {
      if (it->second.submission.task_id == task_id &&
          it->second.submission.service_generation == service_generation) {
        it = records->erase(it);
      } else {
        ++it;
      }
    }
  };
  revoke_approvals(&staged_approvals_);
  revoke_approvals(&committed_approvals_);
  for (auto it = generation_loss_approval_candidates_.begin();
       it != generation_loss_approval_candidates_.end();) {
    if (it->second.submission.task_id == task_id) {
      it = generation_loss_approval_candidates_.erase(it);
    } else {
      ++it;
    }
  }
}

void AcceptedApprovalLedger::ApplyTaskSettlement(
    const std::string& task_id,
    uint64_t service_generation,
    mojom::TaskSettlementKind kind) {
  if (kind != mojom::TaskSettlementKind::kPause) {
    RevokeTask(task_id, service_generation);
    return;
  }

  // A one-use action approval never survives a pause. Standing task consent
  // does, but only as an inert copy of browser-owned durable evidence.
  RevokeTaskApprovals(task_id, service_generation);
  const auto active = accepted_consents_.find(task_id);
  const auto staged = staged_consents_.find(task_id);
  const auto retained = generation_loss_consent_candidates_.find(task_id);
  const bool active_candidate =
      active != accepted_consents_.end() &&
      active->second.submission.service_generation == service_generation;
  const bool staged_candidate =
      staged != staged_consents_.end() &&
      staged->second.submission.service_generation == service_generation;
  const bool retained_candidate =
      retained != generation_loss_consent_candidates_.end() &&
      retained->second.submission.service_generation < service_generation;
  const size_t candidate_count = static_cast<size_t>(active_candidate) +
                                 static_cast<size_t>(staged_candidate) +
                                 static_cast<size_t>(retained_candidate);
  auto suspended = suspended_consents_.find(task_id);

  // A repeated projection of the same pause only keeps the already-suspended
  // proof inert. A second live candidate is an ownership contradiction, so no
  // version of it may survive.
  if (suspended != suspended_consents_.end()) {
    suspended->second.resume_submission.reset();
    if (candidate_count != 0u) {
      suspended_consents_.erase(suspended);
    }
    staged_consents_.erase(task_id);
    accepted_consents_.erase(task_id);
    generation_loss_consent_candidates_.erase(task_id);
    return;
  }

  std::optional<ConsentRecord> candidate;
  if (candidate_count == 1u) {
    if (active_candidate) {
      candidate = std::move(active->second);
    } else if (staged_candidate) {
      candidate = std::move(staged->second);
    } else {
      candidate = std::move(retained->second);
    }
  }
  staged_consents_.erase(task_id);
  accepted_consents_.erase(task_id);
  generation_loss_consent_candidates_.erase(task_id);

  const bool durable = candidate && candidate->submission.storage_bound &&
                       candidate->submission.storage_committed &&
                       candidate->submission.resulting_revision >
                           candidate->submission.submitted_revision;
  if (durable) {
    candidate->pending_replacements.clear();
    suspended_consents_.emplace(
        task_id, SuspendedConsentRecord{std::move(*candidate), std::nullopt});
  }
}

}  // namespace taffy
