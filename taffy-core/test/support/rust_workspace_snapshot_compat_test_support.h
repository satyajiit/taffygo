// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_RUST_WORKSPACE_SNAPSHOT_COMPAT_TEST_SUPPORT_H_
#define TAFFY_TEST_SUPPORT_RUST_WORKSPACE_SNAPSHOT_COMPAT_TEST_SUPPORT_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

class CoreEffectBroker;
class CoreStorageBroker;
class RustCore;
struct PendingApprovalLookup;

namespace test {

struct WorkspaceSnapshotObservationResult {
  core_service::mojom::PolicyEvaluationStatus policy_status =
      core_service::mojom::PolicyEvaluationStatus::kInvalidRequest;
  core_service::mojom::TaskReducerEffectKind action_kind =
      core_service::mojom::TaskReducerEffectKind::kAskPolicy;
  uint64_t grant_graph_revision = 0u;
  core_service::mojom::AdmissionStatus below_floor_status =
      core_service::mojom::AdmissionStatus::kCoreUnavailable;
  uint64_t observation_graph_revision = 0u;
  core_service::mojom::AdmissionStatus observation_status =
      core_service::mojom::AdmissionStatus::kCoreUnavailable;
  size_t post_observation_commit_count = 0u;
  core_service::mojom::TaskReducerEffectKind terminal_effect_kind =
      core_service::mojom::TaskReducerEffectKind::kAskPolicy;
  uint64_t terminal_task_revision = 0u;
};

inline constexpr uint64_t kWorkspaceSnapshotGeneration = 7u;
inline constexpr uint64_t kWorkspaceSnapshotNowMillis = 10'000u;
inline constexpr uint64_t kWorkspaceSnapshotNowUtcMillis = 1'800'000'000'000u;
inline constexpr uint64_t kWorkspaceSnapshotApprovalExpiresAtUtcMillis =
    kWorkspaceSnapshotNowUtcMillis + 60'000u;

core_service::mojom::CoreBootstrapPtr MakeWorkspaceSnapshotBootstrap();
core_service::mojom::CoreServiceCommandPtr MakeWorkspaceSnapshotStartTask();
core_service::mojom::CoreServiceCommandPtr MakeWorkspaceSnapshotApproval(
    const PendingApprovalLookup& pending,
    const std::string& action_id);

core_service::mojom::EffectResultPtr DispatchWorkspaceSnapshotEffect(
    CoreEffectBroker* broker,
    core_service::mojom::EffectEnvelopePtr effect);
core_service::mojom::CoreBootstrapPtr LoadWorkspaceSnapshotBootstrap(
    CoreStorageBroker* broker);
core_service::mojom::PolicyEvaluationResultPtr
EvaluateWorkspaceSnapshotReadPolicy(
    RustCore* core,
    const core_service::mojom::TaskPolicyEffect& policy,
    uint64_t now_monotonic_ms,
    uint64_t graph_revision = 0u);
WorkspaceSnapshotObservationResult ExerciseWorkspaceSnapshotObservation(
    RustCore* core,
    CoreEffectBroker* broker,
    core_service::mojom::TaskEffectBindingPtr approved_policy_effect,
    uint64_t now_monotonic_ms);
void VerifyWorkspaceSnapshotTerminalSaveAndRestore(
    RustCore* core,
    CoreEffectBroker* broker,
    CoreStorageBroker* storage,
    core_service::mojom::TaskEffectBindingPtr approved_policy_effect,
    uint64_t now_monotonic_ms);

}  // namespace test
}  // namespace taffy

#endif  // TAFFY_TEST_SUPPORT_RUST_WORKSPACE_SNAPSHOT_COMPAT_TEST_SUPPORT_H_
