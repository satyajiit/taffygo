// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_backup_recovery_validation.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace {

// `taffy::mojom` -- the BIP page-semantics contract -- is also visible in
// this translation unit, so a plain `mojom` alias would be ambiguous at
// `taffy` scope below. The core-service contract is reached by this name.
namespace core_mojom = core_service::mojom;
namespace resolution = restore_resolution_internal;
using Error = BackupRestoreCandidateResolutionError;

BackupRestoreRecoveryRecords CloneHistory(
    const BackupRestoreRecoveryRecords& records) {
  BackupRestoreRecoveryRecords copy;
  copy.reserve(records.size());
  for (const auto& record : records) {
    copy.push_back(record.Clone());
  }
  return copy;
}

CoreServiceManager* LiveManager(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const ResolvedDormantBackupRestoreTarget& target,
    const BackupRestoreProfileReservation& reservation,
    const BackupRestoreRecoveryRecords& records) {
  if (!resolution::ExactCurrentState(profile_manager, local_state, target,
                                     reservation, records)) {
    return nullptr;
  }
  return restore_reconciliation_internal::LiveSourceManager(profile_manager,
                                                            target, records);
}

}  // namespace

void BrowserProfilesRestoreLifecycle::PersistCandidateResolutionOutcome(
    core_mojom::BackupRestoreResolutionOutcome outcome) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_ ||
      !pending_candidate_resolution_->intent) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  auto record = RecordBackupRestoreResolutionOutcome(
      local_state_, pending.target.reservation_id, *pending.intent, outcome);
  // `base::expected` disables `operator bool` when the value type is itself
  // bool-convertible, which a mojo StructPtr is; ask for the value directly.
  if (!record.has_value()) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  pending.outcome_record = std::move(*record);
  auto records = ReadBackupRestoreRecoveryJournal(
      local_state_, pending.target.reservation_id);
  if (!records || records->empty() || !records->back() ||
      !records->back()->Equals(*pending.outcome_record)) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  pending.records = std::move(*records);
  pending.persistence_witnesses = resolution::CaptureResolutionWitnesses(
      *local_state_, pending.target.target_profile_path, false);
  if (pending.persistence_witnesses.empty()) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  local_state_->CommitPendingWrite(
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnCandidateResolutionOutcomeWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::
    OnCandidateResolutionOutcomeWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto witnesses =
      std::move(pending_candidate_resolution_->persistence_witnesses);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(witnesses),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnCandidateResolutionOutcomeReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionOutcomeReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  auto records = ReadBackupRestoreRecoveryJournal(
      local_state_, pending.target.reservation_id);
  if (!matches || !records || records->empty() || !pending.outcome_record ||
      !records->back() || !records->back()->Equals(*pending.outcome_record)) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  pending.records = std::move(*records);
  CoreServiceManager* const manager =
      LiveManager(profile_manager_, local_state_, pending.target,
                  pending.reservation, pending.records);
  if (!manager) {
    FinishCandidateResolution(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  if (!pending.authorization ||
      !IsLiveBackupRestoreRecoveryResolutionAuthorization(
          pending.authorization.get(), manager->service_generation(),
          BackupPlanningNowMonotonicMillis(), manager->browser_profile_id())) {
    // A restarted source Core has no in-memory consumptive authorization to
    // echo. The durable history is still classified through the read-only
    // recovery operation; no authority is reconstructed from it.
    InspectCandidateResolutionTerminal();
    return;
  }
  pending.operation =
      resolution::NewOperation(manager->service_generation(), "report");
  if (!pending.operation) {
    FinishCandidateResolution(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  manager->backup_protocol().ReportBackupRestoreRecoveryResolutionOutcome(
      core_mojom::BackupRestoreRecoveryResolutionOutcomeReport::New(
          pending.operation.Clone(), pending.authorization.Clone(),
          CloneHistory(pending.records)),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnCandidateResolutionOutcomeReported,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionOutcomeReported(
    core_mojom::BackupRestoreProtocolResultPtr result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  if (!result || !pending_candidate_resolution_->operation ||
      !result->operation ||
      !result->operation->Equals(*pending_candidate_resolution_->operation) ||
      result->status != core_mojom::BackupRestoreProtocolStatus::kSucceeded) {
    // The physical terminal and its exact journal suffix are already durable.
    // A lost/expired consumptive report may not turn that into failure or
    // authorize replay; reclassify the retained facts read-only instead.
    InspectCandidateResolutionTerminal();
    return;
  }
  InspectCandidateResolutionTerminal();
}

void BrowserProfilesRestoreLifecycle::InspectCandidateResolutionTerminal() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  CoreServiceManager* const manager =
      LiveManager(profile_manager_, local_state_, pending.target,
                  pending.reservation, pending.records);
  pending.operation = manager ? resolution::NewOperation(
                                    manager->service_generation(), "terminal")
                              : nullptr;
  if (!manager || !pending.operation) {
    FinishCandidateResolution(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  manager->backup_protocol().InspectBackupRestoreRecovery(
      core_mojom::BackupRestoreRecoveryInspectionRequest::New(
          pending.operation.Clone(), CloneHistory(pending.records)),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnCandidateResolutionTerminalInspected,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionTerminalInspected(
    core_mojom::BackupRestoreRecoveryInspectionResultPtr result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  if (!restore_reconciliation_internal::ValidInspectionResult(
          pending.operation.get(), result.get()) ||
      !result->classification ||
      !resolution::ExactCurrentState(profile_manager_, local_state_,
                                     pending.target, pending.reservation,
                                     pending.records)) {
    FinishCandidateResolution(base::unexpected(Error::kHistoryRefused));
    return;
  }
  pending.classification = std::move(result->classification);
  using Choice = core_mojom::BackupRestoreResolutionChoice;
  using Kind = core_mojom::BackupRestoreRecoveryClassificationKind;
  const bool terminal =
      (pending.choice == Choice::kAcceptCandidate &&
       pending.classification->kind == Kind::kPublished) ||
      (pending.choice == Choice::kDiscardCandidate &&
       pending.classification->kind == Kind::kVerifiedDeleted);
  if (terminal) {
    BeginCandidateResolutionRetirement();
    return;
  }
  FinishCandidateResolution(BackupRestoreCandidateResolution{
      .classification = pending.classification.Clone(),
      .physical_action_dispatched = pending.physical_action_dispatched,
      .reservation_retired = false,
  });
}

}  // namespace taffy
