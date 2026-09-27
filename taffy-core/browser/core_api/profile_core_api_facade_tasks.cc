// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_api/task_workflow_tools.h"

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void ProfileCoreApiFacade::StartTask(
    const std::string& goal,
    core_api::mojom::TaskTemplateId template_id,
    const std::optional<std::string>& workspace_id,
    core_api::mojom::TaskConsentPreviewPtr consent_preview,
    const std::optional<std::string>& skill_offer_id,
    StartTaskCallback callback) {
  if (!manager_) {
    LOG(WARNING) << "[taffy_start_refused] at=facade/no-manager";
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  manager_->PrepareForCoreApi(base::BindOnce(
      &ProfileCoreApiFacade::OnPreparedStartTask, weak_factory_.GetWeakPtr(),
      goal, template_id, workspace_id, std::move(consent_preview),
      skill_offer_id, std::move(callback)));
}

void ProfileCoreApiFacade::OnPreparedStartTask(
    std::string goal,
    core_api::mojom::TaskTemplateId template_id,
    std::optional<std::string> workspace_id,
    core_api::mojom::TaskConsentPreviewPtr consent_preview,
    std::optional<std::string> skill_offer_id,
    StartTaskCallback callback,
    bool ready) {
  if (!ready || !manager_) {
    LOG(WARNING) << "[taffy_start_refused] at=facade/not-ready ready=" << ready;
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  // Named, like the consent and build clauses this start goes on to meet. A
  // clause with no line is a clause that cannot be told from the others.
  if (!consent_preview) {
    LOG(WARNING) << "[taffy_start_refused] at=facade/consent-preview";
    std::move(callback).Run(SubmissionStatus::kInvalidRequest);
    return;
  }
  // What the person is told when the consent is refused. It used to be
  // `kInvalidRequest` for every clause — "Taffy could not read this request"
  // on the phone even when every open tab was one Taffy had opened — so a
  // refusal about the person's tabs or windows now says so (decision 0231).
  SubmissionStatus verdict = SubmissionStatus::kInvalidRequest;
  std::optional<core_service::mojom::TaskConsentPreviewPtr> resolved =
      manager_->ResolveStartTaskConsent(template_id, *consent_preview,
                                        skill_offer_id, &verdict);
  if (!resolved || !*resolved) {
    // The registry and the guard above it name every clause they refuse on,
    // so there is nothing left for this branch to add.
    std::move(callback).Run(verdict);
    return;
  }
  std::optional<std::string> skill_version_id;
  if (skill_offer_id) {
    skill_version_id = manager_->site_skill_offers().ResolveForStart(
        *skill_offer_id, template_id, **resolved);
    if (!skill_version_id) {
      LOG(WARNING) << "[taffy_start_refused] at=facade/skill-offer";
      std::move(callback).Run(SubmissionStatus::kInvalidRequest);
      return;
    }
  }
  CoreApiCommandFactory factory = NewFactory();
  std::vector<std::string> tools =
      WorkflowToolsForStart(template_id, consent_preview->provider_route,
                            consent_preview->attached_stores, skill_version_id);
  SubmitProjected(factory.BuildStartTask(
                      std::move(goal), template_id, std::move(workspace_id),
                      std::move(consent_preview), std::move(*resolved),
                      std::move(tools), manager_->browser_session_id(),
                      manager_->service_generation(), NowMonotonicMillis(),
                      std::move(skill_offer_id), std::move(skill_version_id)),
                  std::move(callback));
}

void ProfileCoreApiFacade::CancelTask(const std::string& task_id,
                                      CancelTaskCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<TaskControlLookup> control = manager_->FindTaskControl(
      task_id, core_service::mojom::TaskControlKind::kStop);
  if (!control) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildCancelTask(task_id, control->task_revision,
                                          control->service_generation,
                                          NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::PauseTask(const std::string& task_id,
                                     PauseTaskCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<TaskControlLookup> control = manager_->FindTaskControl(
      task_id, core_service::mojom::TaskControlKind::kPause);
  if (!control) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildPauseTask(task_id, control->task_revision,
                             control->service_generation, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::ResumeTask(const std::string& task_id,
                                      ResumeTaskCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<TaskControlLookup> control = manager_->FindTaskControl(
      task_id, core_service::mojom::TaskControlKind::kResume);
  if (!control) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildResumeTask(task_id, control->task_revision,
                                          control->service_generation,
                                          NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::TakeOver(const std::string& task_id,
                                    TakeOverCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<TaskControlLookup> control = manager_->FindTaskControl(
      task_id, core_service::mojom::TaskControlKind::kTakeOver);
  if (!control) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildTakeOver(task_id, control->task_revision,
                            control->service_generation, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::CompleteHandover(const std::string& task_id,
                                            CompleteHandoverCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<uint64_t> revision = manager_->FindTaskRevision(task_id);
  const std::optional<HandoverCompletionFacts> facts =
      manager_->CloseHandoverForCompletion(task_id);
  if (!revision || !facts) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildCompleteHandover(
                      task_id, facts->handover_id, facts->lease_before,
                      facts->resumed_with, facts->person_input, *revision,
                      manager_->service_generation(), NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::SupplyUserInput(const std::string& task_id,
                                           const std::string& answer,
                                           SupplyUserInputCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<uint64_t> revision = manager_->FindTaskRevision(task_id);
  if (!revision) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildSupplyUserInput(task_id, answer, *revision,
                                               manager_->service_generation(),
                                               NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::FollowUp(const std::string& task_id,
                                    const std::string& question,
                                    FollowUpCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<uint64_t> revision = manager_->FindTaskRevision(task_id);
  if (!revision) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildFollowUp(task_id, question, *revision,
                                        manager_->service_generation(),
                                        NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::ApproveAction(const std::string& task_id,
                                         const std::string& action_id,
                                         ApproveActionCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<PendingApprovalLookup> approval =
      manager_->FindPendingApproval(task_id, action_id);
  if (!approval) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildApproveAction(
          task_id, action_id, approval->proposal_digest,
          approval->task_revision, approval->service_generation,
          NowMonotonicMillis(), NowUtcMillis(), manager_->browser_session_id()),
      std::move(callback));
}

void ProfileCoreApiFacade::DeliverPermissionResult(
    const std::string& request_id,
    core_api::mojom::PlatformPermission permission,
    core_api::mojom::PermissionDecision decision,
    DeliverPermissionResultCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<PendingPermissionLookup> pending =
      manager_->FindPendingPermission(request_id);
  if (!pending || pending->api_permission != permission) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildPermissionResult(
                      pending->task_id, request_id, permission, decision,
                      pending->task_revision, pending->service_generation,
                      NowMonotonicMillis()),
                  std::move(callback));
}

}  // namespace taffy
