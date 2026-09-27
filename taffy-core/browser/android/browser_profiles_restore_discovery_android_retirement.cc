// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_discovery_android_internal.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace discovery = restore_discovery_internal;
namespace resolution = restore_resolution_internal;
using Error = BackupRestoreRestartDiscoveryError;
using PhysicalResult = BackupRestoreRestartPhysicalResult;
using PhysicalState = BackupRestoreRestartPhysicalState;
using Result = BackupRestoreRestartDiscoveryResult;
using Status = BackupRestoreRestartDiscoveryStatus;
using Witness = BackupRestorePreferenceWriteWitness;

Result StatusOnly(Status status) {
  return BackupRestoreRestartDiscovery{.status = status};
}

std::vector<Witness> CaptureRetirementWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_path) {
  const std::string target_key = target_path.BaseName().AsUTF8Unsafe();
  const base::Value* target_attributes =
      local_state.GetDict(prefs::kProfileAttributes).Find(target_key);
  Witness attributes{.dotted_path = base::StrCat(
                         {prefs::kProfileAttributes, ".", target_key})};
  if (target_attributes) {
    attributes.expected = target_attributes->Clone();
  } else {
    attributes.must_be_absent = true;
  }
  std::vector<Witness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .must_be_absent = true});
  witnesses.push_back(std::move(attributes));
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesOrder,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesOrder).Clone())});
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesDeleted,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesDeleted).Clone())});
  return witnesses;
}

}  // namespace

void BackupRestoreRestartDiscoveryOperation::BeginTerminalVerification(
    Status terminal_status,
    mojom::BackupRestoreResolutionChoice choice) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if ((terminal_status != Status::kPublished &&
       terminal_status != Status::kVerifiedDeleted) ||
      !classification_ || !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  terminal_status_ = terminal_status;
  blocking_owner_.AsyncCall(&BlockingOwner::ObserveResolution)
      .WithArgs(target_.target_profile_path,
                mojom::BackupRestoreTarget::New(
                    mojom::BackupRestoreTargetKind::kNewRegularProfile,
                    target_.target_profile_id),
                choice)
      .Then(base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnTerminalPhysicalProbe,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnTerminalPhysicalProbe(
    PhysicalResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!result) {
    Finish(discovery::ProjectPhysicalFailure(result.error()));
    return;
  }
  const bool published = terminal_status_ == Status::kPublished &&
                         *result == PhysicalState::kSchemaExact &&
                         TargetIsPublishedAndCoreless();
  const bool deleted = terminal_status_ == Status::kVerifiedDeleted &&
                       *result == PhysicalState::kVerifiedDeleted &&
                       TargetIsVerifiedDeleted();
  if ((!published && !deleted) || !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  blocking_owner_.AsyncCall(&BlockingOwner::Close)
      .Then(base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnTerminalOwnerClosed,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnTerminalOwnerClosed(
    bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  blocking_owner_closed_ = true;
  if (!closed) {
    Finish(base::unexpected(Error::kStorageUnavailable));
    return;
  }
  BeginTerminalRetirement();
}

void BackupRestoreRestartDiscoveryOperation::BeginTerminalRetirement() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const bool physical_shape =
      terminal_status_ == Status::kPublished
          ? TargetIsPublishedAndCoreless()
          : terminal_status_ == Status::kVerifiedDeleted &&
                TargetIsVerifiedDeleted();
  if (!terminal_status_ || !classification_ || !physical_shape ||
      !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  auto retirement = BackupRestoreReservationRetirement::Begin(
      local_state_, reservation_, records_, *classification_);
  if (!retirement.has_value()) {
    Finish(retirement.error() == BackupRestoreProfileRegistryError::kUnavailable
               ? Result(base::unexpected(Error::kStorageUnavailable))
               : StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  retirement_ = std::move(*retirement);
  persistence_witnesses_ =
      CaptureRetirementWitnesses(*local_state_, target_.target_profile_path);
  local_state_->CommitPendingWrite(base::BindOnce(
      &BackupRestoreRestartDiscoveryOperation::OnRetirementWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnRetirementWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(persistence_witnesses_),
      base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnRetirementReadBack,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnRetirementReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!matches || !retirement_ || !terminal_status_) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  const auto choice =
      *terminal_status_ == Status::kPublished
          ? mojom::BackupRestoreResolutionChoice::kAcceptCandidate
          : mojom::BackupRestoreResolutionChoice::kDiscardCandidate;
  blocking_owner_closed_ = false;
  blocking_owner_.AsyncCall(&BlockingOwner::ObserveResolution)
      .WithArgs(target_.target_profile_path,
                mojom::BackupRestoreTarget::New(
                    mojom::BackupRestoreTargetKind::kNewRegularProfile,
                    target_.target_profile_id),
                choice)
      .Then(base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnRetirementPhysicalProbe,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnRetirementPhysicalProbe(
    PhysicalResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const bool published = result && terminal_status_ == Status::kPublished &&
                         *result == PhysicalState::kSchemaExact &&
                         TargetIsPublishedAndCoreless();
  const bool deleted = result && terminal_status_ == Status::kVerifiedDeleted &&
                       *result == PhysicalState::kVerifiedDeleted &&
                       TargetIsVerifiedDeleted();
  if (!result || (!published && !deleted)) {
    Finish(!result ? discovery::ProjectPhysicalFailure(result.error())
                   : StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  blocking_owner_.AsyncCall(&BlockingOwner::Close)
      .Then(base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnRetirementOwnerClosed,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnRetirementOwnerClosed(
    bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  blocking_owner_closed_ = true;
  if (!closed || !retirement_ || !terminal_status_ ||
      !retirement_->ReleaseAfterVerifiedAbsence()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  Finish(StatusOnly(*terminal_status_));
}

void BackupRestoreRestartDiscoveryOperation::Finish(Result result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (finish_result_) {
    return;
  }
  finish_result_.emplace(std::move(result));
  if (blocking_owner_closed_) {
    DeliverFinished();
    return;
  }
  blocking_owner_.AsyncCall(&BlockingOwner::Close)
      .Then(base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnFinishOwnerClosed,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnFinishOwnerClosed(bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!finish_result_) {
    return;
  }
  blocking_owner_closed_ = true;
  if (!closed) {
    finish_result_.emplace(base::unexpected(Error::kStorageUnavailable));
  }
  DeliverFinished();
}

void BackupRestoreRestartDiscoveryOperation::DeliverFinished() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!completion_ || !finish_result_) {
    return;
  }
  CompletionCallback completion = std::move(completion_);
  Result result = std::move(*finish_result_);
  finish_result_.reset();
  // Completion clears and destroys this operation. Post it so no member call
  // can delete its own receiver while a callback frame is still active.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(completion), std::move(result)));
}

}  // namespace taffy
