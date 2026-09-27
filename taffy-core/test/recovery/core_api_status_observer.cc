// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/core_api_status_observer.h"

#include <optional>
#include <utility>
#include <vector>

#include "taffy/test/recovery/core_api_status_reader.h"

namespace taffy::test {

namespace api = core_api::mojom;

CoreApiStatusObserver::CoreApiStatusObserver() = default;
CoreApiStatusObserver::~CoreApiStatusObserver() = default;

mojo::PendingRemote<core_api::mojom::TaffyProfileCoreApiObserver>
CoreApiStatusObserver::BindNewPipeAndPassRemote() {
  return receiver_.BindNewPipeAndPassRemote();
}

std::optional<ObservedTaskStatus> CoreApiStatusObserver::only_task() const {
  return tasks_.size() == 1u ? std::optional<ObservedTaskStatus>(tasks_.front())
                             : std::nullopt;
}

std::optional<ObservedTaskStatus> CoreApiStatusObserver::task_other_than(
    const std::string& task_id) const {
  std::optional<ObservedTaskStatus> found;
  for (const ObservedTaskStatus& task : tasks_) {
    if (task.task_id == task_id) {
      continue;
    }
    if (found) {
      return std::nullopt;
    }
    found = task;
  }
  return found;
}

std::optional<ObservedTaskStatus> CoreApiStatusObserver::only_task_in_phase(
    api::TaskPhase phase) const {
  const std::optional<ObservedTaskStatus> task = only_task();
  return task && task->phase == phase ? task : std::nullopt;
}

std::optional<ObservedWorkspaceStatus> CoreApiStatusObserver::only_workspace()
    const {
  return workspaces_.size() == 1u
             ? std::optional<ObservedWorkspaceStatus>(workspaces_.front())
             : std::nullopt;
}

void CoreApiStatusObserver::SetFirstTaskCallback(
    base::OnceCallback<void(ObservedTaskStatus)> callback) {
  first_task_callback_ = std::move(callback);
}

void CoreApiStatusObserver::OnSnapshot(
    api::CoreAvailability availability,
    uint64_t service_generation,
    uint64_t state_sequence,
    uint32_t status_schema_version,
    const std::optional<std::vector<uint8_t>>& status_payload) {
  if (!status_payload) {
    // Non-ready availability is deliberately published without a payload.
    // Retain the last decoded watermark, but expose withdrawal immediately.
    if (availability == api::CoreAvailability::kReady) {
      malformed_payload_seen_ = true;
      availability_ = api::CoreAvailability::kUnavailable;
    } else {
      availability_ = availability;
    }
    return;
  }
  internal::CoreStatusWireReader reader(*status_payload);
  std::optional<internal::ObservedCoreStatusPayload> payload =
      reader.Read(status_schema_version);
  if (!payload || payload->availability != availability ||
      payload->generation != service_generation) {
    malformed_payload_seen_ = true;
    return;
  }

  availability_ = payload->availability;
  service_generation_ = payload->generation;
  state_sequence_ = state_sequence;
  tasks_ = std::move(payload->tasks);
  workspaces_ = std::move(payload->workspaces);
  workspace_export_ = std::move(payload->workspace_export);
  library_ = std::move(payload->library);
  library_export_ = std::move(payload->library_export);
  memory_ = std::move(payload->memory);
  site_skills_ = std::move(payload->site_skills);
  site_skills_complete_ = payload->site_skills_complete;
  snapshot_seen_ = true;
  if (tasks_.size() == 1u && first_task_callback_) {
    std::move(first_task_callback_).Run(tasks_.front());
  }
}

void CoreApiStatusObserver::OnPermissionRequest(const std::string&,
                                                api::PlatformPermission) {
  permission_request_seen_ = true;
}

}  // namespace taffy::test
