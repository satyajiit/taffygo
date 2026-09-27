// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

class DownloadTerminalJournalCallback final : public TaskJournalAppendCallback {
 public:
  DownloadTerminalJournalCallback() = default;
  ~DownloadTerminalJournalCallback() override = default;

  void Run(bool) override {}
};

}  // namespace

void CoreServiceManager::FinishTaskDownloadAction(
    core_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback,
    CapabilityReference capability_reference,
    ActionResultCode code,
    core_mojom::TaskDownloadActionResultPtr task_download_result,
    bool intent_committed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  capabilities_.Settle(capability_reference, code);
  if (!effect || !effect->operation || !effect->action ||
      !effect->action->document || !effect->action->executable ||
      !effect->action->executable->task_download ||
      !IsTaskDownloadOperation(effect->action->executable->operation_kind)) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), core_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  const core_mojom::TaskActionEffect& action = *effect->action;
  const bool starts = action.executable->operation_kind ==
                      core_mojom::TaskActionOperationKind::kDownloadStart;
  const bool pace_snapshot =
      task_download_result &&
      action.executable->operation_kind ==
          core_mojom::TaskActionOperationKind::kDownloadList &&
      std::ranges::any_of(
          task_download_result->downloads, [&](const auto& snapshot) {
            return snapshot &&
                   (snapshot->state ==
                        core_mojom::TaskDownloadState::kCreated ||
                    snapshot->state ==
                        core_mojom::TaskDownloadState::kInProgress) &&
                   task_download_ownership_.Owns(
                       TaskId{effect->task_id}, browser_session_id_,
                       TabId{action.executable->tab_id}, snapshot->download_id);
          });

  ActionResult terminal;
  terminal.schema_version = kBipSchemaVersion;
  terminal.request_id = RequestId{action.dispatch_id};
  terminal.action_id = ActionId{action.action_id};
  terminal.dispatch_id = DispatchId{action.dispatch_id};
  terminal.result_code = code;
  terminal.dispatched = code == ActionResultCode::kVerified;
  terminal.terminal = true;
  if (terminal.dispatched) {
    terminal.verified_by.push_back(starts ? VerifierKind::kBrowserNetworkEvent
                                          : VerifierKind::kFreshSnapshot);
  }
  terminal.observed_page_epoch = PageEpoch{action.document->page_epoch};
  terminal.observed_graph_revision = action.document->graph_revision;
  terminal.completed_at_monotonic_ms = NowMonotonicMillis();
  // After a durable start claim, absence of a typed success cannot prove the
  // request did not reach Chromium's network stack. Preserve the identity and
  // tell reconciliation that repeating may create a second transfer.
  terminal.repeat_may_duplicate_effect =
      starts && intent_committed &&
      RepeatMayDuplicateEffect(IdempotencyPolicy::kNonIdempotent);
  if (intent_committed) {
    if (TaskJournalSink* journal = task_journal_sink()) {
      journal->RecordTerminalResult(
          terminal, std::make_unique<DownloadTerminalJournalCallback>());
    }
  }

  const core_mojom::TaskEffectCompletionStatus status =
      CompletionStatusForActionResult(code);
  auto completion = MakeTaskEffectCompletion(effect.get(), status);
  if (status == core_mojom::TaskEffectCompletionStatus::kSucceeded &&
      task_download_result) {
    auto result = core_mojom::EffectResult::New();
    result->operation = effect->operation.Clone();
    result->effect_id = effect->effect_id;
    result->status = core_mojom::EffectStatus::kCompleted;
    result->kind = core_mojom::EffectKind::kBrowserAction;
    result->browser_action = core_mojom::BrowserActionEffectResult::New();
    result->browser_action->outcome =
        core_mojom::BrowserActionOutcome::kCompleted;
    result->browser_action->dispatch_id = action.dispatch_id;
    result->browser_action->task_download = std::move(task_download_result);
    completion->effect_result = std::move(result);
  }
  // A deterministic replay can immediately ask for the next snapshot. Pace
  // incomplete owned transfers so it cannot spin journal writes while the
  // network is busy; completed/manual downloads return without this delay.
  if (pace_snapshot) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::move(completion)),
        base::Milliseconds(250));
  } else {
    std::move(callback).Run(std::move(completion));
  }
}

}  // namespace taffy
