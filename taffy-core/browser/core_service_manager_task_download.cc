// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/taffy_download_intent_router.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_download_manager_adapter.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

uint64_t TaskDownloadNowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

class TaskDownloadJournalCallback final : public TaskJournalAppendCallback {
 public:
  explicit TaskDownloadJournalCallback(base::OnceCallback<void(bool)> callback)
      : callback_(std::move(callback)) {}
  ~TaskDownloadJournalCallback() override = default;

  void Run(bool committed) override {
    if (callback_) {
      std::move(callback_).Run(committed);
    }
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

std::unique_ptr<TaskJournalAppendCallback> MakeTaskDownloadJournalCallback(
    base::OnceCallback<void(bool)> callback) {
  return std::make_unique<TaskDownloadJournalCallback>(std::move(callback));
}

std::optional<BrowserCommandType> TaskDownloadCommandType(
    core_mojom::TaskActionOperationKind operation) {
  switch (operation) {
    case core_mojom::TaskActionOperationKind::kDownloadStart:
      return BrowserCommandType::kStartDownload;
    case core_mojom::TaskActionOperationKind::kDownloadList:
      return BrowserCommandType::kListDownloads;
    case core_mojom::TaskActionOperationKind::kDownloadCancel:
      return BrowserCommandType::kCancelDownload;
    default:
      return std::nullopt;
  }
}

core_mojom::TaskDownloadState TaskDownloadStateToMojom(DownloadState state) {
  switch (state) {
    case DownloadState::kCreated:
      return core_mojom::TaskDownloadState::kCreated;
    case DownloadState::kInProgress:
      return core_mojom::TaskDownloadState::kInProgress;
    case DownloadState::kPaused:
      return core_mojom::TaskDownloadState::kPaused;
    case DownloadState::kInterrupted:
      return core_mojom::TaskDownloadState::kInterrupted;
    case DownloadState::kComplete:
      return core_mojom::TaskDownloadState::kComplete;
    case DownloadState::kCancelled:
      return core_mojom::TaskDownloadState::kCancelled;
  }
}

core_mojom::TaskDownloadMediaType TaskDownloadMediaTypeToMojom(
    MediaTopLevelType media_type) {
  switch (media_type) {
    case MediaTopLevelType::kApplication:
      return core_mojom::TaskDownloadMediaType::kApplication;
    case MediaTopLevelType::kAudio:
      return core_mojom::TaskDownloadMediaType::kAudio;
    case MediaTopLevelType::kFont:
      return core_mojom::TaskDownloadMediaType::kFont;
    case MediaTopLevelType::kImage:
      return core_mojom::TaskDownloadMediaType::kImage;
    case MediaTopLevelType::kMessage:
      return core_mojom::TaskDownloadMediaType::kMessage;
    case MediaTopLevelType::kModel:
      return core_mojom::TaskDownloadMediaType::kModel;
    case MediaTopLevelType::kMultipart:
      return core_mojom::TaskDownloadMediaType::kMultipart;
    case MediaTopLevelType::kText:
      return core_mojom::TaskDownloadMediaType::kText;
    case MediaTopLevelType::kVideo:
      return core_mojom::TaskDownloadMediaType::kVideo;
    case MediaTopLevelType::kUnknown:
    case MediaTopLevelType::kExample:
    case MediaTopLevelType::kHaptics:
      return core_mojom::TaskDownloadMediaType::kUnknown;
  }
}

core_mojom::TaskDownloadDirectoryClass TaskDownloadDirectoryToMojom(
    DownloadDestinationKind directory) {
  switch (directory) {
    case DownloadDestinationKind::kUndecided:
      return core_mojom::TaskDownloadDirectoryClass::kUndecided;
    case DownloadDestinationKind::kUserChosenLocation:
      return core_mojom::TaskDownloadDirectoryClass::kPersonChosen;
    case DownloadDestinationKind::kDefaultDownloadsDirectory:
      return core_mojom::TaskDownloadDirectoryClass::kDefaultDownloads;
    case DownloadDestinationKind::kApplicationPrivateDirectory:
      return core_mojom::TaskDownloadDirectoryClass::kApplicationPrivate;
  }
}

core_mojom::TaskDownloadSnapshotPtr TaskDownloadSnapshotToMojom(
    const TaskDownloadManagerSnapshot& snapshot) {
  auto out = core_mojom::TaskDownloadSnapshot::New();
  out->download_id = snapshot.download_id;
  out->state = TaskDownloadStateToMojom(snapshot.state);
  out->media_type = TaskDownloadMediaTypeToMojom(snapshot.media_type);
  out->received_bytes = snapshot.received_bytes;
  out->directory_class = TaskDownloadDirectoryToMojom(snapshot.directory_class);
  return out;
}

bool LiveTaskDownloadDocumentMatches(const core_mojom::TaskActionEffect& action,
                                     const TaskPolicyDocumentContext& live) {
  return action.document && action.executable &&
         live.tab_id == action.executable->tab_id &&
         live.frame_id == action.document->frame_id &&
         live.page_epoch == action.document->page_epoch &&
         live.origin == action.document->normalized_origin &&
         live.graph_revision >= action.document->graph_revision;
}

}  // namespace

void CoreServiceManager::ExecuteTaskDownloadAction(
    core_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->action ||
      !effect->action->executable || !effect->action->document ||
      !effect->action->executable->task_download ||
      !IsValidTaskDownloadAction(*effect->action)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), core_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  const core_mojom::TaskActionEffect& action = *effect->action;
  const core_mojom::TaskDownloadActionBinding& download =
      *action.executable->task_download;
  const std::optional<BrowserCommandType> command_type =
      TaskDownloadCommandType(action.executable->operation_kind);
  const std::optional<TaskPolicyDocumentContext> live =
      ResolveTaskPolicyDocument(browser_context_.get(),
                                action.executable->tab_id);
  if (!command_type || download.browser_session_id != browser_session_id_ ||
      !live || !LiveTaskDownloadDocumentMatches(action, *live) ||
      !TaskDownloadDestinationIsCurrent(browser_context_.get(), action) ||
      !accepted_approvals_.IsTaskSourceAuthorized(
          effect->task_id, action.executable->tab_id, *effect->operation,
          live ? live->origin : std::string(), service_generation_)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), core_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  CapabilityGrant capability;
  if (!capabilities_.CopyRegisteredCapability(
          CapabilityReference{action.capability_id}, capability)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), core_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  const CapabilityAdmission admission = capabilities_.AdmitTaskDownloadAction(
      action, effect->task_id, actor_leases_, base::TimeTicks::Now());
  if (admission != CapabilityAdmission::kAdmitted) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(),
        CompletionStatusForActionResult(AdmissionToResultCode(admission))));
    return;
  }

  TaskJournalSink* journal = task_journal_sink();
  if (!journal) {
    capabilities_.Settle(capability.capability_reference,
                         ActionResultCode::kDispatchFailed);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), core_mojom::TaskEffectCompletionStatus::kUnavailable));
    return;
  }

  DispatchIntentRecord record;
  record.dispatch_id = DispatchId{action.dispatch_id};
  record.task_id = TaskId{effect->task_id};
  record.action_id = ActionId{action.action_id};
  record.command_type = *command_type;
  record.capability_reference = capability.capability_reference;
  record.actor_lease_id = capability.actor_lease_id;
  record.tab_id = TabId{action.executable->tab_id};
  record.frame_id = FrameId{action.document->frame_id};
  record.page_epoch = PageEpoch{action.document->page_epoch};
  record.graph_revision = action.document->graph_revision;
  record.origin = Origin{
      .kind = OriginKind::kTuple,
      .serialization = action.document->normalized_origin,
  };
  if (action.executable->operation_kind ==
      core_mojom::TaskActionOperationKind::kDownloadList) {
    record.idempotency_policy = IdempotencyPolicy::kPureRead;
  } else if (action.executable->operation_kind ==
             core_mojom::TaskActionOperationKind::kDownloadCancel) {
    record.idempotency_policy = IdempotencyPolicy::kIdempotentWrite;
  } else {
    record.idempotency_policy = IdempotencyPolicy::kNonIdempotent;
  }
  record.recorded_at_monotonic_ms = TaskDownloadNowMonotonicMillis();
  journal->RecordDispatching(
      std::move(record),
      MakeTaskDownloadJournalCallback(base::BindOnce(
          &CoreServiceManager::OnTaskDownloadIntentRecorded,
          weak_factory_.GetWeakPtr(), std::move(effect), std::move(callback),
          capability.capability_reference, capability.actor_lease_id)));
}

void CoreServiceManager::OnTaskDownloadIntentRecorded(
    core_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback,
    CapabilityReference capability_reference,
    ActorLeaseId actor_lease_id,
    bool committed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->action ||
      !effect->action->document || !effect->action->executable ||
      !effect->action->executable->task_download) {
    capabilities_.Settle(capability_reference,
                         ActionResultCode::kDispatchFailed);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), core_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  const core_mojom::TaskActionEffect& action = *effect->action;
  const bool starts = action.executable->operation_kind ==
                      core_mojom::TaskActionOperationKind::kDownloadStart;
  const bool cancels = action.executable->operation_kind ==
                       core_mojom::TaskActionOperationKind::kDownloadCancel;
  if (!committed) {
    // A refused durable claim may be the same consequential dispatch from a
    // previous utility-process generation. Decision 0100 forbids turning that
    // ambiguity into another network request.
    FinishTaskDownloadAction(std::move(effect), std::move(callback),
                             capability_reference,
                             starts ? ActionResultCode::kOutcomeUnknown
                                    : ActionResultCode::kDispatchFailed,
                             nullptr, /*intent_committed=*/false);
    return;
  }
  if (shutdown_started_ || availability_ != Availability::kReady ||
      action.executable->task_download->browser_session_id !=
          browser_session_id_ ||
      !actor_leases_.IsValidFor(actor_lease_id,
                                TabId{action.executable->tab_id},
                                base::TimeTicks::Now())) {
    FinishTaskDownloadAction(std::move(effect), std::move(callback),
                             capability_reference,
                             ActionResultCode::kDispatchFailed, nullptr,
                             /*intent_committed=*/true);
    return;
  }

  const std::optional<TaskPolicyDocumentContext> live =
      ResolveTaskPolicyDocument(browser_context_.get(),
                                action.executable->tab_id);
  TaffyPageIntelligenceHost* host = FindPageIntelligenceHost(
      browser_context_.get(), action.executable->tab_id);
  if (!live || !host || !LiveTaskDownloadDocumentMatches(action, *live) ||
      !TaskDownloadDestinationIsCurrent(browser_context_.get(), action) ||
      !accepted_approvals_.IsTaskSourceAuthorized(
          effect->task_id, action.executable->tab_id, *effect->operation,
          live ? live->origin : std::string(), service_generation_)) {
    FinishTaskDownloadAction(std::move(effect), std::move(callback),
                             capability_reference,
                             ActionResultCode::kStalePageEpoch, nullptr,
                             /*intent_committed=*/true);
    return;
  }

  if (starts) {
    DownloadRequestFacts request;
    request.tab_id = TabId{action.executable->tab_id};
    request.initiator = NavigationInitiator::kAssistant;
    request.task_id = TaskId{effect->task_id};
    request.actor_lease_id = actor_lease_id;
    request.capability_reference = capability_reference;
    request.dispatch_id = DispatchId{action.dispatch_id};
    request.capability_admitted = true;
    if (GetDownloadIntentRouter().EvaluateDownload(request) !=
            DownloadDecision::kAllowOrdinaryPath ||
        !action.executable->destination_address) {
      FinishTaskDownloadAction(std::move(effect), std::move(callback),
                               capability_reference,
                               ActionResultCode::kDeniedByPolicy, nullptr,
                               /*intent_committed=*/true);
      return;
    }
    StartTaskDownload(
        host->observed_web_contents(),
        GURL(*action.executable->destination_address),
        base::BindOnce(&CoreServiceManager::OnTaskDownloadStarted,
                       weak_factory_.GetWeakPtr(), std::move(effect),
                       std::move(callback), capability_reference));
    return;
  }

  if (cancels) {
    const std::optional<std::string>& download_id =
        action.executable->task_download->download_id;
    if (!download_id ||
        !task_download_ownership_.Owns(
            TaskId{effect->task_id}, browser_session_id_,
            TabId{action.executable->tab_id}, *download_id)) {
      FinishTaskDownloadAction(std::move(effect), std::move(callback),
                               capability_reference,
                               ActionResultCode::kDeniedByPolicy, nullptr,
                               /*intent_committed=*/true);
      return;
    }
    std::optional<TaskDownloadManagerSnapshot> cancelled =
        CancelTaskDownload(host->observed_web_contents(), *download_id);
    if (!cancelled) {
      FinishTaskDownloadAction(std::move(effect), std::move(callback),
                               capability_reference,
                               ActionResultCode::kDispatchFailed, nullptr,
                               /*intent_committed=*/true);
      return;
    }
    auto result = core_mojom::TaskDownloadActionResult::New();
    result->browser_session_id = browser_session_id_;
    result->operation_kind =
        core_mojom::TaskActionOperationKind::kDownloadCancel;
    result->postcondition = core_mojom::TaskDownloadPostcondition::kCancelled;
    result->downloads.push_back(TaskDownloadSnapshotToMojom(*cancelled));
    result->truncated = false;
    FinishTaskDownloadAction(std::move(effect), std::move(callback),
                             capability_reference, ActionResultCode::kVerified,
                             std::move(result), /*intent_committed=*/true);
    return;
  }

  TaskDownloadManagerList listed = ListTaskDownloads(
      host->observed_web_contents(), core_mojom::kMaxTaskDownloadResults);
  auto result = core_mojom::TaskDownloadActionResult::New();
  result->browser_session_id = browser_session_id_;
  result->operation_kind = core_mojom::TaskActionOperationKind::kDownloadList;
  result->postcondition = core_mojom::TaskDownloadPostcondition::kListed;
  result->truncated = listed.truncated;
  result->downloads.reserve(listed.downloads.size());
  for (const TaskDownloadManagerSnapshot& snapshot : listed.downloads) {
    // Only the exact download facts exposed to this task can later back a
    // media handle. Registration grants no path and opens nothing yet.
    tool_artifact_broker_->RegisterCompletedDownload(
        effect->task_id, browser_session_id_, service_generation_,
        host->observed_web_contents(), snapshot);
    result->downloads.push_back(TaskDownloadSnapshotToMojom(snapshot));
  }
  FinishTaskDownloadAction(std::move(effect), std::move(callback),
                           capability_reference, ActionResultCode::kVerified,
                           std::move(result),
                           /*intent_committed=*/true);
}

void CoreServiceManager::OnTaskDownloadStarted(
    core_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback,
    CapabilityReference capability_reference,
    std::optional<TaskDownloadManagerSnapshot> snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  core_mojom::TaskDownloadActionResultPtr result;
  ActionResultCode code = ActionResultCode::kDispatchFailed;
  if (snapshot && effect && effect->action && effect->action->executable &&
      effect->action->executable->task_download &&
      effect->action->executable->task_download->browser_session_id ==
          browser_session_id_) {
    task_download_ownership_.Record(
        TaskId{effect->task_id}, browser_session_id_,
        TabId{effect->action->executable->tab_id}, snapshot->download_id);
    result = core_mojom::TaskDownloadActionResult::New();
    result->browser_session_id = browser_session_id_;
    result->operation_kind =
        core_mojom::TaskActionOperationKind::kDownloadStart;
    result->postcondition = core_mojom::TaskDownloadPostcondition::kStarted;
    result->downloads.push_back(TaskDownloadSnapshotToMojom(*snapshot));
    result->truncated = false;
    code = ActionResultCode::kVerified;
  }
  FinishTaskDownloadAction(std::move(effect), std::move(callback),
                           capability_reference, code, std::move(result),
                           /*intent_committed=*/true);
}

}  // namespace taffy
