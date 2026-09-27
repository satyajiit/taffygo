// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <set>
#include <string>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_terminal_internal.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bool ValidIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

bool ValidDownloadIdentifier(std::string_view value) {
  if (!ValidIdentifier(value)) {
    return false;
  }
  for (const unsigned char byte : value) {
    if (byte < 0x20u || byte == 0x7fu) {
      return false;
    }
  }
  return true;
}

bool SameOperation(const mojom::OperationEnvelope& left,
                   const mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

}  // namespace

bool CopyTaskDownloadCompletion(const mojom::TaskEffectBinding& effect,
                                const mojom::TaskEffectCompletion& completion,
                                bridge::BridgeTaskTerminal& out) {
  const auto& executable = effect.action->executable;
  const auto operation = executable->operation_kind;
  const bool starts =
      operation == mojom::TaskActionOperationKind::kDownloadStart;
  const bool lists = operation == mojom::TaskActionOperationKind::kDownloadList;
  const bool cancels =
      operation == mojom::TaskActionOperationKind::kDownloadCancel;
  if ((!starts && !lists && !cancels) || !executable->task_download ||
      !completion.effect_result || !completion.effect_result->operation ||
      !SameOperation(*effect.operation, *completion.effect_result->operation) ||
      completion.effect_result->effect_id != effect.effect_id ||
      completion.effect_result->kind != mojom::EffectKind::kBrowserAction ||
      completion.effect_result->status != mojom::EffectStatus::kCompleted ||
      !completion.effect_result->browser_action ||
      completion.effect_result->browser_action->outcome !=
          mojom::BrowserActionOutcome::kCompleted ||
      !completion.effect_result->browser_action->dispatch_id ||
      *completion.effect_result->browser_action->dispatch_id !=
          effect.action->dispatch_id ||
      completion.effect_result->browser_action->discovered_source ||
      completion.effect_result->browser_action->discovery_tab_id ||
      completion.effect_result->browser_action->browser_session_id ||
      completion.effect_result->browser_action->task_tab ||
      completion.effect_result->browser_action->task_store ||
      !completion.effect_result->browser_action->task_download) {
    return false;
  }

  const auto& binding = executable->task_download;
  const auto& result = completion.effect_result->browser_action->task_download;
  const auto expected_postcondition =
      starts ? mojom::TaskDownloadPostcondition::kStarted
             : (lists ? mojom::TaskDownloadPostcondition::kListed
                      : mojom::TaskDownloadPostcondition::kCancelled);
  if (!ValidIdentifier(binding->browser_session_id) ||
      result->browser_session_id != binding->browser_session_id ||
      result->operation_kind != operation ||
      result->postcondition != expected_postcondition ||
      result->downloads.size() > mojom::kMaxTaskDownloadResults ||
      (lists && result->truncated &&
       result->downloads.size() != mojom::kMaxTaskDownloadResults) ||
      ((starts || cancels) &&
       (result->downloads.size() != 1u || result->truncated)) ||
      (cancels &&
       (!binding->download_id || !result->downloads[0] ||
        result->downloads[0]->download_id != *binding->download_id ||
        result->downloads[0]->state !=
            mojom::TaskDownloadState::kCancelled))) {
    return false;
  }

  std::set<std::string> download_ids;
  out.has_task_download_result = true;
  out.task_download_browser_session_id = result->browser_session_id;
  out.task_download_operation = static_cast<uint8_t>(result->operation_kind);
  out.task_download_postcondition = static_cast<uint8_t>(result->postcondition);
  out.task_download_truncated = result->truncated;
  for (const mojom::TaskDownloadSnapshotPtr& snapshot : result->downloads) {
    if (!snapshot || !ValidDownloadIdentifier(snapshot->download_id) ||
        !download_ids.insert(snapshot->download_id).second) {
      return false;
    }
    bridge::BridgeTaskDownloadSnapshot projected;
    projected.download_id = snapshot->download_id;
    projected.state = static_cast<uint8_t>(snapshot->state);
    projected.media_type = static_cast<uint8_t>(snapshot->media_type);
    projected.received_bytes = snapshot->received_bytes;
    projected.directory_class = static_cast<uint8_t>(snapshot->directory_class);
    out.task_download_snapshots.push_back(std::move(projected));
  }
  return true;
}

}  // namespace taffy::core_service_internal
