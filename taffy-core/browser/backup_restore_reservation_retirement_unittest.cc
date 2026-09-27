// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_reservation_retirement.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;
using Kind = wire::BackupRestoreRecoveryClassificationKind;
using Intent = wire::BackupRestorePhysicalIntent;
using Observed = wire::BackupRestoreObservedOutcome;
using Quarantine = BackupRestoreProfileQuarantineStatus;
using Retirement = BackupRestoreReservationRetirement;
constexpr char kReservation[] = "11111111-1111-4111-8111-111111111111";
constexpr char kOtherReservation[] = "44444444-4444-4444-8444-444444444444";
constexpr char kSource[] = "22222222-2222-4222-8222-222222222222";
constexpr char kTarget[] = "33333333-3333-4333-8333-333333333333";

class BackupRestoreReservationRetirementTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_FALSE(Retirement::IsInProgress());
    application_preferences::RegisterLocalStatePreferences(prefs_.registry());
    ASSERT_TRUE(ReserveBackupRestoreProfilePath(&prefs_, kReservation,
                                                base::FilePath("Default"),
                                                base::FilePath("Profile 2")));
    ASSERT_TRUE(MarkBackupRestoreProfileCreated(&prefs_, kReservation));
    ASSERT_TRUE(
        BindBackupRestoreTargetProfileId(&prefs_, kReservation, kTarget));
    auto binding = wire::BackupRestoreRecoveryBinding::New(
        kReservation, kSource,
        wire::BackupRestoreTargetKind::kNewRegularProfile, kTarget, "backup",
        std::vector<uint8_t>(32u, 1u), std::vector<uint8_t>(32u, 2u),
        std::vector<wire::BackupRecordKind>{
            wire::BackupRecordKind::kLibraryEntry},
        1u, std::vector<uint8_t>(32u, 3u));
    history_.push_back(wire::BackupRestoreRecoveryRecord::New(
        1u, 1u, binding.Clone(),
        wire::BackupRestoreRecoveryFactKind::kIntentRecorded,
        wire::BackupRestoreRecoveryIntentFact::New("commit",
                                                   Intent::kCommitCandidate),
        nullptr));
    history_.push_back(wire::BackupRestoreRecoveryRecord::New(
        1u, 2u, binding.Clone(),
        wire::BackupRestoreRecoveryFactKind::kOutcomeObserved, nullptr,
        wire::BackupRestoreRecoveryOutcomeFact::New("commit",
                                                    Observed::kCompleted)));
    history_.push_back(wire::BackupRestoreRecoveryRecord::New(
        1u, 3u, binding.Clone(),
        wire::BackupRestoreRecoveryFactKind::kIntentRecorded,
        wire::BackupRestoreRecoveryIntentFact::New("resolve",
                                                   Intent::kAcceptCandidate),
        nullptr));
    history_.push_back(wire::BackupRestoreRecoveryRecord::New(
        1u, 4u, std::move(binding),
        wire::BackupRestoreRecoveryFactKind::kOutcomeObserved, nullptr,
        wire::BackupRestoreRecoveryOutcomeFact::New("resolve",
                                                    Observed::kCompleted)));
    StoreFixtureHistory();
    auto reservations = ReadBackupRestoreProfileReservations(&prefs_);
    ASSERT_TRUE(reservations);
    ASSERT_EQ(1u, reservations->size());
    reservation_ = reservations->front();
  }

  void TearDown() override { EXPECT_FALSE(Retirement::IsInProgress()); }

  void StoreFixtureHistory() {
    base::ListValue records;
    for (const auto& record : history_) {
      auto encoded = EncodeBackupRestoreRecoveryRecord(*record);
      ASSERT_TRUE(encoded);
      records.Append(std::move(*encoded));
    }
    auto registry = Registry();
    registry.FindDict(kReservation)
        ->Set(kBackupRestoreRecoveryJournalKey, std::move(records));
    prefs_.SetDict(application_preferences::kBackupRestoreProfileReservations,
                   std::move(registry));
  }

  auto Begin(Kind kind = Kind::kPublished) {
    return Retirement::Begin(
        &prefs_, reservation_, history_,
        *wire::BackupRestoreRecoveryClassification::New(kind, nullptr));
  }

  base::DictValue Registry() const {
    return prefs_
        .GetDict(application_preferences::kBackupRestoreProfileReservations)
        .Clone();
  }

  Quarantine TargetQuarantine() const {
    return BackupRestoreQuarantineForProfilePath(
        &prefs_, base::FilePath("/profiles/Profile 2"));
  }

  TestingPrefServiceSimple prefs_;
  BackupRestoreProfileReservation reservation_;
  BackupRestoreRecoveryRecords history_;
};

TEST_F(BackupRestoreReservationRetirementTest,
       PreferenceObserversAndPendingAbsenceRemainQuarantined) {
  PrefChangeRegistrar observer;
  observer.Init(&prefs_);
  int notifications = 0;
  // base::BindRepeating rejects capturing lambdas, and this observer has to
  // read the fixture and the local counter, so bind the test-only variant.
  observer.Add(application_preferences::kBackupRestoreProfileReservations,
               base::BindLambdaForTesting([&] {
                 ++notifications;
                 EXPECT_EQ(Quarantine::kQuarantined, TargetQuarantine());
               }));
  auto owner = Begin();
  ASSERT_TRUE(owner.has_value());
  EXPECT_EQ(1, notifications);
  EXPECT_TRUE(Registry().empty());
  EXPECT_EQ(Quarantine::kQuarantined, TargetQuarantine());
  EXPECT_EQ(Quarantine::kNotQuarantined,
            BackupRestoreQuarantineForProfilePath(
                &prefs_, base::FilePath("/profiles/Default")));
  // The fixture stands in for the physical caller's successful disk barrier;
  // this unit test itself makes no synchronized disk or device claim.
  EXPECT_TRUE((*owner)->ReleaseAfterVerifiedAbsence());
  EXPECT_EQ(Quarantine::kNotQuarantined, TargetQuarantine());
  EXPECT_FALSE((*owner)->ReleaseAfterVerifiedAbsence());
}

TEST_F(BackupRestoreReservationRetirementTest,
       AbandonedAbsenceBarrierRestoresExactRegistryBeforeUnfencing) {
  const auto before = Registry();
  {
    auto owner = Begin();
    ASSERT_TRUE(owner.has_value());
    EXPECT_TRUE(Retirement::IsInProgress());
  }
  EXPECT_EQ(before, Registry());
  EXPECT_EQ(Quarantine::kQuarantined, TargetQuarantine());
  EXPECT_FALSE(Retirement::IsInProgress());
  auto retry = Begin();
  ASSERT_TRUE(retry.has_value());
  EXPECT_TRUE((*retry)->ReleaseAfterVerifiedAbsence());
}

TEST_F(BackupRestoreReservationRetirementTest,
       PendingRetirementRefusesNewReservationsAndAnotherRetirement) {
  auto owner = Begin();
  ASSERT_TRUE(owner.has_value());
  auto next = ReserveBackupRestoreProfilePath(&prefs_, kOtherReservation,
                                              base::FilePath("Default"),
                                              base::FilePath("Profile 3"));
  ASSERT_FALSE(next);
  EXPECT_EQ(BackupRestoreProfileRegistryError::kBusy, next.error());
  EXPECT_FALSE(Begin().has_value());
  EXPECT_TRUE((*owner)->ReleaseAfterVerifiedAbsence());
}

TEST_F(BackupRestoreReservationRetirementTest,
       AStoredRegistryIsNotACompletedAbsenceBarrier) {
  const auto before = Registry();
  auto owner = Begin();
  ASSERT_TRUE(owner.has_value());
  prefs_.SetDict(application_preferences::kBackupRestoreProfileReservations,
                 before.Clone());
  EXPECT_FALSE((*owner)->ReleaseAfterVerifiedAbsence());
  EXPECT_EQ(Quarantine::kQuarantined, TargetQuarantine());
}

TEST_F(BackupRestoreReservationRetirementTest,
       CompletionKindMustNameTheExactPhysicalResolution) {
  const auto before = Registry();
  EXPECT_FALSE(Begin(Kind::kVerifiedDeleted).has_value());
  EXPECT_FALSE(Begin(Kind::kRollbackAvailable).has_value());
  EXPECT_FALSE(Begin(Kind::kCleanupRequired).has_value());
  EXPECT_EQ(before, Registry());
  history_[2]->intent->intent = Intent::kDiscardCandidate;
  StoreFixtureHistory();
  EXPECT_FALSE(Begin(Kind::kPublished).has_value());
  auto owner = Begin(Kind::kVerifiedDeleted);
  ASSERT_TRUE(owner.has_value());
  EXPECT_TRUE((*owner)->ReleaseAfterVerifiedAbsence());
}

TEST_F(BackupRestoreReservationRetirementTest,
       UnknownOrUnstartedResolutionCannotRemoveQuarantine) {
  for (auto outcome :
       {Observed::kOutcomeUnknown, Observed::kDefinitelyNotCompleted}) {
    history_.back()->outcome->outcome = outcome;
    StoreFixtureHistory();
    EXPECT_FALSE(Begin().has_value());
    EXPECT_EQ(Quarantine::kQuarantined, TargetQuarantine());
  }
  history_.resize(2u);
  StoreFixtureHistory();
  EXPECT_FALSE(Begin().has_value());
}

TEST_F(BackupRestoreReservationRetirementTest,
       ExactReservationAndEveryTerminalHistoryFactMustStillMatch) {
  const auto before = Registry();
  reservation_.source_profile_base_name = base::FilePath("Profile 4");
  EXPECT_FALSE(Begin().has_value());
  reservation_.source_profile_base_name = base::FilePath("Default");
  history_.back()->outcome->intent_id = "other-resolution";
  EXPECT_FALSE(Begin().has_value());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreReservationRetirementTest,
       ReconciledUnknownCanRetireOnlyAfterItsExactCompletedObservation) {
  auto completed = history_.back().Clone();
  history_.back()->outcome->outcome = Observed::kOutcomeUnknown;
  completed->sequence = 5u;
  history_.push_back(std::move(completed));
  StoreFixtureHistory();
  auto owner = Begin();
  ASSERT_TRUE(owner.has_value());
  EXPECT_TRUE((*owner)->ReleaseAfterVerifiedAbsence());
}

}  // namespace
}  // namespace taffy
