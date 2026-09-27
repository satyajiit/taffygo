// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_BACKUP_PROTOCOL_H_
#define TAFFY_BROWSER_CORE_BACKUP_PROTOCOL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/browser/core_service_manager_types.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

class CoreServiceManager;
class CoreBackupProtocolRetirementState;
class CoreBackupProtocolRetirementTestPeer;

// Browser adapter for one source profile's sandboxed portable backup
// protocol. It owns request custody and validates every current-generation
// reply, but never owns restore phase policy, keys, payload bytes, paths, or a
// dormant-profile reservation.
class CoreBackupProtocol {
 public:
  using WorkflowInterest = base::ScopedClosureRunner;
  using StageAuthorizationCallback = base::OnceCallback<void(
      core_service::mojom::BackupRestoreStageAuthorizationResultPtr)>;
  using CommitAuthorizationCallback = base::OnceCallback<void(
      core_service::mojom::BackupRestoreCommitAuthorizationResultPtr)>;
  using ResolutionAuthorizationCallback = base::OnceCallback<void(
      core_service::mojom::BackupRestoreResolutionAuthorizationResultPtr)>;
  using ProtocolCallback = base::OnceCallback<void(
      core_service::mojom::BackupRestoreProtocolResultPtr)>;
  using RecoveryInspectionCallback = base::OnceCallback<void(
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr)>;
  using RecoveryResolutionAuthorizationCallback = base::OnceCallback<void(
      core_service::mojom::
          BackupRestoreRecoveryResolutionAuthorizationResultPtr)>;

  explicit CoreBackupProtocol(CoreServiceManager* manager);
  CoreBackupProtocol(const CoreBackupProtocol&) = delete;
  CoreBackupProtocol& operator=(const CoreBackupProtocol&) = delete;
  ~CoreBackupProtocol();

  void PrepareBackupManifest(BackupPrepareRequest request,
                             BackupPrepareCallback callback);
  void InspectBackupManifest(BackupInspectRequest request,
                             BackupInspectCallback callback);
  void PlanBackupRestore(BackupRestoreRequest request,
                         BackupRestoreCallback callback);

  // Keeps this source Core incarnation alive while browser-owned review or
  // hidden physical work depends on its retained portable restore session.
  // The interest has no deadline and grants no action authority. Disconnect
  // consumes `on_disconnected`; destroying the returned interest releases it.
  std::optional<WorkflowInterest> AcquireWorkflowInterest(
      base::OnceClosure on_disconnected);
  void ConfirmBackupRestorePlan(
      core_service::mojom::BackupRestorePlanConfirmationRequestPtr request,
      StageAuthorizationCallback callback);
  void ReportBackupRestoreStageVerified(
      core_service::mojom::BackupRestoreStageVerificationRequestPtr request,
      CommitAuthorizationCallback callback);
  void ReportBackupRestoreCommitOutcome(
      core_service::mojom::BackupRestoreCommitOutcomeReportPtr report,
      ProtocolCallback callback);
  void ChooseBackupRestoreResolution(
      core_service::mojom::BackupRestoreResolutionRequestPtr request,
      ResolutionAuthorizationCallback callback);
  void ReportBackupRestoreResolutionOutcome(
      core_service::mojom::BackupRestoreResolutionOutcomeReportPtr report,
      ProtocolCallback callback);
  void CancelBackupRestoreBeforeCommit(
      core_service::mojom::BackupRestoreCancellationRequestPtr request,
      ProtocolCallback callback);
  // Classifies one bounded, content-free durable history in the current
  // source Core. The result is observational and carries no physical action
  // authority.
  void InspectBackupRestoreRecovery(
      core_service::mojom::BackupRestoreRecoveryInspectionRequestPtr request,
      RecoveryInspectionCallback callback);
  void ChooseBackupRestoreRecoveryResolution(
      core_service::mojom::BackupRestoreRecoveryResolutionRequestPtr request,
      RecoveryResolutionAuthorizationCallback callback);
  void ReportBackupRestoreRecoveryResolutionOutcome(
      core_service::mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr
          report,
      ProtocolCallback callback);

  // Takes independent custody of withdrawing one exact precommit plan after
  // its browser workflow owner has gone away. The same binding is idempotent;
  // a different binding cannot replace one already retained. Success means
  // custody was accepted, not that cancellation has completed.
  bool RetireBackupRestorePlan(
      std::string public_operation_id,
      core_service::mojom::BackupRestoreBindingPtr binding);

 private:
  friend class CoreServiceManager;
  friend class CoreBackupProtocolRetirementState;
  friend class CoreBackupProtocolRetirementTestPeer;

  bool BeginCall(const core_service::mojom::OperationEnvelope* operation,
                 bool request_is_valid);
  bool CompleteCall(
      uint64_t generation,
      const core_service::mojom::OperationEnvelope& expected_operation);
  bool has_live_work() const {
    return !pending_operation_ids_.empty() ||
           !workflow_disconnect_callbacks_.empty();
  }
  void ReleaseWorkflowInterest(uint64_t interest_id);
  void OnServiceDisconnected();
  bool BackupRestorePlanningBlockedByRetirement() const;
  void ObserveBackupRestoreCancellationAdmitted(
      const core_service::mojom::OperationEnvelope& operation,
      const core_service::mojom::BackupRestoreBinding& binding);
  void ObserveBackupRestoreCancellationCompleted(
      const std::string& operation_id,
      const core_service::mojom::BackupRestoreBinding& binding,
      core_service::mojom::BackupRestoreProtocolStatus status);
  void ObserveBackupRestoreCancellationRejected(
      const core_service::mojom::BackupRestoreBinding* binding);
  void OnBackupRestoreRetirementSourceDisconnected();
  void DispatchBackupRestoreRetirementAttempt(
      const std::string& public_operation_id,
      const core_service::mojom::BackupRestoreBinding& binding);

  const raw_ptr<CoreServiceManager> manager_;
  std::unique_ptr<CoreBackupProtocolRetirementState> retirement_;
  base::flat_set<std::string> pending_operation_ids_;
  base::flat_map<uint64_t, base::OnceClosure> workflow_disconnect_callbacks_;
  uint64_t next_workflow_interest_id_ = 1u;
  base::WeakPtrFactory<CoreBackupProtocol> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_BACKUP_PROTOCOL_H_
