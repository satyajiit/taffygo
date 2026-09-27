// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <string>

#include "taffy/browser/android/browser_profiles_restore_discovery_android_internal.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::restore_discovery_internal {
namespace {

using Action = BackupRestoreRestartCandidateAction;
using PhysicalError = BackupRestoreRestartPhysicalError;
using Status = BackupRestoreRestartDiscoveryStatus;

BackupRestoreRestartCandidate Candidate(Action action) {
  return {.reservation_id = "reservation", .action = action};
}

TEST(BackupRestoreProfileDiscoverySourceTest,
     OnlyExactCandidateStatusesCarryActions) {
  constexpr std::array kNoActionStatuses = {
      Status::kNone,
      Status::kSourceUnavailable,
      Status::kPrecommit,
      Status::kPresentationUnavailable,
      Status::kSchemaMismatch,
      Status::kOutcomeUnknown,
      Status::kCustodyAmbiguous,
      Status::kPublished,
      Status::kVerifiedDeleted,
  };
  for (Status status : kNoActionStatuses) {
    EXPECT_TRUE(IsWellFormedDiscovery({.status = status}));
    EXPECT_FALSE(IsWellFormedDiscovery(
        {.status = status, .candidate = Candidate(Action::kReview)}));
  }

  EXPECT_TRUE(IsWellFormedDiscovery({.status = Status::kRollbackAvailable,
                                     .candidate = Candidate(Action::kReview)}));
  EXPECT_FALSE(
      IsWellFormedDiscovery({.status = Status::kRollbackAvailable,
                             .candidate = Candidate(Action::kDiscardOnly)}));
  EXPECT_TRUE(
      IsWellFormedDiscovery({.status = Status::kCleanupRequired,
                             .candidate = Candidate(Action::kDiscardOnly)}));
  EXPECT_FALSE(
      IsWellFormedDiscovery({.status = Status::kCleanupRequired,
                             .candidate = Candidate(Action::kReview)}));
}

TEST(BackupRestoreProfileDiscoverySourceTest,
     SchemaCustodyAndStorageFailuresRemainDistinct) {
  auto schema = ProjectPhysicalFailure(PhysicalError::kSchemaMismatch);
  ASSERT_TRUE(schema);
  EXPECT_EQ(Status::kSchemaMismatch, schema->status);
  auto busy = ProjectPhysicalFailure(PhysicalError::kBusy);
  ASSERT_TRUE(busy);
  EXPECT_EQ(Status::kCustodyAmbiguous, busy->status);
  auto custody = ProjectPhysicalFailure(PhysicalError::kCustodyAmbiguous);
  ASSERT_TRUE(custody);
  EXPECT_EQ(Status::kCustodyAmbiguous, custody->status);
  auto storage = ProjectPhysicalFailure(PhysicalError::kStorageUnavailable);
  ASSERT_FALSE(storage);
  EXPECT_EQ(BackupRestoreRestartDiscoveryError::kStorageUnavailable,
            storage.error());
}

}  // namespace
}  // namespace taffy::restore_discovery_internal
