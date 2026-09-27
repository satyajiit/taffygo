// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_LIFECYCLE_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_LIFECYCLE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/token.h"
#include "base/types/expected.h"
#include "taffy/browser/android/browser_profiles_restore_discovery.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"
#include "taffy/browser/profile_backup_restore_target.h"

class PrefService;
class Profile;
class ProfileManager;

namespace taffy {

namespace storage::backup {
enum class DormantBackupRestoreReconcileError;
}

class ProfileBackupPrecommitCancellationReceipt;
class BackupRestoreRestartDiscoveryOperation;
struct OwnedPrecommitBackupRestoreReservation;

enum class BackupRestoreProfileReserveError : uint8_t {
  kInvalidArgument,
  kUnavailable,
  kBusy,
  kLimitReached,
  kDuplicateName,
  kTargetPathOccupied,
  kRegistryRefused,
  kPersistenceFailed,
  kCreationFailed,
  kCancelled,
};

struct ReservedBackupRestoreProfile {
  std::string reservation_id;
  base::FilePath target_profile_path;

  bool operator==(const ReservedBackupRestoreProfile&) const = default;
};

// A process-local physical-custody handle minted only after the target UUID's
// Local State write has an exact synchronized disk witness. It is move-only,
// one-use, and accepted only by the lifecycle instance that minted it. This is
// not portable restore authority and carries neither a path nor a target UUID.
class BoundBackupRestoreProfileHandle {
 public:
  BoundBackupRestoreProfileHandle(
      BoundBackupRestoreProfileHandle&& other) noexcept;
  BoundBackupRestoreProfileHandle& operator=(
      BoundBackupRestoreProfileHandle&& other) noexcept;
  BoundBackupRestoreProfileHandle(const BoundBackupRestoreProfileHandle&) =
      delete;
  BoundBackupRestoreProfileHandle& operator=(
      const BoundBackupRestoreProfileHandle&) = delete;
  ~BoundBackupRestoreProfileHandle();

 private:
  friend class BrowserProfilesRestoreLifecycle;

  BoundBackupRestoreProfileHandle(base::Token lifecycle_instance,
                                  std::string reservation_id);

  base::Token lifecycle_instance_;
  std::string reservation_id_;
};

enum class BackupRestoreDormantTargetError : uint8_t {
  kInvalidHandle,
  kBusy,
  kRegistryRefused,
  kPersistenceFailed,
  kProfileStateRefused,
  kCoreAlreadyStarted,
  kTargetOccupied,
  kStorageUnavailable,
  kCancelled,
};

enum class BackupRestoreCommitReconciliationError : uint8_t {
  kInvalidArgument,
  kBusy,
  kRegistryRefused,
  kPersistenceFailed,
  kProfileStateRefused,
  kCoreUnavailable,
  kHistoryRefused,
  kSchemaMismatch,
  kStorageUnavailable,
};

enum class BackupRestoreCandidateResolutionError : uint8_t {
  kInvalidArgument,
  kBusy,
  kRegistryRefused,
  kPersistenceFailed,
  kProfileStateRefused,
  kCoreUnavailable,
  kHistoryRefused,
  kStorageUnavailable,
};

struct BackupRestoreCommitReconciliation {
  core_service::mojom::BackupRestoreRecoveryClassificationPtr classification;
  // Present only when the source Core required a read-only physical
  // reconciliation. A terminal journal classification does not claim that
  // target SQL was reopened or freshly verified.
  std::optional<ProfileBackupRestoreCommitObservation> physical_observation;
};

struct BackupRestoreCandidateResolution {
  core_service::mojom::BackupRestoreRecoveryClassificationPtr classification;
  // True only after the authorized physical linearization point was crossed.
  // It never implies completion; an ambiguous terminal remains quarantined.
  bool physical_action_dispatched = false;
  // True only after terminal Core classification, exact physical validation,
  // synchronized registry absence, and release of the retirement fence.
  bool reservation_retired = false;
};

// Android's physical new-profile reservation. The resulting Profile remains
// omitted, ephemeral, unselected, and quarantined from Taffy's live Core. On
// Android, ephemeral profiles are not automatically deleted when their last
// keepalive is removed; later deletion still requires the portable Core
// resolution authorization and verified physical custody.
//
// Candidate resolution consumes a fresh source-Core authorization and keeps
// publication/deletion hidden behind durable intent and outcome barriers. It
// never activates a restored profile; selection remains a separate UI action.
class BrowserProfilesRestoreLifecycle {
 public:
  using ReserveResult = base::expected<ReservedBackupRestoreProfile,
                                       BackupRestoreProfileReserveError>;
  using ReserveCallback = base::OnceCallback<void(ReserveResult)>;
  using BindResult = base::expected<BoundBackupRestoreProfileHandle,
                                    BackupRestoreProfileReserveError>;
  using BindCallback = base::OnceCallback<void(BindResult)>;
  using DormantTargetResult =
      base::expected<InitializedDormantBackupRestoreTarget,
                     BackupRestoreDormantTargetError>;
  using DormantTargetCallback = base::OnceCallback<void(DormantTargetResult)>;
  using RecoveryPresentationCallback =
      base::OnceCallback<void(BackupRestoreRecoveryPresentationResult)>;
  using RestartDiscoveryCallback =
      base::OnceCallback<void(BackupRestoreRestartDiscoveryResult)>;
  using CommitReconciliationResult =
      base::expected<BackupRestoreCommitReconciliation,
                     BackupRestoreCommitReconciliationError>;
  using CommitReconciliationCallback =
      base::OnceCallback<void(CommitReconciliationResult)>;
  using CandidateResolutionResult =
      base::expected<BackupRestoreCandidateResolution,
                     BackupRestoreCandidateResolutionError>;
  using CandidateResolutionCallback =
      base::OnceCallback<void(CandidateResolutionResult)>;
  using PrecommitCleanupResult =
      base::expected<void, BackupRestorePrecommitCleanupError>;
  using PrecommitCleanupCallback =
      base::OnceCallback<void(PrecommitCleanupResult)>;

  BrowserProfilesRestoreLifecycle(ProfileManager* profile_manager,
                                  PrefService* local_state);
  BrowserProfilesRestoreLifecycle(const BrowserProfilesRestoreLifecycle&) =
      delete;
  BrowserProfilesRestoreLifecycle& operator=(
      const BrowserProfilesRestoreLifecycle&) = delete;
  ~BrowserProfilesRestoreLifecycle();

  // Reserves the next Chromium-generated profile path for one active regular
  // source profile. Completion means both the quarantine record and hidden
  // Chromium profile metadata have exact synchronized disk witnesses, and
  // profile initialization finished without ever activating the target.
  void Reserve(Profile* source_profile,
               std::u16string display_name,
               ReserveCallback callback);

  // Durably binds the caller-generated dormant Core identity to an already
  // created reservation. Completion is an exact synchronized Local State disk
  // witness, not merely completion of PrefService's write queue. This remains
  // physical custody metadata and grants no restore authority.
  void BindTargetProfileId(std::string reservation_id,
                           std::string target_profile_id,
                           BindCallback callback);

  // Revalidates and disk-witnesses the exact bound reservation, hidden profile
  // metadata and Core quarantine before opening schema-only dormant storage.
  // Success transfers one exclusive target owner whose SQL state remains on a
  // blocking sequence. Destroying that owner withdraws callbacks and closes
  // SQL on its sequence; it never deletes the retained candidate. The owner
  // can consume source-Core staging authority, but successful initialization
  // alone grants no stage or commit authority.
  void InitializeDormantTarget(BoundBackupRestoreProfileHandle handle,
                               DormantTargetCallback callback);

  // Attaches the exact content-free plan presentation to this lifecycle's
  // already-transferred dormant target. Completion follows a Local State
  // queue drain, exact file readback, file sync and parent-directory sync.
  // Failure exposes no review state and retains quarantine custody.
  void PersistRecoveryPresentation(
      std::string reservation_id,
      std::u16string target_profile_label,
      std::vector<core_service::mojom::BackupRecordKind> original_selection,
      core_service::mojom::BackupRestorePlanResultPtr exact_plan,
      RecoveryPresentationCallback callback);

  // Cancels only this lifecycle incarnation's reservation before the dormant
  // target is transferred. In-flight creation/binding/initialization first
  // reaches a stable checkpoint; any initialized storage owner is then closed
  // before exact physical deletion begins. No portable receipt exists or is
  // required because no target or plan custody has left this lifecycle.
  void CancelPendingNewReservation(PrecommitCleanupCallback callback);

  // Consumes both the exact creation-incarnation capability and a coordinator
  // receipt that can be minted only after portable cancellation, isolated
  // stage cleanup, and target-owner destruction have all completed before any
  // commit authority was requested. The implementation independently proves
  // an empty recovery journal, deletes only the exact hidden profile and its
  // derived cache, verifies their durable absence, then retires quarantine.
  // Any ambiguous physical or persistence result retains quarantine for
  // recovery and never replays deletion.
  void CleanupNewPrecommitReservation(
      PrecommitBackupRestoreCleanupHandle handle,
      ProfileBackupPrecommitCancellationReceipt receipt,
      PrecommitCleanupCallback callback);

  // Discovers the sole durable reservation for this exact active source. It
  // discloses no saved label before source matching, reconstructs no prior
  // operation or authority, and never chooses a resolution. Commit SQL is
  // opened read-only only in kReconcileCommit mode. A returned candidate is a
  // fresh native projection and exists only for an exact hidden/coreless
  // RollbackAvailable or discard-only CleanupRequired state.
  void DiscoverInterruptedBackupRestore(Profile* source_profile,
                                        BackupRestoreRestartDiscoveryMode mode,
                                        RestartDiscoveryCallback callback);

  // Re-enters from durable browser custody after a process interruption. It
  // never creates or activates the target Profile, starts its Core, restores
  // authority, or exposes a mutable target owner. The current source Core
  // first classifies the content-free journal; only an exact outstanding
  // CommitCandidate intent permits read-only target reconciliation. Any
  // completed physical observation is durably appended before the updated
  // journal is classified again. Lease, schema, custody and SQL failures stay
  // typed and append no synthetic unknown observation. Every failure leaves
  // the target quarantined.
  void ReconcileInterruptedCommit(std::string reservation_id,
                                  CommitReconciliationCallback callback);

  // Resolves one durably committed hidden candidate using a fresh source-Core
  // recovery authorization. Accept re-verifies the exact candidate, removes
  // only its retained stage, and synchronizes regular profile metadata.
  // Discard synchronizes an exact deletion marker before deleting only the
  // reserved target and derived cache. Unknown outcomes stay quarantined and
  // are observed on re-entry; they are never physically replayed.
  void ResolveBackupRestoreCandidate(
      std::string reservation_id,
      core_service::mojom::BackupRestoreResolutionChoice choice,
      CandidateResolutionCallback callback);

  bool operation_in_flight() const {
    return pending_ != nullptr || pending_binding_ != nullptr ||
           pending_dormant_target_ != nullptr ||
           pending_recovery_presentation_ != nullptr ||
           restart_discovery_ != nullptr ||
           pending_commit_reconciliation_ != nullptr ||
           pending_candidate_resolution_ != nullptr ||
           pending_precommit_cleanup_ != nullptr ||
           pending_pretransfer_cleanup_callback_ || dormant_target_transferred_;
  }

 private:
  friend class BrowserProfilesRestoreLifecyclePrecommitTestPeer;

  using CommitReconciliationStorageResult =
      base::expected<core_service::mojom::BackupRestoreCommitOutcome,
                     storage::backup::DormantBackupRestoreReconcileError>;

  struct PendingReservation;
  struct PendingBinding;
  struct PendingDormantTarget;
  struct PendingRecoveryPresentation;
  struct PendingCommitReconciliation;
  struct PendingCandidateResolution;
  struct PendingPrecommitCleanup;
  class DormantTargetState;
  class CommitReconciliationBlockingOwner;
  class CandidateResolutionBlockingOwner;
  class PrecommitCleanupBlockingOwner;

  void OnInitialPathCheckComplete(bool path_exists);
  void OnQuarantineWriteDrained();
  void OnQuarantineReadBack(bool matches);
  void OnHiddenMetadataWriteDrained();
  void OnHiddenMetadataReadBack(bool matches);
  void OnReservedPathCheckComplete(bool path_exists);
  void OnProfileCreated(Profile* profile);
  void OnProfilePreferencesWriteDrained();
  void OnProfilePreferencesReadBack(bool matches);
  void OnCreatedStateWriteDrained();
  void OnCreatedStateReadBack(bool matches);
  void OnBindingWriteDrained();
  void OnBindingReadBack(bool matches);
  void OnDormantTargetReadBack(bool matches);
  void OnDormantTargetInitialized(
      base::expected<void, BackupRestoreDormantTargetError> result);
  void OnDormantTargetPostReadBack(bool matches);
  void OnRecoveryPresentationWriteDrained();
  void OnRecoveryPresentationReadBack(bool matches);
  void FinishRecoveryPresentation(
      BackupRestoreRecoveryPresentationResult result);
  void BeginPrecommitCleanupReadBack();
  void Finish(ReserveResult result);
  void FinishBinding(BindResult result);
  void FinishDormantTarget(DormantTargetResult result);
  void BeginOwnedPrecommitCleanup();
  void OnPendingDormantTargetClosedForCleanup(bool closed);
  void OnPrecommitCleanupReadBack(bool matches);
  void OnPrecommitCleanupStoragePrepared(
      base::expected<void, BackupRestorePrecommitCleanupError> result);
  void OnPrecommitCleanupDeletionMarkerWriteDrained();
  void OnPrecommitCleanupDeletionMarkerReadBack(bool matches);
  void OnPrecommitCleanupDeleted(bool deleted);
  void OnPrecommitCleanupDeletionVerified(bool verified);
  void OnPrecommitCleanupStorageClosed(bool closed);
  void OnPrecommitCleanupRetirementWriteDrained();
  void OnPrecommitCleanupRetirementReadBack(bool matches);
  void FinishPrecommitCleanup(PrecommitCleanupResult result);
  void OnCommitRecoveryReadBack(bool matches);
  void OnCommitRecoveryInspected(
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result);
  void OnCommitRecoveryStorageObserved(
      CommitReconciliationStorageResult result);
  void OnCommitRecoveryOutcomeWriteDrained();
  void OnCommitRecoveryOutcomeReadBack(bool matches);
  void OnCommitRecoveryUpdatedInspected(
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result);
  void FinishCommitReconciliation(CommitReconciliationResult result);
  void OnCandidateResolutionReadBack(bool matches);
  void OnCandidateResolutionInspected(
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result);
  void OnCandidateResolutionStoragePrepared(
      base::expected<void, BackupRestoreCandidateResolutionError> result);
  void OnCandidateResolutionAuthorized(
      core_service::mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
          result);
  void OnCandidateResolutionIntentWriteDrained();
  void OnCandidateResolutionIntentReadBack(bool matches);
  void DispatchCandidateResolutionOrRecordRefusal();
  void OnCandidateAcceptStorageFinalized(bool finalized);
  void OnCandidateAcceptMetadataWriteDrained();
  void OnCandidateAcceptMetadataReadBack(bool matches);
  void OnCandidateDiscardMarkerWriteDrained();
  void OnCandidateDiscardMarkerReadBack(bool matches);
  void OnCandidateDiscardDeleted(bool deleted);
  void OnCandidateDiscardVerified(bool verified);
  void PersistCandidateResolutionOutcome(
      core_service::mojom::BackupRestoreResolutionOutcome outcome);
  void OnCandidateResolutionOutcomeWriteDrained();
  void OnCandidateResolutionOutcomeReadBack(bool matches);
  void OnCandidateResolutionOutcomeReported(
      core_service::mojom::BackupRestoreProtocolResultPtr result);
  void InspectCandidateResolutionTerminal();
  void OnCandidateResolutionTerminalInspected(
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result);
  void BeginCandidateResolutionRetirement();
  void OnCandidateResolutionPhysicalRetirementChecked(bool valid);
  void OnCandidateResolutionPhysicalCustodyReleased(bool released);
  void OnCandidateResolutionRetirementWriteDrained();
  void OnCandidateResolutionRetirementReadBack(bool matches);
  void FinishCandidateResolution(CandidateResolutionResult result);
  void OnCandidateResolutionOwnerClosed(bool closed);

  const raw_ptr<ProfileManager> profile_manager_;
  const raw_ptr<PrefService> local_state_;
  std::unique_ptr<PendingReservation> pending_;
  std::unique_ptr<PendingBinding> pending_binding_;
  std::unique_ptr<PendingDormantTarget> pending_dormant_target_;
  std::unique_ptr<PendingRecoveryPresentation> pending_recovery_presentation_;
  std::unique_ptr<BackupRestoreRestartDiscoveryOperation> restart_discovery_;
  std::unique_ptr<PendingCommitReconciliation> pending_commit_reconciliation_;
  std::unique_ptr<PendingCandidateResolution> pending_candidate_resolution_;
  std::unique_ptr<PendingPrecommitCleanup> pending_precommit_cleanup_;
  std::unique_ptr<OwnedPrecommitBackupRestoreReservation>
      owned_precommit_reservation_;
  PrecommitCleanupCallback pending_pretransfer_cleanup_callback_;
  std::unique_ptr<DormantTargetState> dormant_target_state_;
  bool dormant_target_transferred_ = false;
  const base::Token handle_lifecycle_instance_ = base::Token::CreateRandom();
  base::WeakPtrFactory<BrowserProfilesRestoreLifecycle> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_LIFECYCLE_H_
