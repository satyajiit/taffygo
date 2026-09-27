// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_STATE_BINDING_REGISTRY_H_
#define TAFFY_BROWSER_CORE_STATE_BINDING_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

struct PendingApprovalLookup {
  uint64_t service_generation = 0;
  uint64_t task_revision = 0;
  std::string proposal_digest;
};

struct PendingPermissionLookup {
  std::string task_id;
  core_service::mojom::PlatformPermission permission =
      core_service::mojom::PlatformPermission::kNotifications;
  core_api::mojom::PlatformPermission api_permission =
      core_api::mojom::PlatformPermission::kNotifications;
  uint64_t service_generation = 0;
  uint64_t task_revision = 0;
  uint64_t deadline_monotonic_ms = 0;
  uint64_t deadline_utc_ms = 0;
  std::string browser_session_id;
};

struct TaskSettlementLookup {
  std::string task_id;
  uint64_t service_generation = 0;
  uint64_t task_revision = 0;
  core_service::mojom::TaskSettlementKind kind =
      core_service::mojom::TaskSettlementKind::kPause;
};

struct TerminalTaskLookup {
  std::string task_id;
  uint64_t service_generation = 0;
  uint64_t task_revision = 0;
  core_service::mojom::TerminalTaskKind kind =
      core_service::mojom::TerminalTaskKind::kCompleted;
};

struct TaskControlLookup {
  uint64_t service_generation = 0;
  uint64_t task_revision = 0;
  core_service::mojom::TaskControlKind kind =
      core_service::mojom::TaskControlKind::kPause;
};

// Browser-only command correlation registered before each CoreStatus update.
// A snapshot atomically replaces both maps; no digest enters CoreStatus/UI.
class CoreStateBindingRegistry final {
 public:
  CoreStateBindingRegistry();
  CoreStateBindingRegistry(const CoreStateBindingRegistry&) = delete;
  CoreStateBindingRegistry& operator=(const CoreStateBindingRegistry&) = delete;
  ~CoreStateBindingRegistry();

  core_service::mojom::PendingApprovalRegistrationStatus Replace(
      const core_service::mojom::CoreStateBrowserBindings& bindings);
  core_service::mojom::PendingApprovalRegistrationStatus Replace(
      core_service::mojom::CoreStateBrowserBindingsPtr bindings);
  void Reset();

  std::optional<uint64_t> FindTaskRevision(const std::string& task_id) const;
  std::optional<TaskControlLookup> FindTaskControl(
      const std::string& task_id,
      core_service::mojom::TaskControlKind kind) const;
  std::optional<PendingApprovalLookup> FindPendingApproval(
      const std::string& task_id,
      const std::string& action_id) const;
  std::optional<PendingPermissionLookup> FindPendingPermission(
      const std::string& request_id) const;
  std::optional<TaskSettlementLookup> FindTaskSettlement(
      const std::string& task_id) const;
  std::optional<TerminalTaskLookup> FindTerminalTask(
      const std::string& task_id) const;
  const std::vector<TaskSettlementLookup>& task_settlements() const {
    return task_settlements_;
  }
  const std::vector<TerminalTaskLookup>& terminal_tasks() const {
    return terminal_tasks_;
  }

  uint64_t service_generation() const { return service_generation_; }
  uint64_t state_sequence() const { return state_sequence_; }
  size_t task_count() const { return task_revisions_.size(); }
  size_t approval_count() const { return pending_approvals_.size(); }
  size_t permission_count() const { return pending_permissions_.size(); }

 private:
  using ApprovalKey = std::pair<std::string, std::string>;

  base::flat_map<std::string, uint64_t> task_revisions_;
  base::flat_map<std::string, std::vector<core_service::mojom::TaskControlKind>>
      task_controls_;
  base::flat_map<ApprovalKey, PendingApprovalLookup> pending_approvals_;
  base::flat_map<std::string, PendingPermissionLookup> pending_permissions_;
  std::vector<TaskSettlementLookup> task_settlements_;
  std::vector<TerminalTaskLookup> terminal_tasks_;
  uint64_t service_generation_ = 0;
  uint64_t state_sequence_ = 0;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_STATE_BINDING_REGISTRY_H_
