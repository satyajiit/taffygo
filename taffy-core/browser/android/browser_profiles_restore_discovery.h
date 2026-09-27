// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_DISCOVERY_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_DISCOVERY_H_

#include <cstdint>
#include <optional>
#include <string>

#include "base/types/expected.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"

namespace taffy {

// Discovery is observational. The reconciliation mode permits only the
// read-only SQL inspection of an outstanding commit intent; it never permits
// an accept, discard, publication, deletion or replay.
enum class BackupRestoreRestartDiscoveryMode : uint8_t {
  kInspectOnly,
  kReconcileCommit,
};

// Every status except the two candidate statuses carries no action. Terminal
// statuses are returned only after exact physical verification and durable
// retirement of the stale reservation.
enum class BackupRestoreRestartDiscoveryStatus : uint8_t {
  kNone,
  kSourceUnavailable,
  kPrecommit,
  kPresentationUnavailable,
  kSchemaMismatch,
  kOutcomeUnknown,
  kCustodyAmbiguous,
  kRollbackAvailable,
  kCleanupRequired,
  kPublished,
  kVerifiedDeleted,
};

enum class BackupRestoreRestartCandidateAction : uint8_t {
  kReview,
  kDiscardOnly,
};

// A fresh process-local projection, not a capability. The reservation id is
// correlation only. No operation envelope, authority, token or Core
// generation is reconstructed from durable state.
struct BackupRestoreRestartCandidate {
  std::string reservation_id;
  BackupRestoreRecoveryPresentation presentation;
  BackupRestoreRestartCandidateAction action =
      BackupRestoreRestartCandidateAction::kReview;

  bool operator==(const BackupRestoreRestartCandidate&) const = default;
};

struct BackupRestoreRestartDiscovery {
  BackupRestoreRestartDiscoveryStatus status =
      BackupRestoreRestartDiscoveryStatus::kNone;
  std::optional<BackupRestoreRestartCandidate> candidate;

  bool operator==(const BackupRestoreRestartDiscovery&) const = default;
};

enum class BackupRestoreRestartDiscoveryError : uint8_t {
  kInvalidArgument,
  kBusy,
  kStorageUnavailable,
};

using BackupRestoreRestartDiscoveryResult =
    base::expected<BackupRestoreRestartDiscovery,
                   BackupRestoreRestartDiscoveryError>;

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_DISCOVERY_H_
