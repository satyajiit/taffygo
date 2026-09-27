// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/accepted_approval_ledger_shapes.h"
#include "taffy/browser/core_state_binding_registry.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using accepted_approval_ledger_shapes::IsIdentifier;

}  // namespace

AuthoritySubmissionStage AcceptedApprovalLedger::StageResumeCommand(
    SubmissionRecord submission,
    const mojom::ResumeTaskCommand& resume,
    const std::string& browser_session_id,
    const CoreStateBindingRegistry& bindings,
    uint64_t now_monotonic_ms) {
  const std::optional<TaskControlLookup> control =
      bindings.FindTaskControl(resume.task_id, mojom::TaskControlKind::kResume);
  auto suspended = suspended_consents_.find(resume.task_id);
  if (suspended != suspended_consents_.end() &&
      suspended->second.resume_submission &&
      !suspended->second.resume_submission->storage_committed &&
      suspended->second.resume_submission->deadline_monotonic_ms <=
          now_monotonic_ms) {
    suspended->second.resume_submission.reset();
  }
  if (submission.submitted_revision == 0u || !IsIdentifier(resume.task_id) ||
      !control ||
      control->service_generation != submission.service_generation ||
      control->task_revision != submission.submitted_revision ||
      suspended == suspended_consents_.end() ||
      suspended->second.resume_submission ||
      suspended->second.consent.browser_session_id != browser_session_id ||
      !suspended->second.consent.submission.storage_bound ||
      !suspended->second.consent.submission.storage_committed ||
      suspended->second.consent.submission.resulting_revision >
          submission.submitted_revision) {
    last_stage_refusal_ = "resume";
    return AuthoritySubmissionStage::kInvalid;
  }
  submission.task_id = resume.task_id;
  suspended->second.resume_submission = std::move(submission);
  return AuthoritySubmissionStage::kStaged;
}

}  // namespace taffy
