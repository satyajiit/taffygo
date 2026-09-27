// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_PROJECTION_H_
#define TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_PROJECTION_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "taffy/browser/android/browser_profiles_restore_discovery.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"
#include "taffy/browser/profile_backup_workflow.h"

namespace taffy::backup_workflow_restore_internal {

// Projects the physical lifecycle result without turning an ambiguous
// post-intent failure into permission to retry. Refused and unavailable are
// reserved for gates known to precede consumptive resolution authorization.
ProfileBackupWorkflow::RestoreResolutionStatus ProjectCandidateResolution(
    const BrowserProfilesRestoreLifecycle::CandidateResolutionResult& result);

bool ResolutionRetainsHiddenCandidate(
    ProfileBackupWorkflow::RestoreResolutionStatus status);

// The Android owner may observe verified physical cleanup before the internal
// workflow commit callback, or vice versa. It may release the operation only
// after both one-shot continuations have settled.
bool CanReleasePrecommitOperation(bool cleanup_settled,
                                  bool commit_completion_pending);

// Validates and flattens the durable content-free presentation into the
// seven-integer-per-selected-class Android wire shape.
std::optional<std::vector<int32_t>> FlattenRecoveryPresentation(
    const BackupRestoreRecoveryPresentation& presentation);

// Only the two exact portable candidate classifications may mint a fresh
// process-local review token.
bool IsValidRecoveredCandidate(const BackupRestoreRestartDiscovery& discovery);

bool RecoveryResolutionChoiceAllowed(
    bool discard_only,
    core_service::mojom::BackupRestoreResolutionChoice choice);

}  // namespace taffy::backup_workflow_restore_internal

#endif  // TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_PROJECTION_H_
