// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_recovery_journal.h"

#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;
using Outcome = wire::BackupRestoreCommitOutcome;
constexpr char kReservation[] = "11111111-1111-4111-8111-111111111111";
constexpr char kSource[] = "22222222-2222-4222-8222-222222222222";
constexpr char kTarget[] = "33333333-3333-4333-8333-333333333333";

wire::BackupRestoreBindingPtr Binding() {
  return wire::BackupRestoreBinding::New(
      wire::OperationEnvelope::New("plan", 7u, 0u, 50u, "plan-once"), kSource,
      wire::BackupRestoreTarget::New(
          wire::BackupRestoreTargetKind::kNewRegularProfile, kTarget),
      "backup-id", std::vector<uint8_t>(32u, 1u),
      std::vector<uint8_t>(32u, 2u));
}

wire::BackupRestoreCandidateWitnessPtr Witness() {
  return wire::BackupRestoreCandidateWitness::New(
      std::vector<wire::BackupRecordKind>{
          wire::BackupRecordKind::kLibraryEntry},
      1u, std::vector<uint8_t>(32u, 3u));
}

class BackupRestoreRecoveryJournalTest : public testing::Test {
 protected:
  void SetUp() override {
    application_preferences::RegisterLocalStatePreferences(prefs_.registry());
    ASSERT_TRUE(ReserveBackupRestoreProfilePath(&prefs_, kReservation,
                                                base::FilePath("Default"),
                                                base::FilePath("Profile 2")));
    ASSERT_TRUE(MarkBackupRestoreProfileCreated(&prefs_, kReservation));
    ASSERT_TRUE(
        BindBackupRestoreTargetProfileId(&prefs_, kReservation, kTarget));
  }

  BackupRestoreRecoveryRecordResult Begin(std::string id = "commit-once") {
    return BeginBackupRestoreCommitIntent(&prefs_, kReservation, *Binding(),
                                          *Witness(), std::move(id));
  }

  base::DictValue Registry() {
    return prefs_
        .GetDict(application_preferences::kBackupRestoreProfileReservations)
        .Clone();
  }

  void Store(base::DictValue value) {
    prefs_.SetDict(application_preferences::kBackupRestoreProfileReservations,
                   std::move(value));
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(BackupRestoreRecoveryJournalTest,
       BeginKeepsReservationQuarantinedAndRoundTrips) {
  auto absent = ReadBackupRestoreRecoveryJournal(&prefs_, kReservation);
  ASSERT_TRUE(absent);
  EXPECT_TRUE(absent->empty());
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  auto history = ReadBackupRestoreRecoveryJournal(&prefs_, kReservation);
  ASSERT_TRUE(history);
  ASSERT_EQ(1u, history->size());
  EXPECT_EQ(kSource, history->front()->binding->owner_profile_id);
  EXPECT_EQ(kTarget, history->front()->binding->target_profile_id);
  EXPECT_EQ(1u, history->front()->binding->record_count);
  EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                &prefs_, base::FilePath("/profiles/Profile 2")));
  EXPECT_TRUE(ReadBackupRestoreProfileReservations(&prefs_));
  // In-memory PrefService evidence only; no test here claims a disk barrier.
}

TEST_F(BackupRestoreRecoveryJournalTest,
       RepeatedIntentNeverAuthorizesAnotherAttempt) {
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  auto repeated = Begin();
  ASSERT_FALSE(repeated.has_value());
  EXPECT_EQ(Error::kBusy, repeated.error());
  ASSERT_TRUE(RecordBackupRestoreCommitOutcome(&prefs_, kReservation, **intent,
                                               Outcome::kOutcomeUnknown)
                  .has_value());
  EXPECT_FALSE(Begin("different-attempt").has_value());
  auto history = ReadBackupRestoreRecoveryJournal(&prefs_, kReservation);
  ASSERT_TRUE(history);
  EXPECT_EQ(2u, history->size());
}

TEST_F(BackupRestoreRecoveryJournalTest,
       ExactTerminalObservationIsIdempotentNotRewritten) {
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  auto outcome = RecordBackupRestoreCommitOutcome(
      &prefs_, kReservation, **intent, Outcome::kCommitted);
  ASSERT_TRUE(outcome.has_value());
  const auto before = Registry();
  auto again = RecordBackupRestoreCommitOutcome(&prefs_, kReservation, **intent,
                                                Outcome::kCommitted);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(2u, (*again)->sequence);
  EXPECT_EQ(before, Registry());
  EXPECT_FALSE(RecordBackupRestoreCommitOutcome(
                   &prefs_, kReservation, **intent,
                   Outcome::kDefinitelyNotCommitted)
                   .has_value());
  EXPECT_FALSE(RecordBackupRestoreCommitOutcome(&prefs_, kReservation, **intent,
                                                Outcome::kOutcomeUnknown)
                   .has_value());
}

TEST_F(BackupRestoreRecoveryJournalTest,
       UnknownMayBeSettledOnlyForTheExactIntent) {
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  ASSERT_TRUE(RecordBackupRestoreCommitOutcome(&prefs_, kReservation, **intent,
                                               Outcome::kOutcomeUnknown)
                  .has_value());
  auto different = (*intent).Clone();
  different->intent->intent_id = "other";
  EXPECT_FALSE(RecordBackupRestoreCommitOutcome(
                   &prefs_, kReservation, *different, Outcome::kCommitted)
                   .has_value());
  auto settled = RecordBackupRestoreCommitOutcome(
      &prefs_, kReservation, **intent, Outcome::kDefinitelyNotCommitted);
  ASSERT_TRUE(settled.has_value());
  EXPECT_EQ(3u, (*settled)->sequence);
  auto history = ReadBackupRestoreRecoveryJournal(&prefs_, kReservation);
  ASSERT_TRUE(history);
  ASSERT_EQ(3u, history->size());
  EXPECT_EQ(wire::BackupRestoreObservedOutcome::kOutcomeUnknown,
            history->at(1)->outcome->outcome);
}

TEST_F(BackupRestoreRecoveryJournalTest,
       WrongTargetAndUnsupportedWitnessNeverWrite) {
  const auto before = Registry();
  auto binding = Binding();
  binding->target->profile_id = kSource;
  EXPECT_FALSE(BeginBackupRestoreCommitIntent(&prefs_, kReservation, *binding,
                                              *Witness(), "attempt")
                   .has_value());
  auto witness = Witness();
  witness->candidate_records_sha256.assign(32u, 0u);
  EXPECT_FALSE(BeginBackupRestoreCommitIntent(&prefs_, kReservation, *Binding(),
                                              *witness, "attempt")
                   .has_value());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreRecoveryJournalTest,
       PresentEmptyOrWrongTypedJournalIsCorrupt) {
  for (const bool wrong_type : {true, false}) {
    base::Value malformed =
        wrong_type ? base::Value(false) : base::Value(base::ListValue());
    auto registry = Registry();
    registry.FindDict(kReservation)
        ->Set(kBackupRestoreRecoveryJournalKey, std::move(malformed));
    Store(std::move(registry));
    EXPECT_FALSE(ReadBackupRestoreRecoveryJournal(&prefs_, kReservation));
    EXPECT_EQ(BackupRestoreProfileQuarantineStatus::kRegistryCorrupt,
              BackupRestoreQuarantineForProfilePath(
                  &prefs_, base::FilePath("/profiles/Profile 2")));
  }
}

TEST_F(BackupRestoreRecoveryJournalTest,
       UnknownFieldsAndCrossBindingNeverLoseRawCustody) {
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  ASSERT_TRUE(RecordBackupRestoreCommitOutcome(&prefs_, kReservation, **intent,
                                               Outcome::kOutcomeUnknown)
                  .has_value());
  auto registry = Registry();
  auto* rows = registry.FindDict(kReservation)
                   ->FindList(kBackupRestoreRecoveryJournalKey);
  ASSERT_TRUE(rows);
  rows->back().GetDict().FindDict("binding")->Set("owner_profile_id",
                                                  "different-owner");
  const auto corrupted = registry.Clone();
  Store(std::move(registry));
  EXPECT_FALSE(ReadBackupRestoreProfileReservations(&prefs_));
  EXPECT_FALSE(Begin("retry").has_value());
  EXPECT_EQ(corrupted, Registry());
}

TEST_F(BackupRestoreRecoveryJournalTest,
       CodecRejectsNoncanonicalAndUnknownFields) {
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  auto encoded = EncodeBackupRestoreRecoveryRecord(**intent);
  ASSERT_TRUE(encoded);
  for (const std::string field :
       {"payload", "planning_operation", "credential", "path"}) {
    auto invalid = encoded->Clone();
    invalid.Set(field, "unexpected");
    base::ListValue rows;
    rows.Append(std::move(invalid));
    EXPECT_FALSE(DecodeBackupRestoreRecoveryJournal(
        base::Value(std::move(rows)), kReservation, kTarget));
  }
  auto invalid = encoded->Clone();
  invalid.Set("sequence", "01");
  base::ListValue rows;
  rows.Append(std::move(invalid));
  EXPECT_FALSE(DecodeBackupRestoreRecoveryJournal(base::Value(std::move(rows)),
                                                  kReservation, kTarget));
}

TEST_F(BackupRestoreRecoveryJournalTest,
       EmptyProjectionHasAnExplicitNonzeroWitness) {
  auto witness = Witness();
  witness->selection.clear();
  witness->record_count = 0u;
  auto intent = BeginBackupRestoreCommitIntent(
      &prefs_, kReservation, *Binding(), *witness, "empty-commit");
  ASSERT_TRUE(intent.has_value());
  auto history = ReadBackupRestoreRecoveryJournal(&prefs_, kReservation);
  ASSERT_TRUE(history);
  EXPECT_TRUE(history->front()->binding->selection.empty());
  EXPECT_EQ(0u, history->front()->binding->record_count);
}

TEST_F(BackupRestoreRecoveryJournalTest,
       CandidateKindsAreCanonicalAndBoundedByRecordCount) {
  const auto before = Registry();
  for (const bool duplicate : {true, false}) {
    auto witness = Witness();
    witness->selection.push_back(duplicate
                                     ? wire::BackupRecordKind::kLibraryEntry
                                     : wire::BackupRecordKind::kMemoryRecord);
    EXPECT_FALSE(BeginBackupRestoreCommitIntent(
                     &prefs_, kReservation, *Binding(), *witness,
                     "bad-selection")
                     .has_value());
  }
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreRecoveryJournalTest,
       CodecRejectsUnknownTagsAndDigestRepresentations) {
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  auto encoded = EncodeBackupRestoreRecoveryRecord(**intent);
  ASSERT_TRUE(encoded);
  for (const int mutation : {0, 1, 2, 3, 4, 5}) {
    auto invalid = encoded->Clone();
    switch (mutation) {
      case 0:
        invalid.Set("fact_kind", 999);
        break;
      case 1:
        invalid.FindDict("intent")->Set("intent", -1);
        break;
      case 2:
        invalid.FindDict("binding")->Set("target_kind", 999);
        break;
      case 3:
        invalid.FindDict("binding")->Set("record_count", "01");
        break;
      case 4:
        invalid.FindDict("binding")->Set("snapshot_sha256",
                                         std::string(64u, 'A'));
        break;
      case 5:
        invalid.FindDict("binding")->Set("candidate_records_sha256",
                                         std::string(64u, '0'));
        break;
    }
    base::ListValue rows;
    rows.Append(std::move(invalid));
    EXPECT_FALSE(DecodeBackupRestoreRecoveryJournal(
        base::Value(std::move(rows)), kReservation, kTarget))
        << mutation;
  }
}

TEST_F(BackupRestoreRecoveryJournalTest,
       CodecBoundsStructuralHistoryWithoutClassifyingIt) {
  auto intent = Begin();
  ASSERT_TRUE(intent.has_value());
  base::ListValue rows;
  for (uint64_t sequence = 1u;
       sequence <= wire::kMaxBackupRestoreRecoveryRecords; ++sequence) {
    // Repeated intent identities are structurally valid here. Only the Rust
    // history inspector decides that this is not executable recovery history.
    auto record = (*intent).Clone();
    record->sequence = sequence;
    auto encoded = EncodeBackupRestoreRecoveryRecord(*record);
    ASSERT_TRUE(encoded);
    rows.Append(std::move(*encoded));
  }
  EXPECT_TRUE(DecodeBackupRestoreRecoveryJournal(base::Value(rows.Clone()),
                                                 kReservation, kTarget));
  auto extra = rows.back().Clone();
  rows.Append(std::move(extra));
  EXPECT_FALSE(DecodeBackupRestoreRecoveryJournal(base::Value(std::move(rows)),
                                                  kReservation, kTarget));
}

}  // namespace
}  // namespace taffy
