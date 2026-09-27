// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_action_reconciliation.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_action_route.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/components/storage/browser/core_storage_broker.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool SuccessNeedsTransientPayload(mojom::TaskActionOperationKind operation) {
  // The journal retains only the result code. A verified page action needs no
  // returned value, while reads, navigation/source discovery, tab and download
  // work all return transient evidence that cannot be reconstructed here.
  //
  // A reload and a stop are the exception inside the navigation executor: both
  // leave the tab on the document it was already on, so neither returns an
  // observation, a new source, a tab or a download. They are routed there
  // because they are navigation *commands*, not because they answer with what
  // a navigation answers with — and reading the route as the payload meant a
  // verified reload could never be reconciled. The effect answered "still
  // unknown", the reducer asked the person a question it had no way to
  // phrase, and the errand stopped there with no move that could resume it.
  if (operation == mojom::TaskActionOperationKind::kReload ||
      operation == mojom::TaskActionOperationKind::kStopLoading) {
    return false;
  }
  return TaskActionExecutorForOperation(operation) !=
         TaskActionExecutor::kPageAction;
}

mojom::TaskEffectCompletionPtr UnknownCompletion(
    const mojom::TaskEffectBinding* effect) {
  return MakeTaskEffectCompletion(
      effect, mojom::TaskEffectCompletionStatus::kOutcomeUnknown);
}

void OnJournalRead(mojom::TaskEffectBindingPtr effect,
                   TaskActionReconciliationCallback callback,
                   std::optional<TaskActionJournalLookup> lookup) {
  if (!effect || !effect->reconcile || !lookup ||
      lookup->state != TaskActionJournalState::kTerminal ||
      (lookup->result_code == ActionResultCode::kVerified &&
       SuccessNeedsTransientPayload(effect->reconcile->operation))) {
    std::move(callback).Run(UnknownCompletion(effect.get()));
    return;
  }
  auto completion = MakeTaskEffectCompletion(
      effect.get(), mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->reconciled_action_result =
      mojom::TaskReconciledActionResult::New(
          static_cast<uint32_t>(lookup->result_code));
  std::move(callback).Run(std::move(completion));
}

}  // namespace

void ReconcileTaskAction(CoreStorageBroker* storage,
                         mojom::TaskEffectBindingPtr effect,
                         TaskActionReconciliationCallback callback) {
  if (!storage || !effect || !effect->reconcile) {
    std::move(callback).Run(UnknownCompletion(effect.get()));
    return;
  }
  const mojom::TaskReconcileEffect& reconcile = *effect->reconcile;
  storage->ReadTaskActionJournal(
      DispatchId{reconcile.dispatch_id}, TaskId{effect->task_id},
      ActionId{reconcile.action_id},
      base::BindOnce(&OnJournalRead, std::move(effect), std::move(callback)));
}

}  // namespace taffy
