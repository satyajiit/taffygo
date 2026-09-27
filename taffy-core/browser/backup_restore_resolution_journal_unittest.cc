// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;
using Outcome = wire::BackupRestoreResolutionOutcome;
using Intent = wire::BackupRestorePhysicalIntent;
constexpr char kReservation[] = "11111111-1111-4111-8111-111111111111";
constexpr char kSource[] = "22222222-2222-4222-8222-222222222222";
constexpr char kTarget[] = "33333333-3333-4333-8333-333333333333";

class BackupRestoreResolutionJournalTest : public testing::Test {
 protected:
  void SetUp() override {
    application_preferences::RegisterLocalStatePreferences(prefs_.registry());
    ASSERT_TRUE(ReserveBackupRestoreProfilePath(&prefs_, kReservation,
                                                base::FilePath("Default"),
                                                base::FilePath("Profile 2")));
    ASSERT_TRUE(MarkBackupRestoreProfileCreated(&prefs_, kReservation));
    ASSERT_TRUE(
        BindBackupRestoreTargetProfileId(&prefs_, kReservation, kTarget));
    auto binding = wire::BackupRestoreBinding::New(
        wire::OperationEnvelope::New("plan", 7u, 0u, 50u, "plan-once"), kSource,
        wire::BackupRestoreTarget::New(
            wire::BackupRestoreTargetKind::kNewRegularProfile, kTarget),
        "backup", std::vector<uint8_t>(32u, 1u), std::vector<uint8_t>(32u, 2u));
    auto witness = wire::BackupRestoreCandidateWitness::New(
        std::vector<wire::BackupRecordKind>{
            wire::BackupRecordKind::kLibraryEntry},
        1u, std::vector<uint8_t>(32u, 3u));
    auto intent = BeginBackupRestoreCommitIntent(&prefs_, kReservation,
                                                 *binding, *witness, "commit");
    // base::expected disables its operator bool when the value type is itself
    // bool-testable, and a mojo StructPtr is. Every journal result therefore
    // has to be asserted through has_value().
    ASSERT_TRUE(intent.has_value());
    commit_intent_ = std::move(*intent);
    ASSERT_TRUE(RecordBackupRestoreCommitOutcome(
                    &prefs_, kReservation, *commit_intent_,
                    wire::BackupRestoreCommitOutcome::kCommitted)
                    .has_value());
  }

  // Fixture construction only. Production appends resolution intent only from
  // the current source Core's exact consumptive resolution authorization.
  wire::BackupRestoreRecoveryRecordPtr SeedIntent(
      Intent kind = Intent::kAcceptCandidate,
      std::string id = "resolve") {
    auto history = ReadBackupRestoreRecoveryJournal(&prefs_, kReservation);
    EXPECT_TRUE(history);
    if (!history) {
      return nullptr;
    }
    auto intent = commit_intent_.Clone();
    intent->sequence = history->size() + 1u;
    intent->intent =
        wire::BackupRestoreRecoveryIntentFact::New(std::move(id), kind);
    auto encoded = EncodeBackupRestoreRecoveryRecord(*intent);
    EXPECT_TRUE(encoded);
    if (!encoded) {
      return nullptr;
    }
    auto registry = Registry();
    registry.FindDict(kReservation)
        ->FindList(kBackupRestoreRecoveryJournalKey)
        ->Append(std::move(*encoded));
    prefs_.SetDict(application_preferences::kBackupRestoreProfileReservations,
                   std::move(registry));
    return intent;
  }

  auto Observe(const wire::BackupRestoreRecoveryRecord& intent,
               Outcome outcome) {
    return RecordBackupRestoreResolutionOutcome(&prefs_, kReservation, intent,
                                                outcome);
  }

  wire::BackupRestoreRecoveryResolutionAuthorizationPtr Authorization() {
    auto history = ReadBackupRestoreRecoveryJournal(&prefs_, kReservation);
    EXPECT_TRUE(history);
    if (!history) {
      return nullptr;
    }
    auto authorization =
        wire::BackupRestoreRecoveryResolutionAuthorization::New();
    authorization->binding = commit_intent_->binding.Clone();
    authorization->decision_operation = wire::OperationEnvelope::New(
        "resolution", 8u, 0u, 100u, "resolution-once");
    authorization->choice =
        wire::BackupRestoreResolutionChoice::kAcceptCandidate;
    authorization->intent_id = "fresh-resolution";
    authorization->history_prefix = std::move(*history);
    return authorization;
  }

  auto Begin(
      const wire::BackupRestoreRecoveryResolutionAuthorization& authorization) {
    return BeginBackupRestoreResolutionIntent(&prefs_, kReservation,
                                              authorization);
  }

  base::DictValue Registry() {
    return prefs_
        .GetDict(application_preferences::kBackupRestoreProfileReservations)
        .Clone();
  }

  TestingPrefServiceSimple prefs_;
  wire::BackupRestoreRecoveryRecordPtr commit_intent_;
};

TEST_F(BackupRestoreResolutionJournalTest,
       ExactResolutionAuthorityAppendsOnlyItsNamedIntentOnce) {
  auto authorization = Authorization();
  ASSERT_TRUE(authorization);
  auto intent = Begin(*authorization);
  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(3u, (*intent)->sequence);
  EXPECT_EQ("fresh-resolution", (*intent)->intent->intent_id);
  EXPECT_EQ(Intent::kAcceptCandidate, (*intent)->intent->intent);
  const auto before = Registry();
  EXPECT_FALSE(Begin(*authorization).has_value());
  EXPECT_EQ(before, Registry());
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                &prefs_, base::FilePath("/profiles/Profile 2")));
}

TEST_F(BackupRestoreResolutionJournalTest,
       DiscardAuthorityDoesNotBecomeAcceptIntent) {
  auto authorization = Authorization();
  ASSERT_TRUE(authorization);
  authorization->choice =
      wire::BackupRestoreResolutionChoice::kDiscardCandidate;
  auto intent = Begin(*authorization);
  ASSERT_TRUE(intent.has_value());
  EXPECT_EQ(Intent::kDiscardCandidate, (*intent)->intent->intent);
}

TEST_F(BackupRestoreResolutionJournalTest,
       AuthorityCannotRebindAnyPersistedTargetIdentity) {
  auto authorization = Authorization();
  ASSERT_TRUE(authorization);
  const auto before = Registry();
  auto changed = authorization.Clone();
  changed->binding->candidate_records_sha256[0] ^= 1u;
  EXPECT_FALSE(Begin(*changed).has_value());
  changed = authorization.Clone();
  changed->binding->target_profile_id = kSource;
  EXPECT_FALSE(Begin(*changed).has_value());
  changed = authorization.Clone();
  changed->binding->reservation_id = kTarget;
  EXPECT_FALSE(Begin(*changed).has_value());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreResolutionJournalTest,
       AuthorityRequiresEveryExactPrefixFactAndRefusesNullRecords) {
  auto authorization = Authorization();
  ASSERT_TRUE(authorization);
  const auto before = Registry();
  auto changed = authorization.Clone();
  changed->history_prefix[1]->outcome->outcome =
      wire::BackupRestoreObservedOutcome::kOutcomeUnknown;
  EXPECT_FALSE(Begin(*changed).has_value());
  changed = authorization.Clone();
  changed->history_prefix[0].reset();
  EXPECT_FALSE(Begin(*changed).has_value());
  changed = authorization.Clone();
  changed->history_prefix.pop_back();
  EXPECT_FALSE(Begin(*changed).has_value());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreResolutionJournalTest,
       InterveningIntentCannotBeOverwrittenByPreviouslyIssuedAuthority) {
  auto authorization = Authorization();
  ASSERT_TRUE(authorization);
  ASSERT_TRUE(SeedIntent(Intent::kDiscardCandidate, "intervening"));
  const auto before = Registry();
  EXPECT_FALSE(Begin(*authorization).has_value());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreResolutionJournalTest,
       MissingOperationBindingOrIntentIdentityDoesNotMutateJournal) {
  auto authorization = Authorization();
  ASSERT_TRUE(authorization);
  const auto before = Registry();
  auto changed = authorization.Clone();
  changed->decision_operation.reset();
  EXPECT_FALSE(Begin(*changed).has_value());
  changed = authorization.Clone();
  changed->binding.reset();
  EXPECT_FALSE(Begin(*changed).has_value());
  changed = authorization.Clone();
  changed->intent_id.clear();
  EXPECT_FALSE(Begin(*changed).has_value());
  changed = authorization.Clone();
  changed->choice = static_cast<wire::BackupRestoreResolutionChoice>(255u);
  EXPECT_FALSE(Begin(*changed).has_value());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreResolutionJournalTest,
       ResolutionPrefixMustReserveCapacityForIntentAndTwoObservations) {
  auto authorization = Authorization();
  ASSERT_TRUE(authorization);
  while (authorization->history_prefix.size() <
         wire::kMaxBackupRestoreRecoveryRecords - 2u) {
    auto next = authorization->history_prefix.back().Clone();
    next->sequence = authorization->history_prefix.size() + 1u;
    authorization->history_prefix.push_back(std::move(next));
  }
  const auto before = Registry();
  auto result = Begin(*authorization);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(Error::kInvalidArgument, result.error());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreResolutionJournalTest,
       CompletedAcceptRetainsQuarantineUntilSeparateVerifiedRetirement) {
  auto intent = SeedIntent();
  ASSERT_TRUE(intent);
  auto recorded = Observe(*intent, Outcome::kCompleted);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(4u, (*recorded)->sequence);
  ASSERT_TRUE((*recorded)->outcome);
  EXPECT_EQ(wire::BackupRestoreObservedOutcome::kCompleted,
            (*recorded)->outcome->outcome);
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                &prefs_, base::FilePath("/profiles/Profile 2")));
}

TEST_F(BackupRestoreResolutionJournalTest,
       UnknownDiscardCanBeSettledOnlyForItsExactIntent) {
  auto intent = SeedIntent(Intent::kDiscardCandidate);
  ASSERT_TRUE(intent);
  ASSERT_TRUE(Observe(*intent, Outcome::kOutcomeUnknown).has_value());
  auto changed = intent.Clone();
  changed->intent->intent_id = "another-discard";
  EXPECT_FALSE(Observe(*changed, Outcome::kCompleted).has_value());
  changed = intent.Clone();
  changed->binding->candidate_records_sha256[0] ^= 1u;
  EXPECT_FALSE(Observe(*changed, Outcome::kCompleted).has_value());
  auto terminal = Observe(*intent, Outcome::kCompleted);
  ASSERT_TRUE(terminal.has_value());
  EXPECT_EQ(5u, (*terminal)->sequence);
}

TEST_F(BackupRestoreResolutionJournalTest,
       ExactUnknownAndTerminalObservationsAreIdempotent) {
  auto intent = SeedIntent();
  ASSERT_TRUE(intent);
  ASSERT_TRUE(Observe(*intent, Outcome::kOutcomeUnknown).has_value());
  const auto unknown = Registry();
  EXPECT_TRUE(Observe(*intent, Outcome::kOutcomeUnknown).has_value());
  EXPECT_EQ(unknown, Registry());
  ASSERT_TRUE(Observe(*intent, Outcome::kDefinitelyNotCompleted).has_value());
  const auto terminal = Registry();
  EXPECT_TRUE(Observe(*intent, Outcome::kDefinitelyNotCompleted).has_value());
  EXPECT_EQ(terminal, Registry());
  EXPECT_FALSE(Observe(*intent, Outcome::kCompleted).has_value());
  EXPECT_FALSE(Observe(*intent, Outcome::kOutcomeUnknown).has_value());
}

TEST_F(BackupRestoreResolutionJournalTest,
       LateOutcomeCannotCrossAnInterveningResolution) {
  auto first = SeedIntent(Intent::kAcceptCandidate, "first");
  ASSERT_TRUE(first);
  ASSERT_TRUE(Observe(*first, Outcome::kDefinitelyNotCompleted).has_value());
  auto second = SeedIntent(Intent::kDiscardCandidate, "second");
  ASSERT_TRUE(second);
  const auto before = Registry();
  EXPECT_FALSE(Observe(*first, Outcome::kCompleted).has_value());
  EXPECT_EQ(before, Registry());
  auto terminal = Observe(*second, Outcome::kCompleted);
  ASSERT_TRUE(terminal.has_value());
  EXPECT_EQ(6u, (*terminal)->sequence);
}

TEST_F(BackupRestoreResolutionJournalTest,
       UnstoredOrInitialCommitIntentCannotBecomeResolutionOutcome) {
  EXPECT_FALSE(Observe(*commit_intent_, Outcome::kCompleted).has_value());
  auto intent = commit_intent_.Clone();
  intent->sequence = 3u;
  intent->intent = wire::BackupRestoreRecoveryIntentFact::New(
      "unstored", Intent::kDiscardCandidate);
  const auto before = Registry();
  auto refused = Observe(*intent, Outcome::kCompleted);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(Error::kWrongPhysicalState, refused.error());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreResolutionJournalTest,
       LateOutcomeCannotChangeAStoredIntentKindOrSequence) {
  auto intent = SeedIntent();
  ASSERT_TRUE(intent);
  auto changed = intent.Clone();
  changed->intent->intent = Intent::kDiscardCandidate;
  EXPECT_FALSE(Observe(*changed, Outcome::kCompleted).has_value());
  changed = intent.Clone();
  ++changed->sequence;
  EXPECT_FALSE(Observe(*changed, Outcome::kCompleted).has_value());
  auto terminal = Observe(*intent, Outcome::kCompleted);
  ASSERT_TRUE(terminal.has_value());
  EXPECT_EQ(4u, (*terminal)->sequence);
}

}  // namespace
}  // namespace taffy
