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
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/core_task_store_rows.h"
#include "taffy/browser/profile_store/profile_store_reader.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

// How many entries a store is asked for, so the count of rows left out is a
// number the model can act on rather than a yes. Four times the row cap is
// enough to say "narrow the words"; more would only copy more of the store
// into memory to be counted.
constexpr size_t kStoreProbeEntries = 4u * service_mojom::kMaxTaskStoreResults;

}  // namespace

void CoreServiceManager::ExecuteTaskStoreAction(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const service_mojom::TaskActionEffect& action = *effect->action;
  const service_mojom::TaskStoreActionBinding& store =
      *action.executable->task_store;
  const service_mojom::TaskActionOperationKind operation =
      action.executable->operation_kind;
  const std::optional<TaskPolicyDocumentContext> live =
      ResolveTaskPolicyDocument(browser_context_.get(),
                                action.executable->tab_id);
  if (!IsTaskStoreOperation(operation) || !live ||
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
  const CapabilityAdmission admission = capabilities_.AdmitTaskStoreAction(
      action, effect->task_id, actor_leases_, base::TimeTicks::Now());
  if (admission != CapabilityAdmission::kAdmitted) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(),
        CompletionStatusForActionResult(AdmissionToResultCode(admission))));
    return;
  }

  // No dispatch is journaled for a store read. The journal exists so that a
  // consequence in the world — a navigation, a sent form, a download — can
  // be reconciled after a crash; a local read has none, and its outcome is
  // durable only as the rows the core commits beside the action's record.
  // Claiming a dispatch identity here would be a claim of work that cannot
  // be duplicated.
  if (operation == service_mojom::TaskActionOperationKind::kOpenTabsList) {
    FinishTaskStoreAction(std::move(effect), std::move(callback),
                          capability.capability_reference, ListPersonTabs());
    return;
  }
  if (!profile_store_reader_) {
    capabilities_.Settle(capability.capability_reference,
                         ActionResultCode::kUnsupported);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kUnavailable));
    return;
  }
  const std::u16string words =
      store.query ? base::UTF8ToUTF16(*store.query) : std::u16string();
  ProfileStoreReader::EntriesCallback on_read = base::BindOnce(
      &CoreServiceManager::OnTaskStoreEntriesRead, weak_factory_.GetWeakPtr(),
      std::move(effect), std::move(callback), capability.capability_reference,
      service_generation_);
  if (operation == service_mojom::TaskActionOperationKind::kHistorySearch ||
      operation == service_mojom::TaskActionOperationKind::kHistoryRecent) {
    profile_store_reader_->ReadHistory(words, kStoreProbeEntries,
                                       std::move(on_read));
    return;
  }
  profile_store_reader_->ReadBookmarks(words, kStoreProbeEntries,
                                       std::move(on_read));
}

void CoreServiceManager::OnTaskStoreEntriesRead(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback,
    CapabilityReference capability_reference,
    uint64_t generation,
    std::vector<TaskStoreEntry> entries) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || generation != service_generation_ ||
      availability_ != Availability::kReady) {
    capabilities_.Settle(capability_reference,
                         ActionResultCode::kDispatchFailed);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  FinishTaskStoreAction(std::move(effect), std::move(callback),
                        capability_reference, entries);
}

void CoreServiceManager::FinishTaskStoreAction(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback,
    CapabilityReference capability_reference,
    const std::vector<TaskStoreEntry>& entries) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const service_mojom::TaskActionEffect& action = *effect->action;
  service_mojom::TaskStoreActionResultPtr result = BuildTaskStoreResult(
      action.executable->operation_kind, entries,
      action.executable->task_store->limit);
  capabilities_.Settle(capability_reference, ActionResultCode::kVerified);
  auto completion = MakeTaskEffectCompletion(
      effect.get(), service_mojom::TaskEffectCompletionStatus::kSucceeded);
  auto effect_result = service_mojom::EffectResult::New();
  effect_result->operation = effect->operation.Clone();
  effect_result->effect_id = effect->effect_id;
  effect_result->status = service_mojom::EffectStatus::kCompleted;
  effect_result->kind = service_mojom::EffectKind::kBrowserAction;
  effect_result->browser_action =
      service_mojom::BrowserActionEffectResult::New();
  effect_result->browser_action->outcome =
      service_mojom::BrowserActionOutcome::kCompleted;
  effect_result->browser_action->dispatch_id = action.dispatch_id;
  effect_result->browser_action->task_store = std::move(result);
  completion->effect_result = std::move(effect_result);
  std::move(callback).Run(std::move(completion));
}

}  // namespace taffy
