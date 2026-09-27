// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_TARGET_ANDROID_INTERNAL_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_TARGET_ANDROID_INTERNAL_H_

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "taffy/browser/android/browser_profiles_restore_target_android.h"
#include "taffy/browser/backup_restore_preference_readback.h"

namespace taffy {

struct BrowserProfilesRestoreLifecycle::DormantTargetState::PendingStage {
  core_service::mojom::BackupRestorePlanResultPtr plan;
  core_service::mojom::BackupRestorePlanResultPtr retained_plan;
  core_service::mojom::BackupRestoreStageAuthorizationPtr authorization;
  base::File plaintext_payload;
  std::array<uint8_t, 32> expected_snapshot_sha256{};
  uint64_t expected_record_count = 0u;
  StageCallback callback;
  std::optional<StageExecutionResult> execution_result;
};

struct BrowserProfilesRestoreLifecycle::DormantTargetState::PendingCommit {
  core_service::mojom::BackupRestorePlanResultPtr plan;
  core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization;
  core_service::mojom::BackupRestoreCandidateWitnessPtr witness;
  core_service::mojom::BackupRestoreRecoveryRecordPtr intent;
  core_service::mojom::BackupRestoreRecoveryRecordPtr outcome_record;
  std::vector<BackupRestorePreferenceWriteWitness> persistence_witnesses;
  core_service::mojom::BackupRestoreCommitOutcome observed_outcome =
      core_service::mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
  CommitCallback callback;
  bool dispatched = false;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_TARGET_ANDROID_INTERNAL_H_
