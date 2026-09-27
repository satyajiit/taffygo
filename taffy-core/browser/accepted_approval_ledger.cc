// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/accepted_approval_ledger.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "taffy/browser/accepted_approval_ledger_shapes.h"
#include "taffy/browser/core_api/task_consent_shape.h"
#include "taffy/browser/core_state_binding_registry.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using accepted_approval_ledger_shapes::IsIdentifier;
using accepted_approval_ledger_shapes::IsSha256Digest;


bool IsValidOperation(const mojom::OperationEnvelope* operation,
                      uint64_t now_monotonic_ms) {
  return operation && operation->service_generation != 0u &&
         !operation->operation_id.empty() &&
         operation->operation_id.size() <= mojom::kMaxOperationIdBytes &&
         !operation->idempotency_key.empty() &&
         operation->idempotency_key.size() <= mojom::kMaxIdempotencyKeyBytes &&
         operation->deadline_monotonic_ms > now_monotonic_ms;
}

bool OperationMatches(const mojom::OperationEnvelope* operation,
                      const std::string& operation_id,
                      const std::string& idempotency_key,
                      uint64_t service_generation,
                      uint64_t task_revision,
                      uint64_t deadline_monotonic_ms) {
  return operation && operation->operation_id == operation_id &&
         operation->idempotency_key == idempotency_key &&
         operation->service_generation == service_generation &&
         operation->task_revision == task_revision &&
         operation->deadline_monotonic_ms == deadline_monotonic_ms;
}

}  // namespace

AcceptedApprovalLedger::AcceptedApprovalLedger() = default;
AcceptedApprovalLedger::~AcceptedApprovalLedger() = default;

AuthoritySubmissionStage AcceptedApprovalLedger::StageSubmittedCommand(
    const mojom::CoreServiceCommand& command,
    const std::string& browser_profile_id,
    const std::string& browser_session_id,
    const CoreStateBindingRegistry& bindings,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms) {
  // Rewritten on every call, so the label can never belong to an older one.
  last_stage_refusal_ = "unnamed";
  const bool is_user_decision =
      command.kind == mojom::CoreServiceCommandKind::kUserDecision;
  const bool is_start_task =
      command.kind == mojom::CoreServiceCommandKind::kStartTask;
  const bool is_resume_task =
      command.kind == mojom::CoreServiceCommandKind::kResumeTask;
  if (!is_user_decision && !is_start_task && !is_resume_task) {
    return AuthoritySubmissionStage::kNotApplicable;
  }
  if ((is_user_decision && !command.user_decision) ||
      (is_start_task && !command.start_task) ||
      (is_resume_task && !command.resume_task)) {
    last_stage_refusal_ = "body-missing";
    return AuthoritySubmissionStage::kInvalid;
  }
  const bool accepted_action =
      is_user_decision &&
      command.user_decision->decision == mojom::UserDecisionKind::kAccept;
  if (is_user_decision && !accepted_action) {
    return AuthoritySubmissionStage::kNotApplicable;
  }
  // Named, one clause per rule, for the reason the whole file exists: this is
  // standing authority, so every rule here is deliberate and each is a
  // different fault. They answered one verdict between them and left nothing
  // in the log, which made a start that could never succeed indistinguishable
  // from one the ordered core had refused.
  if (!IsValidOperation(command.operation.get(), now_monotonic_ms)) {
    last_stage_refusal_ = "operation";
    return AuthoritySubmissionStage::kInvalid;
  }
  if (bindings.service_generation() != command.operation->service_generation) {
    last_stage_refusal_ = "operation-generation";
    return AuthoritySubmissionStage::kInvalid;
  }
  if (!OperationIsUnique(command.operation->operation_id)) {
    last_stage_refusal_ = "operation-not-unique";
    return AuthoritySubmissionStage::kInvalid;
  }

  SubmissionRecord submission;
  submission.operation_id = command.operation->operation_id;
  submission.idempotency_key = command.operation->idempotency_key;
  submission.service_generation = command.operation->service_generation;
  submission.submitted_revision = command.operation->task_revision;
  submission.deadline_monotonic_ms = command.operation->deadline_monotonic_ms;

  if (is_resume_task) {
    const mojom::ResumeTaskCommand& resume = *command.resume_task;
    return StageResumeCommand(std::move(submission), resume, browser_session_id,
                              bindings, now_monotonic_ms);
  }

  if (is_start_task) {
    return StageStartCommand(std::move(submission), *command.start_task,
                             *command.operation, browser_profile_id,
                             browser_session_id, bindings);
  }

  const mojom::UserDecisionCommand& decision = *command.user_decision;
  const std::optional<PendingApprovalLookup> pending =
      bindings.FindPendingApproval(decision.task_id, decision.action_id);
  ApprovalKey key(decision.task_id, decision.action_id);
  if (command.operation->task_revision == 0u ||
      !IsIdentifier(decision.task_id) || !IsIdentifier(decision.action_id) ||
      !IsIdentifier(decision.approval_receipt_id) ||
      !IsSha256Digest(decision.approval_digest) ||
      decision.approval_expires_at_monotonic_ms <= now_monotonic_ms ||
      decision.approval_expires_at_monotonic_ms >
          command.operation->deadline_monotonic_ms ||
      decision.approval_expires_at_utc_ms <= now_utc_ms ||
      !IsIdentifier(decision.browser_session_id) ||
      decision.browser_session_id != browser_session_id || !pending ||
      pending->service_generation != command.operation->service_generation ||
      pending->task_revision != command.operation->task_revision ||
      pending->proposal_digest != decision.approval_digest ||
      staged_approvals_.contains(key) || committed_approvals_.contains(key) ||
      generation_loss_approval_candidates_.contains(key) ||
      staged_approvals_.size() + committed_approvals_.size() +
              generation_loss_approval_candidates_.size() >=
          mojom::kMaxPendingApprovalsPerProfile ||
      !ReceiptIsUnique(decision.approval_receipt_id)) {
    last_stage_refusal_ = "decision";
    return AuthoritySubmissionStage::kInvalid;
  }

  ApprovalRecord record;
  submission.task_id = decision.task_id;
  record.submission = std::move(submission);
  record.action_id = decision.action_id;
  record.receipt_reference = decision.approval_receipt_id;
  record.proposal_digest = decision.approval_digest;
  record.expires_at_monotonic_ms = decision.approval_expires_at_monotonic_ms;
  record.expires_at_utc_ms = decision.approval_expires_at_utc_ms;
  record.browser_session_id = decision.browser_session_id;
  staged_approvals_.emplace(std::move(key), std::move(record));
  return AuthoritySubmissionStage::kStaged;
}

AuthorityStorageBinding AcceptedApprovalLedger::BindStorageCommit(
    const mojom::EffectEnvelope& effect,
    uint64_t now_monotonic_ms) {
  if (!effect.operation) {
    return AuthorityStorageBinding::kNotTracked;
  }
  SubmissionRecord* submission =
      FindStagedSubmission(effect.operation->operation_id);
  if (!submission) {
    return AuthorityStorageBinding::kNotTracked;
  }
  // A submitted command names the task revision it read. Its storage effect
  // names the revision the accepted mutation produced. Bind both halves:
  // expected_revision to the submitted state below, and the effect envelope
  // to resulting_revision here.
  const bool exact =
      effect.kind == mojom::EffectKind::kStorageCommit &&
      effect.retry_class == mojom::RetryClass::kIdempotent &&
      effect.storage_commit && !submission->storage_bound &&
      submission->deadline_monotonic_ms > now_monotonic_ms &&
      OperationMatches(effect.operation.get(), submission->operation_id,
                       submission->idempotency_key,
                       submission->service_generation,
                       effect.storage_commit->resulting_revision,
                       submission->deadline_monotonic_ms) &&
      effect.effect_id == submission->operation_id &&
      effect.storage_commit->operation_kind ==
          mojom::StorageOperation::kAppendTaskCommit &&
      effect.storage_commit->task_id == submission->task_id &&
      effect.storage_commit->expected_revision ==
          submission->submitted_revision &&
      effect.storage_commit->resulting_revision >
          submission->submitted_revision;
  if (!exact) {
    return AuthorityStorageBinding::kInvalid;
  }
  submission->storage_bound = true;
  submission->resulting_revision = effect.storage_commit->resulting_revision;
  return AuthorityStorageBinding::kBound;
}

AuthorityStorageCompletion AcceptedApprovalLedger::RecordStorageCompletion(
    const mojom::EffectResult& result) {
  if (!result.operation) {
    return AuthorityStorageCompletion::kNotTracked;
  }
  SubmissionRecord* submission =
      FindStagedSubmission(result.operation->operation_id);
  if (!submission) {
    return AuthorityStorageCompletion::kNotTracked;
  }
  const bool exact =
      submission->storage_bound && !submission->storage_committed &&
      result.kind == mojom::EffectKind::kStorageCommit && result.storage &&
      result.status == mojom::EffectStatus::kCompleted &&
      OperationMatches(
          result.operation.get(), submission->operation_id,
          submission->idempotency_key, submission->service_generation,
          submission->resulting_revision, submission->deadline_monotonic_ms) &&
      result.effect_id == submission->operation_id &&
      result.storage->committed_revision == submission->resulting_revision;
  if (!exact) {
    const std::string operation_id = submission->operation_id;
    EraseStagedSubmission(operation_id);
    return AuthorityStorageCompletion::kRejected;
  }
  submission->storage_committed = true;
  return AuthorityStorageCompletion::kCommitted;
}

void AcceptedApprovalLedger::RecordAdmission(const std::string& operation_id,
                                             mojom::AdmissionStatus status) {
  if (status == mojom::AdmissionStatus::kAccepted) {
    return;
  }
  SubmissionRecord* submission = FindStagedSubmission(operation_id);
  if (submission && !submission->storage_committed) {
    EraseStagedSubmission(operation_id);
  }
}

AcceptedApprovalLedger::SubmissionRecord*
AcceptedApprovalLedger::FindStagedSubmission(const std::string& operation_id) {
  for (auto& [key, record] : staged_approvals_) {
    if (record.submission.operation_id == operation_id) {
      return &record.submission;
    }
  }
  for (auto& [task_id, record] : staged_consents_) {
    if (record.submission.operation_id == operation_id) {
      return &record.submission;
    }
  }
  for (auto& entry : suspended_consents_) {
    SuspendedConsentRecord& record = entry.second;
    if (record.resume_submission &&
        record.resume_submission->operation_id == operation_id) {
      return &*record.resume_submission;
    }
  }
  return nullptr;
}

void AcceptedApprovalLedger::EraseStagedSubmission(
    const std::string& operation_id) {
  for (auto it = staged_approvals_.begin(); it != staged_approvals_.end();
       ++it) {
    if (it->second.submission.operation_id == operation_id) {
      staged_approvals_.erase(it);
      return;
    }
  }
  for (auto it = staged_consents_.begin(); it != staged_consents_.end(); ++it) {
    if (it->second.submission.operation_id == operation_id) {
      staged_consents_.erase(it);
      return;
    }
  }
  for (auto& entry : suspended_consents_) {
    SuspendedConsentRecord& record = entry.second;
    if (record.resume_submission &&
        record.resume_submission->operation_id == operation_id) {
      record.resume_submission.reset();
      return;
    }
  }
}

bool AcceptedApprovalLedger::ReceiptIsUnique(
    const std::string& receipt_reference) const {
  const auto approval_has_receipt = [&receipt_reference](const auto& entry) {
    return entry.second.receipt_reference == receipt_reference;
  };
  const auto consent_has_receipt = [&receipt_reference](const auto& entry) {
    return entry.second.receipt_reference == receipt_reference;
  };
  return std::none_of(staged_approvals_.begin(), staged_approvals_.end(),
                      approval_has_receipt) &&
         std::none_of(committed_approvals_.begin(), committed_approvals_.end(),
                      approval_has_receipt) &&
         std::none_of(generation_loss_approval_candidates_.begin(),
                      generation_loss_approval_candidates_.end(),
                      approval_has_receipt) &&
         std::none_of(staged_consents_.begin(), staged_consents_.end(),
                      consent_has_receipt) &&
         std::none_of(accepted_consents_.begin(), accepted_consents_.end(),
                      consent_has_receipt) &&
         std::none_of(generation_loss_consent_candidates_.begin(),
                      generation_loss_consent_candidates_.end(),
                      consent_has_receipt) &&
         std::none_of(suspended_consents_.begin(), suspended_consents_.end(),
                      [&receipt_reference](const auto& entry) {
                        return entry.second.consent.receipt_reference ==
                               receipt_reference;
                      });
}

bool AcceptedApprovalLedger::OperationIsUnique(
    const std::string& operation_id) const {
  const auto approval_has_operation = [&operation_id](const auto& entry) {
    return entry.second.submission.operation_id == operation_id;
  };
  const auto consent_has_operation = [&operation_id](const auto& entry) {
    return entry.second.submission.operation_id == operation_id;
  };
  return std::none_of(staged_approvals_.begin(), staged_approvals_.end(),
                      approval_has_operation) &&
         std::none_of(committed_approvals_.begin(), committed_approvals_.end(),
                      approval_has_operation) &&
         std::none_of(generation_loss_approval_candidates_.begin(),
                      generation_loss_approval_candidates_.end(),
                      approval_has_operation) &&
         std::none_of(staged_consents_.begin(), staged_consents_.end(),
                      consent_has_operation) &&
         std::none_of(accepted_consents_.begin(), accepted_consents_.end(),
                      consent_has_operation) &&
         std::none_of(generation_loss_consent_candidates_.begin(),
                      generation_loss_consent_candidates_.end(),
                      consent_has_operation) &&
         std::none_of(
             suspended_consents_.begin(), suspended_consents_.end(),
             [&operation_id](const auto& entry) {
               const SuspendedConsentRecord& record = entry.second;
               return record.consent.submission.operation_id == operation_id ||
                      (record.resume_submission &&
                       record.resume_submission->operation_id == operation_id);
             });
}

}  // namespace taffy
