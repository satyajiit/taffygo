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
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

uint64_t TaskActionNowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

class TaskTabJournalCallback final : public TaskJournalAppendCallback {
 public:
  explicit TaskTabJournalCallback(base::OnceCallback<void(bool)> callback)
      : callback_(std::move(callback)) {}
  ~TaskTabJournalCallback() override = default;

  void Run(bool committed) override {
    if (callback_) {
      std::move(callback_).Run(committed);
    }
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

std::unique_ptr<TaskJournalAppendCallback> MakeTaskTabJournalCallback(
    base::OnceCallback<void(bool)> callback) {
  return std::make_unique<TaskTabJournalCallback>(std::move(callback));
}

std::optional<BrowserCommandType> TaskTabCommandType(
    service_mojom::TaskActionOperationKind operation) {
  switch (operation) {
    case service_mojom::TaskActionOperationKind::kTabsList:
      return std::make_optional(BrowserCommandType::kListTaskTabs);
    case service_mojom::TaskActionOperationKind::kTabsActivate:
      return std::make_optional(BrowserCommandType::kActivateTaskTab);
    case service_mojom::TaskActionOperationKind::kTabsClose:
      return std::make_optional(BrowserCommandType::kCloseTaskTab);
    default:
      return std::nullopt;
  }
}

}  // namespace

void CoreServiceManager::ExecuteTaskTabRelease(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::optional<TerminalTaskLookup> terminal =
      state_bindings_.FindTerminalTask(effect->task_id);
  if (!terminal || terminal->service_generation != service_generation_ ||
      terminal->task_revision != effect->operation->task_revision ||
      terminal->task_revision != effect->release_tabs->terminal_revision) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  // Claim the exact browser-session ownership set before revoking task
  // authority. The registry contains only tabs marked before TabModel
  // publication; consented source tabs are user-owned and can never enter
  // this set. Repeating this effect sees an empty set after successful close.
  const bool released_tabs = ReleaseOwnedTaskTabs(effect->task_id);
  AbandonOpenHandover(effect->task_id);
  actor_leases_.RevokeTask(effect->task_id, service_generation_);
  capabilities_.RevokeTask(effect->task_id, service_generation_);
  value_references_.RevokeTask(TaskId{effect->task_id});
  if (field_value_requests_) {
    field_value_requests_->CloseRequestsForTask(effect->task_id);
  }
  accepted_approvals_.RevokeTask(effect->task_id, service_generation_);
  const auto disposition =
      terminal->kind == service_mojom::TerminalTaskKind::kCompleted ||
              terminal->kind == service_mojom::TerminalTaskKind::kPartial
          ? ProfileToolArtifactBroker::TaskDisposition::kFinished
          : ProfileToolArtifactBroker::TaskDisposition::kAbandoned;
  tool_artifact_broker_->SettleTask(effect->task_id, service_generation_,
                                    disposition);
  effect_broker_->CancelTask(effect->task_id, service_generation_);
  std::move(callback).Run(MakeTaskEffectCompletion(
      effect.get(),
      released_tabs ? service_mojom::TaskEffectCompletionStatus::kSucceeded
                    : service_mojom::TaskEffectCompletionStatus::kUnavailable));
}

void CoreServiceManager::ExecuteTaskTabAction(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const service_mojom::TaskActionEffect& action = *effect->action;
  const service_mojom::TaskTabActionBinding& tab = *action.executable->task_tab;
  const std::optional<BrowserCommandType> command_type =
      TaskTabCommandType(action.executable->operation_kind);
  const std::optional<TaskPolicyDocumentContext> live =
      ResolveTaskPolicyDocument(browser_context_.get(),
                                action.executable->tab_id);
  if (!command_type || tab.browser_session_id != browser_session_id_ || !live ||
      live->frame_id != action.document->frame_id ||
      live->page_epoch != action.document->page_epoch ||
      live->origin != action.document->normalized_origin ||
      live->graph_revision < action.document->graph_revision ||
      !accepted_approvals_.IsTaskSourceAuthorized(
          effect->task_id, action.executable->tab_id, *effect->operation,
          live ? live->origin : std::string(), service_generation_)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  CapabilityGrant capability;
  if (!capabilities_.CopyRegisteredCapability(
          CapabilityReference{action.capability_id}, capability)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  const CapabilityAdmission admission = capabilities_.AdmitTaskTabAction(
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
        effect.get(), service_mojom::TaskEffectCompletionStatus::kUnavailable));
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
  record.idempotency_policy =
      action.executable->operation_kind ==
              service_mojom::TaskActionOperationKind::kTabsList
          ? IdempotencyPolicy::kPureRead
          : IdempotencyPolicy::kConditionallyIdempotent;
  record.recorded_at_monotonic_ms = TaskActionNowMonotonicMillis();
  journal->RecordDispatching(
      std::move(record),
      MakeTaskTabJournalCallback(base::BindOnce(
          &CoreServiceManager::OnTaskTabIntentRecorded,
          weak_factory_.GetWeakPtr(), std::move(effect), std::move(callback),
          capability.capability_reference)));
}

void CoreServiceManager::OnTaskTabIntentRecorded(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback,
    CapabilityReference capability_reference,
    bool committed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->action ||
      !effect->action->document || !effect->action->executable ||
      !effect->action->executable->task_tab) {
    capabilities_.Settle(capability_reference,
                         ActionResultCode::kDispatchFailed);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  const service_mojom::TaskActionEffect& action = *effect->action;
  const service_mojom::TaskTabActionBinding& tab = *action.executable->task_tab;
  ActionResultCode code = ActionResultCode::kDispatchFailed;
  std::optional<service_mojom::TaskTabActionResultPtr> task_tab_result;
  if (committed && !shutdown_started_ &&
      availability_ == Availability::kReady &&
      tab.browser_session_id == browser_session_id_) {
    const std::optional<TaskPolicyDocumentContext> live =
        ResolveTaskPolicyDocument(browser_context_.get(),
                                  action.executable->tab_id);
    if (live && live->frame_id == action.document->frame_id &&
        live->page_epoch == action.document->page_epoch &&
        live->origin == action.document->normalized_origin &&
        live->graph_revision >= action.document->graph_revision &&
        accepted_approvals_.IsTaskSourceAuthorized(
            effect->task_id, action.executable->tab_id, *effect->operation,
            live->origin, service_generation_)) {
      switch (action.executable->operation_kind) {
        case service_mojom::TaskActionOperationKind::kTabsList:
          task_tab_result =
              ListOwnedTaskTabs(effect->task_id, tab.browser_session_id);
          break;
        case service_mojom::TaskActionOperationKind::kTabsActivate:
          if (tab.target) {
            task_tab_result = ActivateOwnedTaskTab(
                effect->task_id, tab.browser_session_id, *tab.target);
          }
          break;
        case service_mojom::TaskActionOperationKind::kTabsClose:
          if (tab.target) {
            task_tab_result = CloseOwnedTaskTab(
                effect->task_id, tab.browser_session_id, *tab.target);
          }
          break;
        default:
          break;
      }
      code = task_tab_result && *task_tab_result
                 ? ActionResultCode::kVerified
                 : ActionResultCode::kStalePageEpoch;
    } else {
      code = ActionResultCode::kStalePageEpoch;
    }
  }
  capabilities_.Settle(capability_reference, code);

  ActionResult terminal;
  terminal.schema_version = kBipSchemaVersion;
  terminal.request_id = RequestId{action.dispatch_id};
  terminal.action_id = ActionId{action.action_id};
  terminal.dispatch_id = DispatchId{action.dispatch_id};
  terminal.result_code = code;
  terminal.dispatched = code == ActionResultCode::kVerified;
  terminal.terminal = true;
  if (terminal.dispatched) {
    terminal.verified_by.push_back(VerifierKind::kBrowserTabEvent);
  }
  terminal.observed_page_epoch = PageEpoch{action.document->page_epoch};
  terminal.observed_graph_revision = action.document->graph_revision;
  terminal.completed_at_monotonic_ms = TaskActionNowMonotonicMillis();
  const IdempotencyPolicy idempotency =
      action.executable->operation_kind ==
              service_mojom::TaskActionOperationKind::kTabsList
          ? IdempotencyPolicy::kPureRead
          : IdempotencyPolicy::kConditionallyIdempotent;
  terminal.repeat_may_duplicate_effect =
      terminal.dispatched &&
      (RepeatMayDuplicateEffect(idempotency) || IsAmbiguousOutcome(code));
  if (committed) {
    TaskJournalSink* journal = task_journal_sink();
    if (journal) {
      journal->RecordTerminalResult(
          terminal, MakeTaskTabJournalCallback(base::BindOnce([](bool) {})));
    }
  }

  const service_mojom::TaskEffectCompletionStatus status =
      CompletionStatusForActionResult(code);
  auto completion = MakeTaskEffectCompletion(effect.get(), status);
  if (status == service_mojom::TaskEffectCompletionStatus::kSucceeded &&
      task_tab_result && *task_tab_result) {
    auto result = service_mojom::EffectResult::New();
    result->operation = effect->operation.Clone();
    result->effect_id = effect->effect_id;
    result->status = service_mojom::EffectStatus::kCompleted;
    result->kind = service_mojom::EffectKind::kBrowserAction;
    result->browser_action = service_mojom::BrowserActionEffectResult::New();
    result->browser_action->outcome =
        service_mojom::BrowserActionOutcome::kCompleted;
    result->browser_action->dispatch_id = action.dispatch_id;
    result->browser_action->task_tab = std::move(*task_tab_result);
    completion->effect_result = std::move(result);
  }
  std::move(callback).Run(std::move(completion));
}

}  // namespace taffy
