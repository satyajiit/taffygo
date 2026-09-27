// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_recovery_presentation.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Action = mojom::BackupRestoreAction;
using Error = BackupRestoreProfileRegistryError;
using Kind = mojom::BackupRecordKind;

constexpr char kReservation[] = "11111111-1111-4111-8111-111111111111";
constexpr char kSource[] = "22222222-2222-4222-8222-222222222222";
constexpr char kTarget[] = "33333333-3333-4333-8333-333333333333";
constexpr char kOtherTarget[] = "44444444-4444-4444-8444-444444444444";

std::vector<uint8_t> Digest(uint8_t byte) {
  return std::vector<uint8_t>(32u, byte);
}

mojom::BackupRestorePlanResultPtr Plan(
    std::vector<std::pair<Kind, Action>> entries = {}) {
  auto operation = mojom::OperationEnvelope::New("presentation-plan", 7u, 0u,
                                                 50u, "presentation-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, kTarget);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "archive-1";
  plan->snapshot_sha256 = Digest(1u);
  plan->target = target.Clone();
  for (size_t index = 0u; index < entries.size(); ++index) {
    const auto [kind, action] = entries[index];
    const bool deletion = action == Action::kStageDeletion;
    plan->entries.push_back(mojom::BackupRestorePlanEntry::New(
        kind, "record-" + base::NumberToString(index + 1u), 1u, action, 1u,
        deletion ? mojom::BackupRecordState::kTombstone
                 : mojom::BackupRecordState::kActive,
        deletion ? 0u : 1u,
        deletion ? std::vector<uint8_t>(32u, 0u) : Digest(4u)));
    plan->has_conflicts |= static_cast<uint32_t>(action) >=
                           static_cast<uint32_t>(Action::kBlockedByDeletion);
  }
  plan->confirmation_sha256 = Digest(2u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), kSource, std::move(target), plan->backup_id,
      plan->snapshot_sha256, plan->confirmation_sha256);
  return plan;
}

mojom::BackupRestoreCandidateWitnessPtr Witness(
    const mojom::BackupRestorePlanResult& plan) {
  std::vector<Kind> selection;
  for (const auto& entry : plan.entries) {
    selection.push_back(entry->kind);
  }
  std::ranges::sort(selection);
  selection.erase(std::ranges::unique(selection).begin(), selection.end());
  return mojom::BackupRestoreCandidateWitness::New(
      std::move(selection), plan.entries.size(), Digest(3u));
}

class BackupRestoreRecoveryPresentationTest : public testing::Test {
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

  base::DictValue Registry() const {
    return prefs_
        .GetDict(application_preferences::kBackupRestoreProfileReservations)
        .Clone();
  }

  void Store(base::DictValue registry) {
    prefs_.SetDict(application_preferences::kBackupRestoreProfileReservations,
                   std::move(registry));
  }

  BackupRestoreRecoveryPresentationResult Attach(
      base::span<const Kind> selection,
      const mojom::BackupRestorePlanResult& plan,
      std::u16string label = u"Restored profile") {
    return AttachBackupRestoreRecoveryPresentation(
        &prefs_, kReservation, std::move(label), selection, plan);
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(BackupRestoreRecoveryPresentationTest,
       EmptySelectedClassSurvivesInCanonicalOrderWithoutAuthority) {
  auto plan = Plan({{Kind::kMemoryRecord, Action::kStageCreate}});
  const std::array selection = {Kind::kMemoryRecord,
                                Kind::kAssistantConfiguration};

  auto attached = Attach(selection, *plan);

  ASSERT_TRUE(attached);
  ASSERT_EQ(2u, attached->selected_classes.size());
  EXPECT_EQ(Kind::kAssistantConfiguration, attached->selected_classes[0].kind);
  EXPECT_EQ(Kind::kMemoryRecord, attached->selected_classes[1].kind);
  EXPECT_EQ((std::array<uint32_t, 6>{}),
            attached->selected_classes[0].action_counts);
  EXPECT_EQ((std::array<uint32_t, 6>{1u, 0u, 0u, 0u, 0u, 0u}),
            attached->selected_classes[1].action_counts);
  EXPECT_FALSE(attached->has_conflicts);
  EXPECT_TRUE(attached->can_stage);

  const base::DictValue registry = Registry();
  const base::DictValue* entry = registry.FindDict(kReservation);
  ASSERT_TRUE(entry);
  EXPECT_EQ(2, entry->FindInt("version"));
  const base::DictValue* encoded = entry->FindDict("recovery_presentation");
  ASSERT_TRUE(encoded);
  EXPECT_EQ(9u, encoded->size());
  std::string serialized;
  ASSERT_TRUE(base::JSONWriter::Write(*encoded, &serialized));
  for (std::string_view forbidden :
       {"operation_id", "service_generation", "task_revision",
        "deadline_monotonic_ms", "idempotency_key", "authorization", "token",
        "generation"}) {
    EXPECT_EQ(std::string::npos, serialized.find(forbidden)) << forbidden;
  }
  // A journal result holds a mojo StructPtr, which is itself bool-testable, so
  // base::expected disables its operator bool: these assertions have to name
  // has_value().
  EXPECT_TRUE(BeginBackupRestoreCommitIntent(
                  &prefs_, kReservation, *plan->binding, *Witness(*plan),
                  "commit-once")
                  .has_value());
  EXPECT_TRUE(ReadBackupRestoreRecoveryPresentation(&prefs_, kReservation));
}

TEST(BackupRestoreRecoveryPresentationCodecTest,
     AllSixActionCountsRoundTripExactly) {
  BackupRestoreRecoveryPresentation presentation;
  presentation.target_profile_label = u"Restored profile";
  presentation.source_profile_id = kSource;
  presentation.target_profile_id = kTarget;
  presentation.backup_id = "archive-1";
  presentation.snapshot_sha256.fill(1u);
  presentation.confirmation_sha256.fill(2u);
  presentation.selected_classes.push_back(
      {.kind = Kind::kLibraryEntry, .action_counts = {1u, 2u, 3u, 4u, 5u, 6u}});
  presentation.has_conflicts = true;
  presentation.can_stage = false;

  auto encoded =
      backup_restore_recovery_presentation_internal::Encode(presentation);
  ASSERT_TRUE(encoded);
  auto decoded = backup_restore_recovery_presentation_internal::Decode(
      base::Value(encoded->Clone()));

  ASSERT_TRUE(decoded);
  EXPECT_EQ(presentation, *decoded);
}

TEST_F(BackupRestoreRecoveryPresentationTest,
       ExactAttachIsIdempotentAndReconstructsAfterRestart) {
  auto plan = Plan({{Kind::kLibraryEntry, Action::kStageCreate},
                    {Kind::kLibraryEntry, Action::kStageDeletion}});
  const std::array selection = {Kind::kLibraryEntry};
  auto first = Attach(selection, *plan);
  ASSERT_TRUE(first);
  const base::DictValue persisted = Registry();

  auto repeated = Attach(selection, *plan);
  ASSERT_TRUE(repeated);
  EXPECT_EQ(*first, *repeated);
  EXPECT_EQ(persisted, Registry());
  auto changed = Attach(selection, *plan, u"Different label");
  ASSERT_FALSE(changed);
  EXPECT_EQ(Error::kWrongPhysicalState, changed.error());
  EXPECT_EQ(persisted, Registry());

  TestingPrefServiceSimple restarted;
  application_preferences::RegisterLocalStatePreferences(restarted.registry());
  restarted.SetDict(application_preferences::kBackupRestoreProfileReservations,
                    persisted.Clone());
  auto read = ReadBackupRestoreRecoveryPresentation(&restarted, kReservation);
  ASSERT_TRUE(read);
  EXPECT_EQ(*first, *read);
  EXPECT_EQ((std::array<uint32_t, 6>{1u, 1u, 0u, 0u, 0u, 0u}),
            read->selected_classes.front().action_counts);
}

TEST_F(BackupRestoreRecoveryPresentationTest,
       InvalidInputAndWrongTargetLeaveLegacyRegistryUntouched) {
  const base::DictValue before = Registry();
  auto plan = Plan();
  const std::array supported = {Kind::kLibraryEntry};
  const std::array unsupported = {Kind::kBookmark};

  auto bad_label = Attach(supported, *plan, u" leading");
  ASSERT_FALSE(bad_label);
  EXPECT_EQ(Error::kInvalidArgument, bad_label.error());
  auto bad_selection = Attach(unsupported, *plan);
  ASSERT_FALSE(bad_selection);
  EXPECT_EQ(Error::kInvalidArgument, bad_selection.error());

  plan->target->profile_id = kOtherTarget;
  plan->binding->target->profile_id = kOtherTarget;
  auto wrong_target = Attach(supported, *plan);
  ASSERT_FALSE(wrong_target);
  EXPECT_EQ(Error::kWrongPhysicalState, wrong_target.error());
  EXPECT_EQ(before, Registry());
}

TEST_F(BackupRestoreRecoveryPresentationTest,
       LegacyVersionOneIsReadableButNeverReconstructed) {
  auto absent = ReadBackupRestoreRecoveryPresentation(&prefs_, kReservation);
  ASSERT_FALSE(absent);
  EXPECT_EQ(Error::kNotFound, absent.error());
  auto plan = Plan({{Kind::kLibraryEntry, Action::kStageCreate}});
  auto intent = BeginBackupRestoreCommitIntent(
      &prefs_, kReservation, *plan->binding, *Witness(*plan), "commit-once");
  ASSERT_TRUE(intent.has_value());
  EXPECT_TRUE(ReadBackupRestoreProfileReservations(&prefs_));

  auto still_absent =
      ReadBackupRestoreRecoveryPresentation(&prefs_, kReservation);
  ASSERT_FALSE(still_absent);
  EXPECT_EQ(Error::kNotFound, still_absent.error());
  const std::array selection = {Kind::kLibraryEntry};
  auto late_attach = Attach(selection, *plan);
  ASSERT_FALSE(late_attach);
  EXPECT_EQ(Error::kWrongPhysicalState, late_attach.error());
}

TEST_F(BackupRestoreRecoveryPresentationTest,
       FirstJournalBindingMismatchFailsBeforeDirtyingLocalState) {
  auto plan = Plan({{Kind::kLibraryEntry, Action::kStageCreate}});
  const std::array selection = {Kind::kLibraryEntry};
  ASSERT_TRUE(Attach(selection, *plan));
  const base::DictValue before = Registry();

  auto expect_refused = [&](mojom::BackupRestoreBindingPtr binding,
                            mojom::BackupRestoreCandidateWitnessPtr witness) {
    auto intent = BeginBackupRestoreCommitIntent(
        &prefs_, kReservation, *binding, *witness, "commit-once");
    ASSERT_FALSE(intent.has_value());
    EXPECT_EQ(Error::kInvalidArgument, intent.error());
    EXPECT_EQ(before, Registry());
  };
  auto binding = plan->binding.Clone();
  binding->owner_profile_id = "different-source";
  expect_refused(std::move(binding), Witness(*plan));
  binding = plan->binding.Clone();
  binding->backup_id = "different-archive";
  expect_refused(std::move(binding), Witness(*plan));
  binding = plan->binding.Clone();
  binding->snapshot_sha256[0] ^= 1u;
  expect_refused(std::move(binding), Witness(*plan));
  binding = plan->binding.Clone();
  binding->confirmation_sha256[0] ^= 1u;
  expect_refused(std::move(binding), Witness(*plan));
  binding = plan->binding.Clone();
  binding->target->profile_id = kOtherTarget;
  expect_refused(std::move(binding), Witness(*plan));
  auto witness = Witness(*plan);
  witness->record_count = 2u;
  expect_refused(plan->binding.Clone(), std::move(witness));
  witness = Witness(*plan);
  witness->selection = {Kind::kMemoryRecord};
  expect_refused(plan->binding.Clone(), std::move(witness));

  auto exact = BeginBackupRestoreCommitIntent(
      &prefs_, kReservation, *plan->binding, *Witness(*plan), "commit-once");
  ASSERT_TRUE(exact.has_value());
  EXPECT_TRUE(ReadBackupRestoreRecoveryPresentation(&prefs_, kReservation));
}

TEST_F(BackupRestoreRecoveryPresentationTest,
       PersistedFirstBindingCountMismatchCorruptsWholeRegistry) {
  auto plan = Plan({{Kind::kLibraryEntry, Action::kStageCreate}});
  const std::array selection = {Kind::kLibraryEntry};
  ASSERT_TRUE(Attach(selection, *plan));
  ASSERT_TRUE(BeginBackupRestoreCommitIntent(
                  &prefs_, kReservation, *plan->binding, *Witness(*plan),
                  "commit-once")
                  .has_value());
  base::DictValue corrupted = Registry();
  base::DictValue* entry = corrupted.FindDict(kReservation);
  ASSERT_TRUE(entry);
  base::ListValue* journal = entry->FindList("recovery_journal");
  ASSERT_TRUE(journal);
  ASSERT_EQ(1u, journal->size());
  ASSERT_TRUE(journal->front().is_dict());
  base::DictValue* binding = journal->front().GetDict().FindDict("binding");
  ASSERT_TRUE(binding);
  binding->Set("record_count", "2");
  Store(std::move(corrupted));

  auto reservations = ReadBackupRestoreProfileReservations(&prefs_);
  ASSERT_FALSE(reservations);
  EXPECT_EQ(Error::kCorrupt, reservations.error());
  auto presentation =
      ReadBackupRestoreRecoveryPresentation(&prefs_, kReservation);
  ASSERT_FALSE(presentation);
  EXPECT_EQ(Error::kCorrupt, presentation.error());
}

}  // namespace
}  // namespace taffy
