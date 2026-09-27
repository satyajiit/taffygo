// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge_cxx.h"

namespace taffy {

// One generated state plus the browser/service-private approval bindings that
// must be acknowledged before the state can be published.
struct CoreStatePublication {
  CoreStatePublication();
  CoreStatePublication(CoreStatePublication&&);
  CoreStatePublication& operator=(CoreStatePublication&&);
  ~CoreStatePublication();

  core_service::mojom::CoreStateUpdatePtr state;
  core_service::mojom::CoreStateBrowserBindingsPtr browser_bindings;
  std::vector<core_service::mojom::EffectEnvelopePtr> effects;
  std::vector<core_service::mojom::TaskEffectBindingPtr> task_effects;
  // Set only by CoreServiceImpl after the exact browser storage terminal for
  // an active task effect returns this state. Rust does not guess causation
  // from state order or task revision; the utility transport carries the
  // private marker through its publication queue.
  bool acknowledges_task_effect_commit = false;
};

struct CoreInitializationBatch {
  CoreInitializationBatch();
  CoreInitializationBatch(CoreInitializationBatch&&);
  CoreInitializationBatch& operator=(CoreInitializationBatch&&);
  ~CoreInitializationBatch();

  core_service::mojom::CoreBootstrapResultPtr result;
  std::vector<CoreStatePublication> states;
};

// Results moved from the ordered Rust sequence back to the utility main
// sequence. Every vector is bounded by the Rust runtime before it crosses CXX.
struct CoreResponseBatch {
  CoreResponseBatch();
  CoreResponseBatch(CoreResponseBatch&&);
  CoreResponseBatch& operator=(CoreResponseBatch&&);
  ~CoreResponseBatch();

  core_service::mojom::AdmissionPtr admission;
  std::vector<core_service::mojom::EffectEnvelopePtr> effects;
  std::vector<CoreStatePublication> states;
  std::vector<core_service::mojom::TaskAnswerEventPtr> task_answer_events;
  // An effect already dispatched that a newer request displaced, which the
  // caller must stop; empty when nothing was displaced. Only the composer's
  // single-flight suggestion plane sets it (decision 0097): every other
  // plane's outstanding work is either durable or already terminal, so there
  // is nothing to withdraw. It is named rather than dropped because an effect
  // nobody stops is an effect that still bills.
  std::string superseded_effect_id;
};

// One synchronous backpressure answer from the isolated incremental model
// reader. The events contain sanitized visible text only.
struct CoreModelStreamDelivery {
  CoreModelStreamDelivery();
  CoreModelStreamDelivery(CoreModelStreamDelivery&&);
  CoreModelStreamDelivery& operator=(CoreModelStreamDelivery&&);
  ~CoreModelStreamDelivery();

  core_service::mojom::ModelStreamChunkStatus status =
      core_service::mojom::ModelStreamChunkStatus::kUnavailable;
  std::vector<core_service::mojom::TaskAnswerEventPtr> answer_events;
};

// One delivered entitlement fetch's answer (decision 0082). `installed` is
// bookkeeping for the caller — a summary was installed and the state beside
// it published; the surface-visible change rides that state, never this flag.
struct CoreEntitlementDeliveryBatch {
  CoreEntitlementDeliveryBatch();
  CoreEntitlementDeliveryBatch(CoreEntitlementDeliveryBatch&&);
  CoreEntitlementDeliveryBatch& operator=(CoreEntitlementDeliveryBatch&&);
  ~CoreEntitlementDeliveryBatch();

  bool installed = false;
  CoreResponseBatch batch;
};

// The only C++ owner of the canonical profile-scoped Rust composition. An
// instance is constructed, called, and destroyed on one Chromium sequenced
// runner through base::SequenceBound. It performs no I/O and owns no task
// runner, timer, browser object, path, credential, or capability ledger.
class RustCore {
 public:
  RustCore();
  RustCore(const RustCore&) = delete;
  RustCore& operator=(const RustCore&) = delete;
  ~RustCore();

  CoreInitializationBatch Initialize(
      core_service::mojom::CoreBootstrapPtr bootstrap);
  CoreResponseBatch Submit(core_service::mojom::CoreServiceCommandPtr command,
                           uint64_t now_monotonic_ms);
  core_service::mojom::AccountTokenValidationResultPtr
  ValidateAccountTokenResponse(
      core_service::mojom::AccountTokenValidationRequestPtr request,
      uint64_t expected_generation,
      uint64_t now_monotonic_ms);
  CoreResponseBatch CompleteTaskSettlement(
      core_service::mojom::TaskSettlementBindingPtr settlement,
      uint64_t now_monotonic_ms);
  core_service::mojom::PolicyEvaluationResultPtr EvaluatePolicy(
      core_service::mojom::PolicyEvaluationRequestPtr request);
  CoreResponseBatch CompleteTaskPolicy(
      core_service::mojom::TaskEffectBindingPtr effect,
      core_service::mojom::PolicyEvaluationResultPtr result,
      uint64_t now_monotonic_ms);
  CoreResponseBatch CompleteTaskEffect(
      core_service::mojom::TaskEffectBindingPtr effect,
      core_service::mojom::TaskEffectCompletionPtr completion,
      uint64_t now_monotonic_ms);
  CoreModelStreamDelivery DeliverModelStreamChunk(
      core_service::mojom::ModelStreamChunkPtr chunk);
  core_service::mojom::BackupManifestPrepareResultPtr PrepareBackupManifest(
      core_service::mojom::BackupManifestPrepareRequestPtr request,
      uint64_t expected_generation,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupManifestInspectResultPtr InspectBackupManifest(
      core_service::mojom::BackupManifestInspectRequestPtr request,
      uint64_t expected_generation,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestorePlanResultPtr PlanBackupRestore(
      core_service::mojom::BackupRestorePlanRequestPtr request,
      uint64_t expected_generation,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreStageAuthorizationResultPtr
  ConfirmBackupRestorePlan(
      core_service::mojom::BackupRestorePlanConfirmationRequestPtr request,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreCommitAuthorizationResultPtr
  ReportBackupRestoreStageVerified(
      core_service::mojom::BackupRestoreStageVerificationRequestPtr request,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreProtocolResultPtr
  ReportBackupRestoreCommitOutcome(
      core_service::mojom::BackupRestoreCommitOutcomeReportPtr report,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreResolutionAuthorizationResultPtr
  ChooseBackupRestoreResolution(
      core_service::mojom::BackupRestoreResolutionRequestPtr request,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreProtocolResultPtr
  ReportBackupRestoreResolutionOutcome(
      core_service::mojom::BackupRestoreResolutionOutcomeReportPtr report,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreProtocolResultPtr
  CancelBackupRestoreBeforeCommit(
      core_service::mojom::BackupRestoreCancellationRequestPtr request,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreRecoveryInspectionResultPtr
  InspectBackupRestoreRecovery(
      core_service::mojom::BackupRestoreRecoveryInspectionRequestPtr request,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
  ChooseBackupRestoreRecoveryResolution(
      core_service::mojom::BackupRestoreRecoveryResolutionRequestPtr request,
      uint64_t now_monotonic_ms);
  core_service::mojom::BackupRestoreProtocolResultPtr
  ReportBackupRestoreRecoveryResolutionOutcome(
      core_service::mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr
          report,
      uint64_t now_monotonic_ms);
  core_service::mojom::PageSnapshotExportResultPtr ExportPageSnapshot(
      core_service::mojom::PageSnapshotExportCommandPtr command,
      uint64_t now_monotonic_ms);
  bool CancelPageSnapshotExport(const std::string& operation_id,
                                const std::string& idempotency_key);
  core_service::mojom::SavedFlowQueryResultPtr QuerySavedFlows(
      core_service::mojom::SavedFlowQueryCommandPtr command, uint64_t now_monotonic_ms);
  core_service::mojom::SiteSkillMatchResultPtr MatchSiteSkills(
      core_service::mojom::SiteSkillMatchCommandPtr command,
      uint64_t now_monotonic_ms);
  CoreResponseBatch Cancel(core_service::mojom::OperationEnvelopePtr operation,
                           uint64_t now_monotonic_ms);
  // Resolves every operation whose browser-owned deadline has arrived. The
  // clock stays where every other clock in this file is: the caller reads
  // base::TimeTicks on the utility main sequence and hands the value in, so
  // this class still owns no timer and reads no time.
  CoreResponseBatch ExpireDueOperations(uint64_t now_monotonic_ms);
  // The exact earliest deadline in the Rust pending-operation table. Zero
  // means the table is empty; no second C++ count or deadline index exists.
  uint64_t NextOperationDeadline();
  CoreResponseBatch DeliverEffectResult(
      core_service::mojom::EffectResultPtr result,
      uint64_t now_monotonic_ms);
  // Asks whether a managed-entitlement fetch is warranted for `reason`. Null
  // means not warranted, or one mint already in flight; planning changes no
  // published state, so there is no batch to publish (decision 0082).
  core_service::mojom::EffectEnvelopePtr PlanEntitlementRefresh(
      core_service::mojom::EntitlementFetchReason reason);
  // Delivers what the mint observed and answers whether a summary was
  // installed, beside the state that installation published.
  CoreEntitlementDeliveryBatch DeliverEntitlementFetchResult(
      core_service::mojom::EffectResultPtr result);
  CoreResponseBatch PrepareForShutdown();

 private:
  class Bridge;
  // Passes one projected bridge answer through and, when it says the core is
  // unavailable, logs the content-free branch the bridge recorded for it. The
  // wire carries one status; this is the only place the reason crosses the
  // seam.
  CoreResponseBatch Answer(CoreResponseBatch batch, const char* site);
  std::unique_ptr<Bridge> bridge_;
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_H_
