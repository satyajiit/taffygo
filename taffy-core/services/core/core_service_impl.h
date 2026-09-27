// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_H_
#define TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_deadline_scheduler.h"
#include "taffy/services/core/core_publication_queue.h"
#include "taffy/services/core/rust_core.h"

namespace taffy {

// Utility-main-sequence Mojo owner for one profile-partitioned service
// process. Ordered state is always posted to `core_`; only control,
// cancellation, host publication, and teardown remain on this sequence.
class CoreServiceImpl final : public core_service::mojom::TaffyCoreService,
                              public core_service::mojom::CoreSession {
 public:
  explicit CoreServiceImpl(
      mojo::PendingReceiver<core_service::mojom::TaffyCoreService> receiver);
  CoreServiceImpl(const CoreServiceImpl&) = delete;
  CoreServiceImpl& operator=(const CoreServiceImpl&) = delete;
  ~CoreServiceImpl() override;

 private:
  // Reads the scheduler rather than the clock, so the suite proves whether an
  // empty Rust authority can wake the process at all.
  friend class CoreServiceImplDeadlineTestPeer;

  // Reads what the tool-stream seam refused. A chunk that is dropped and a
  // chunk that never arrived are the same silence from outside, so the count
  // is the only thing that tells the two branches of
  // core_service_impl_tool_stream.cc apart.
  friend class CoreServiceImplToolStreamTestPeer;
  friend class CoreServiceImplPublicationTestPeer;
  friend class CoreServiceImplTaskEffectTestPeer;
  friend class CoreServiceImplParallelReadsTestPeer;
  friend class CoreServiceImplBackupTestPeer;
  friend class CoreServiceImplReplyGuardTestPeer;
  friend class CoreServiceImplBackupRecoveryTestPeer;
  friend class CoreServiceImplBackupRecoveryResolutionTestPeer;

  static constexpr size_t kMaxSessions = 1;

  struct EffectIdentity {
    std::string effect_id;
    std::string operation_id;
    uint64_t service_generation = 0u;
    uint64_t task_revision = 0u;
    uint64_t deadline_monotonic_ms = 0u;
    std::string idempotency_key;
  };

  struct PendingTaskEffectCommit {
    EffectIdentity identity;
    core_service::mojom::StorageOperation operation_kind =
        core_service::mojom::StorageOperation::kAppendTaskCommit;
    std::string task_id;
    uint64_t expected_revision = 0u;
    uint64_t resulting_revision = 0u;
  };

  // core_service::mojom::TaffyCoreService:
  void Initialize(core_service::mojom::CoreBootstrapPtr bootstrap,
                  mojo::PendingRemote<core_service::mojom::CoreHost> host,
                  InitializeCallback callback) override;
  void OpenSession(const std::string& session_id,
                   mojo::PendingReceiver<core_service::mojom::CoreSession>
                       receiver) override;
  void PrepareForShutdown(PrepareForShutdownCallback callback) override;

  // core_service::mojom::CoreSession:
  void Submit(core_service::mojom::CoreServiceCommandPtr command,
              SubmitCallback callback) override;
  void ValidateAccountTokenResponse(
      core_service::mojom::AccountTokenValidationRequestPtr request,
      ValidateAccountTokenResponseCallback callback) override;
  void EvaluatePolicy(core_service::mojom::PolicyEvaluationRequestPtr request,
                      EvaluatePolicyCallback callback) override;
  void PrepareBackupManifest(
      core_service::mojom::BackupManifestPrepareRequestPtr request,
      PrepareBackupManifestCallback callback) override;
  void InspectBackupManifest(
      core_service::mojom::BackupManifestInspectRequestPtr request,
      InspectBackupManifestCallback callback) override;
  void PlanBackupRestore(
      core_service::mojom::BackupRestorePlanRequestPtr request,
      PlanBackupRestoreCallback callback) override;
  void ConfirmBackupRestorePlan(
      core_service::mojom::BackupRestorePlanConfirmationRequestPtr request,
      ConfirmBackupRestorePlanCallback callback) override;
  void ReportBackupRestoreStageVerified(
      core_service::mojom::BackupRestoreStageVerificationRequestPtr request,
      ReportBackupRestoreStageVerifiedCallback callback) override;
  void ReportBackupRestoreCommitOutcome(
      core_service::mojom::BackupRestoreCommitOutcomeReportPtr report,
      ReportBackupRestoreCommitOutcomeCallback callback) override;
  void ChooseBackupRestoreResolution(
      core_service::mojom::BackupRestoreResolutionRequestPtr request,
      ChooseBackupRestoreResolutionCallback callback) override;
  void ReportBackupRestoreResolutionOutcome(
      core_service::mojom::BackupRestoreResolutionOutcomeReportPtr report,
      ReportBackupRestoreResolutionOutcomeCallback callback) override;
  void CancelBackupRestoreBeforeCommit(
      core_service::mojom::BackupRestoreCancellationRequestPtr request,
      CancelBackupRestoreBeforeCommitCallback callback) override;
  void InspectBackupRestoreRecovery(
      core_service::mojom::BackupRestoreRecoveryInspectionRequestPtr request,
      InspectBackupRestoreRecoveryCallback callback) override;
  void ChooseBackupRestoreRecoveryResolution(
      core_service::mojom::BackupRestoreRecoveryResolutionRequestPtr request,
      ChooseBackupRestoreRecoveryResolutionCallback callback) override;
  void ReportBackupRestoreRecoveryResolutionOutcome(
      core_service::mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr
          report,
      ReportBackupRestoreRecoveryResolutionOutcomeCallback callback) override;
  void ExportPageSnapshot(
      core_service::mojom::PageSnapshotExportCommandPtr command,
      ExportPageSnapshotCallback callback) override;
  void CancelPageSnapshotExport(
      core_service::mojom::OperationEnvelopePtr operation,
      CancelPageSnapshotExportCallback callback) override;
  void QuerySavedFlows(core_service::mojom::SavedFlowQueryCommandPtr command,
                       QuerySavedFlowsCallback callback) override;
  void MatchSiteSkills(core_service::mojom::SiteSkillMatchCommandPtr command,
                       MatchSiteSkillsCallback callback) override;
  void CompleteTaskSettlement(
      core_service::mojom::TaskSettlementBindingPtr settlement) override;
  void Cancel(core_service::mojom::OperationEnvelopePtr operation) override;
  void DeliverToolStreamChunk(
      core_service::mojom::ToolStreamChunkPtr chunk) override;
  void DeliverModelStreamChunk(
      core_service::mojom::ModelStreamChunkPtr chunk,
      DeliverModelStreamChunkCallback callback) override;
  void DeliverEffectResult(
      core_service::mojom::EffectResultPtr result) override;
  void PlanEntitlementRefresh(
      core_service::mojom::EntitlementFetchReason reason,
      PlanEntitlementRefreshCallback callback) override;
  void DeliverEntitlementFetchResult(
      core_service::mojom::EffectResultPtr result,
      DeliverEntitlementFetchResultCallback callback) override;

  uint64_t NowMonotonicMillis() const;
  template <typename Result>
  void OnBackupProtocolReply(
      core_service::mojom::OperationEnvelopePtr operation,
      base::OnceCallback<void(mojo::StructPtr<Result>)> callback,
      mojo::StructPtr<Result> result);
  void OnBackupRecoveryInspectionReply(
      core_service::mojom::OperationEnvelopePtr operation,
      InspectBackupRestoreRecoveryCallback callback,
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result);
  // The same guard for the three planning replies. It is a second template
  // rather than a parameter of the first because their refusal status is a
  // different closed enum — `BackupPlanningStatus`, not
  // `BackupRestoreProtocolStatus` — and a closed enum has no cross-contract
  // spelling to share.
  template <typename Result>
  void OnBackupPlanningReply(
      core_service::mojom::OperationEnvelopePtr operation,
      base::OnceCallback<void(mojo::StructPtr<Result>)> callback,
      mojo::StructPtr<Result> result);
  // `BackupRestorePlanResult` is the one backup result with a second
  // non-nullable struct member, so its refusal has to mint a target as well as
  // an operation. It carries the requested kind rather than a default so that
  // a refusal still says which target was asked for.
  void OnBackupRestorePlanReply(
      core_service::mojom::OperationEnvelopePtr operation,
      core_service::mojom::BackupRestoreTargetKind target_kind,
      PlanBackupRestoreCallback callback,
      core_service::mojom::BackupRestorePlanResultPtr result);
  // The same refusal for the two other non-nullable single-result replies.
  // Each keeps its own closed status enum, which is why they are three
  // functions rather than one.
  void OnPageSnapshotExportReply(
      core_service::mojom::OperationEnvelopePtr operation,
      core_service::mojom::PageSnapshotExportFormat format,
      ExportPageSnapshotCallback callback,
      core_service::mojom::PageSnapshotExportResultPtr result);
  void OnSavedFlowQueryReply(
      core_service::mojom::OperationEnvelopePtr operation,
      QuerySavedFlowsCallback callback,
      core_service::mojom::SavedFlowQueryResultPtr result);
  void OnSiteSkillMatchReply(
      core_service::mojom::OperationEnvelopePtr operation,
      MatchSiteSkillsCallback callback,
      core_service::mojom::SiteSkillMatchResultPtr result);
  void OnAccountTokenValidationReply(
      std::string operation_id,
      core_service::mojom::AccountNetworkOperation operation_kind,
      core_service::mojom::AccountAuthMethod auth_method,
      uint64_t target_rotation,
      ValidateAccountTokenResponseCallback callback,
      core_service::mojom::AccountTokenValidationResultPtr result);
  void OnInitialized(InitializeCallback callback,
                     CoreInitializationBatch batch);
  void OnInitialBindingsRegistered(
      InitializeCallback callback,
      core_service::mojom::CoreBootstrapResultPtr result,
      CoreStatePublication publication,
      core_service::mojom::PendingApprovalRegistrationStatus status);
  void OnSubmitted(SubmitCallback callback,
                   core_service::mojom::CoreServiceCommandKind kind,
                   CoreResponseBatch batch);
  void OnEntitlementFetchDelivered(
      DeliverEntitlementFetchResultCallback callback,
      CoreEntitlementDeliveryBatch delivered);
  void OnPolicyEvaluated(EvaluatePolicyCallback callback,
                         core_service::mojom::PolicyEvaluationResultPtr result);
  void OnCapabilityRegistered(
      EvaluatePolicyCallback callback,
      core_service::mojom::PolicyEvaluationResultPtr result,
      core_service::mojom::CapabilityRegistrationStatus status);
  void PublishBatch(CoreResponseBatch batch);
  void DrainPublicationBatches();
  void PublishBatchContents();
  void OnTaskAnswerEventsPublished(bool accepted);
  void RegisterNextState();
  void OnStateBindingsRegistered(
      core_service::mojom::PendingApprovalRegistrationStatus status);
  void DispatchNextTaskEffect();
  void DispatchReadySourceReads();
  bool PrepareTaskEffectDispatch();
  void OnParallelSourceReadExecuted(
      core_service::mojom::TaskEffectBindingPtr effect,
      core_service::mojom::TaskEffectCompletionPtr completion);
  core_service::mojom::TaskEffectCompletionPtr ActivateNextSourceRead();
  void SettleNextSourceRead();
  uint64_t SourceReadDeadline(uint64_t deadline) const;
  void ExpireSourceReads(uint64_t now);
  bool RememberTaskRevisions(
      const core_service::mojom::CoreStateBrowserBindings& bindings);
  void OnTaskPolicyEvaluated(
      core_service::mojom::TaskEffectBindingPtr effect,
      core_service::mojom::PolicyEvaluationResultPtr result);
  void OnTaskEffectExecuted(
      core_service::mojom::TaskEffectBindingPtr effect,
      core_service::mojom::TaskEffectCompletionPtr completion);
  void OnTaskEffectCompletionSubmitted(CoreResponseBatch batch);
  // Why the utility answers for the active task effect itself.
  enum class TaskEffectSynthesis { kRefusedCompletion, kDeadline };
  void SynthesizeTaskEffectCompletion(TaskEffectSynthesis why);
  bool IsLateTaskEffectReply(
      const core_service::mojom::TaskEffectBinding& effect) const;
  void OnEffectResultDelivered(EffectIdentity identity,
                               bool is_storage_result,
                               CoreResponseBatch batch);
  void OnModelStreamChunkDelivered(DeliverModelStreamChunkCallback callback,
                                   CoreModelStreamDelivery delivery);
  void OnModelStreamAnswerEventsPublished(
      DeliverModelStreamChunkCallback callback,
      core_service::mojom::ModelStreamChunkStatus status,
      bool accepted);
  bool SetPendingTaskEffectCommit(
      const core_service::mojom::EffectEnvelope& effect);
  bool IsPendingTaskEffectCommit(const EffectIdentity& identity) const;
  bool AcknowledgeCommittedTaskEffect(
      bool acknowledges_task_effect_commit,
      const core_service::mojom::CoreStateBrowserBindings& bindings);
  void RefreshDeadlineSchedule();
  void StopDeadlineSchedule();
  void OnDeadlineRead(uint64_t deadline_monotonic_ms);
  void OnDeadlineExpired();
  void FailStatePublication(const char* reason);
  void OnShutdownPrepared(PrepareForShutdownCallback callback,
                          CoreResponseBatch batch);
  core_service::mojom::AdmissionPtr UnavailableAdmission(
      const core_service::mojom::CoreServiceCommand* command) const;
  core_service::mojom::AdmissionPtr BackpressureAdmission(
      const core_service::mojom::CoreServiceCommand* command) const;

  mojo::Receiver<core_service::mojom::TaffyCoreService> receiver_;
  mojo::ReceiverSet<core_service::mojom::CoreSession> sessions_;
  mojo::Remote<core_service::mojom::CoreHost> host_;
  base::SequenceBound<RustCore> core_;
  uint64_t generation_ = 0;
  bool initialize_in_flight_ = false;
  bool ready_ = false;
  // The acknowledged bootstrap state stays in the bounded queue until the
  // browser opens its session and can execute the effects it carries.
  bool initial_publication_pending_ = false;
  bool state_registration_in_flight_ = false;
  bool shutdown_started_ = false;
  CorePublicationQueue publication_queue_;
  core_service::mojom::TaskEffectBindingPtr active_task_effect_;
  struct ParallelSourceRead {
    core_service::mojom::TaskEffectBindingPtr effect;
    core_service::mojom::TaskEffectCompletionPtr completion;
    bool synthesized = false;
  };
  // Browser reads overlap; their terminal commands still commit one at a
  // time, in dispatch order, through active_task_effect_.
  std::deque<ParallelSourceRead> parallel_source_reads_;
  std::map<std::string, uint64_t> published_task_revisions_;
  std::optional<uint64_t> active_source_read_revision_;
  // Whether the utility has already spoken once for the active effect — for
  // a completion the bridge refused, or for a deadline that arrived with no
  // answer. One answer per effect: the single-slot active effect re-enters
  // the same completion path, and a second refusal ends the core.
  bool task_effect_completion_synthesized_ = false;
  std::optional<PendingTaskEffectCommit> task_effect_completion_commit_;
  bool answer_publication_in_flight_ = false;
  CoreDeadlineScheduler deadline_scheduler_;
  bool deadline_read_in_flight_ = false;
  bool deadline_refresh_requested_ = false;
  // Streamed tool chunks that were addressed to this generation and had
  // nowhere to go. core_service_impl_tool_stream.cc holds the argument for why
  // there is nowhere yet, and why zero is the only value this should hold.
  uint64_t unrouted_tool_stream_chunk_count_ = 0;

  base::WeakPtrFactory<CoreServiceImpl> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_H_
