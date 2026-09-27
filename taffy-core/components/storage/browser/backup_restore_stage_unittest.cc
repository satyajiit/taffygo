// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_restore_stage.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/scoped_error_expecter.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_restore_stage_internal.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {

class BackupRestoreStageTestPeer {
 public:
  static void SetDeletionGate(BackupRestoreStage* stage,
                              base::RepeatingCallback<bool()> gate) {
    stage->deletion_gate_for_testing_ = std::move(gate);
  }
};

namespace {

namespace mojom = core_service::mojom;
constexpr char kId[] = "11111111111111111111111111111111";
constexpr auto kMemory = mojom::BackupRecordKind::kMemoryRecord;
constexpr auto kLibrary = mojom::BackupRecordKind::kLibraryEntry;
constexpr auto kConfiguration =
    mojom::BackupRecordKind::kAssistantConfiguration;

class BackupRestoreStageTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    staging_parent_ = directory_.GetPath().AppendASCII("staging");
    ASSERT_TRUE(base::CreateDirectory(staging_parent_));
    payload_path_ = directory_.GetPath().AppendASCII("payload");
    plan_ = mojom::BackupRestorePlanResult::New();
    plan_->status = mojom::BackupPlanningStatus::kSucceeded;
    plan_->operation = mojom::OperationEnvelope::New("backup-operation", 1, 0,
                                                     10000, "backup-operation");
    plan_->backup_id = "backup-test-identity";
    plan_->target = mojom::BackupRestoreTarget::New(
        mojom::BackupRestoreTargetKind::kNewRegularProfile,
        "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    // Synthetic Core-result digests: archive authentication is tested at the
    // archive/planner seams. This suite proves physical staging reverification.
    plan_->snapshot_sha256.assign(32, 1);
    plan_->confirmation_sha256.assign(32, 2);
  }

  void Append(mojom::BackupRecordKind kind,
              std::string id,
              uint64_t revision,
              std::vector<uint8_t> bytes,
              bool tombstone = false) {
    auto entry = mojom::BackupRestorePlanEntry::New();
    entry->kind = kind;
    entry->stable_id = std::move(id);
    entry->archive_revision = revision;
    entry->schema_version = 1;
    entry->state = tombstone ? mojom::BackupRecordState::kTombstone
                             : mojom::BackupRecordState::kActive;
    entry->action = tombstone ? mojom::BackupRestoreAction::kStageDeletion
                              : mojom::BackupRestoreAction::kStageCreate;
    entry->plaintext_bytes = bytes.size();
    entry->plaintext_sha256.assign(32, 0);
    if (!tombstone) {
      const auto digest = crypto::hash::Sha256(bytes);
      entry->plaintext_sha256.assign(digest.begin(), digest.end());
    }
    payload_.insert(payload_.end(), bytes.begin(), bytes.end());
    plan_->entries.push_back(std::move(entry));
  }

  void AppendConfiguration() {
    auto record = mojom::AssistantConfiguration::New(
        7,
        std::vector{mojom::AssistantAbility::kPagesLookup,
                    mojom::AssistantAbility::kKeep},
        mojom::PersonalityPreset::kTripPlanner, 1, 2, 0);
    auto bytes = EncodeAssistantConfigurationV1(*record);
    ASSERT_TRUE(bytes.has_value());
    Append(kConfiguration, kAssistantConfigurationBackupStableId, 7,
           std::move(*bytes));
  }

  void AppendMemory() {
    auto record = mojom::MemoryRecord::New();
    record->memory_id = kId;
    record->revision = 42;
    record->statement = "Keep this exact preference";
    record->source_kind = mojom::MemorySourceKind::kUserEntered;
    record->scope_kind = mojom::MemoryScopeKind::kAllTasks;
    record->sensitivity = mojom::MemorySensitivity::kStandard;
    record->created_at_epoch_ms = 1000;
    record->updated_at_epoch_ms = 2000;
    auto bytes = EncodeMemoryRecordV1(*record);
    ASSERT_TRUE(bytes.has_value());
    Append(kMemory, kId, 42, std::move(*bytes));
  }

  void AppendLibrary() {
    std::vector<mojom::LibrarySourceRecordPtr> sources;
    sources.push_back(mojom::LibrarySourceRecord::New(
        "55555555555555555555555555555555", "Specifications", "maker.example",
        1000));
    auto record = mojom::LibraryEntryRecord::New(
        kId, 8, "22222222222222222222222222222222", "Research",
        "33333333333333333333333333333333", 7,
        "44444444444444444444444444444444", "warranty", "two years",
        std::optional<std::string>("three years"),
        mojom::LibraryFactKind::kFromPage, std::move(sources), 2000, 1000,
        false);
    auto bytes = EncodeLibraryRecordV1(*record);
    ASSERT_TRUE(bytes.has_value());
    Append(kLibrary, kId, 8, std::move(*bytes));
  }

  auto Build() {
    EXPECT_TRUE(base::WriteFile(payload_path_, payload_));
    return BackupRestoreStage::Create(
        staging_parent_, *plan_, plan_->confirmation_sha256,
        base::File(payload_path_,
                   base::File::FLAG_OPEN | base::File::FLAG_READ));
  }

  bool NoStageChildren() {
    base::FileEnumerator children(
        staging_parent_, false,
        base::FileEnumerator::DIRECTORIES | base::FileEnumerator::FILES);
    return children.Next().empty();
  }

  base::ScopedTempDir directory_;
  base::FilePath staging_parent_;
  base::FilePath payload_path_;
  mojom::BackupRestorePlanResultPtr plan_;
  std::vector<uint8_t> payload_;
};

TEST_F(BackupRestoreStageTest, ThreeTypedFamiliesRoundTripWithExactRevisions) {
  AppendLibrary();
  AppendConfiguration();
  AppendMemory();
  auto stage = Build();
  ASSERT_TRUE(stage.has_value());
  EXPECT_TRUE((*stage)->Verify());
  auto verified = (*stage)->ReadVerifiedRecords();
  ASSERT_TRUE(verified.has_value());
  EXPECT_EQ(verified->size(), 3u);
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open((*stage)->database_path()));
  auto records = ReadSelectedBackupRecords(
      &database, std::array{kLibrary, kConfiguration, kMemory});
  ASSERT_TRUE(records.has_value());
  ASSERT_EQ(records->size(), 3u);
  EXPECT_TRUE(
      DecodeLibraryRecordV1((*records)[0].plaintext, kId, 8).has_value());
  EXPECT_TRUE(
      DecodeAssistantConfigurationV1((*records)[1].plaintext, 7).has_value());
  EXPECT_TRUE(
      DecodeMemoryRecordV1((*records)[2].plaintext, kId, 42).has_value());
  for (auto table :
       {"core_account_session", "core_effect_journal", "core_task_aggregate",
        "core_task_action_journal", "core_sync_binding", "core_sync_head"}) {
    const std::string query = "SELECT count(*) FROM " + std::string(table);
    sql::Statement count(database.GetUniqueStatement(query));
    ASSERT_TRUE(count.Step());
    EXPECT_EQ(count.ColumnInt(0), 0) << table;
  }
}

TEST_F(BackupRestoreStageTest, DeletionMarkersSurviveWithoutInventedContent) {
  Append(kLibrary, kId, 3, {}, true);
  Append(kMemory, kId, 5, {}, true);
  auto stage = Build();
  ASSERT_TRUE(stage.has_value());
  EXPECT_TRUE((*stage)->Verify());
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open((*stage)->database_path()));
  auto records =
      ReadSelectedBackupRecords(&database, std::array{kLibrary, kMemory});
  ASSERT_TRUE(records.has_value());
  ASSERT_EQ(records->size(), 2u);
  EXPECT_EQ((*records)[0].descriptor->revision, 3u);
  EXPECT_EQ((*records)[1].descriptor->revision, 5u);
  EXPECT_TRUE((*records)[0].plaintext.empty());
  EXPECT_TRUE((*records)[1].plaintext.empty());
}

TEST_F(BackupRestoreStageTest, DifferentConfirmationRefusesBeforeStaging) {
  AppendMemory();
  ASSERT_TRUE(base::WriteFile(payload_path_, payload_));
  auto confirmed = plan_->confirmation_sha256;
  confirmed[0] ^= 1;
  auto result = BackupRestoreStage::Create(
      staging_parent_, *plan_, confirmed,
      base::File(payload_path_, base::File::FLAG_OPEN | base::File::FLAG_READ));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), BackupRestoreStageError::kPlanRefused);
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, ExistingTargetOrUnresolvedConflictIsNotAWrite) {
  AppendMemory();
  plan_->target->kind = mojom::BackupRestoreTargetKind::kExistingRegularProfile;
  EXPECT_FALSE(Build().has_value());
  plan_->target->kind = mojom::BackupRestoreTargetKind::kNewRegularProfile;
  plan_->has_conflicts = true;
  EXPECT_FALSE(Build().has_value());
  plan_->has_conflicts = false;
  for (auto action :
       {mojom::BackupRestoreAction::kAlreadyPresent,
        mojom::BackupRestoreAction::kKeepNewerCurrent,
        mojom::BackupRestoreAction::kBlockedByDeletion,
        mojom::BackupRestoreAction::kNeedsExplicitConflictChoice}) {
    plan_->entries[0]->action = action;
    EXPECT_FALSE(Build().has_value());
  }
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, UnknownSchemaKindAndDuplicateIdentityRefused) {
  AppendMemory();
  plan_->entries[0]->schema_version = 2;
  EXPECT_FALSE(Build().has_value());
  plan_->entries[0]->schema_version = 1;
  plan_->entries[0]->kind = mojom::BackupRecordKind::kSavedWorkspace;
  EXPECT_FALSE(Build().has_value());
  plan_->entries[0]->kind = kMemory;
  plan_->entries.push_back(plan_->entries[0].Clone());
  EXPECT_FALSE(Build().has_value());
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, TruncationTrailingBytesAndModifiedBytesRefused) {
  AppendMemory();
  const auto intact = payload_;
  payload_.pop_back();
  EXPECT_FALSE(Build().has_value());
  payload_ = intact;
  payload_.push_back(0);
  EXPECT_FALSE(Build().has_value());
  payload_ = intact;
  payload_.back() ^= 1;
  EXPECT_FALSE(Build().has_value());
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, MatchingDigestStillRequiresTypedRecordDecode) {
  AppendConfiguration();
  Append(kMemory, kId, 1, {1, 2, 3, 4});
  EXPECT_FALSE(Build().has_value());
  // The configuration inserted before the malformed second record is not
  // published anywhere, and the failed isolated store is removed completely.
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, DescriptorRevisionIsBoundToPayloadRevision) {
  AppendMemory();
  plan_->entries[0]->archive_revision = 43;
  EXPECT_FALSE(Build().has_value());
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, MalformedDeletionMetadataNeverStages) {
  Append(kMemory, kId, 1, {}, true);
  EXPECT_FALSE(Build().has_value());
  plan_->entries[0]->archive_revision = 3;
  plan_->entries[0]->plaintext_sha256[0] = 1;
  EXPECT_FALSE(Build().has_value());
  plan_->entries[0]->plaintext_sha256[0] = 0;
  plan_->entries[0]->action = mojom::BackupRestoreAction::kStageCreate;
  EXPECT_FALSE(Build().has_value());
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, ReverificationDetectsChangedStagedRecord) {
  AppendMemory();
  auto stage = Build();
  ASSERT_TRUE(stage.has_value());
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open((*stage)->database_path()));
    ASSERT_TRUE(database.Execute(
        "UPDATE core_memory_record SET statement='changed after staging'"));
  }
  EXPECT_FALSE((*stage)->Verify());
  auto records = (*stage)->ReadVerifiedRecords();
  ASSERT_FALSE(records.has_value());
  EXPECT_EQ(records.error(), BackupRestoreStageError::kPayloadMismatch);
}

TEST_F(BackupRestoreStageTest, ReverificationDetectsChangedTargetIdentity) {
  AppendConfiguration();
  auto stage = Build();
  ASSERT_TRUE(stage.has_value());
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open((*stage)->database_path()));
    ASSERT_TRUE(
        database.Execute("UPDATE core_profile_identity SET "
                         "browser_profile_id='another-profile'"));
  }
  EXPECT_FALSE((*stage)->Verify());
}

TEST_F(BackupRestoreStageTest, EmptyArchiveDoesNotInventSettingsOrRecords) {
  auto stage = Build();
  ASSERT_TRUE(stage.has_value());
  EXPECT_TRUE((*stage)->Verify());
  const auto path = (*stage)->database_path();
  stage->reset();
  EXPECT_FALSE(base::PathExists(path));
  EXPECT_TRUE(NoStageChildren());
}

TEST_F(BackupRestoreStageTest, FailedDeletionRetainsExactCustodyForRetry) {
  AppendMemory();
  auto stage = Build();
  ASSERT_TRUE(stage.has_value());
  const base::FilePath database_path = (*stage)->database_path();
  const base::FilePath stage_path = database_path.DirName();
  int attempts = 0;
  BackupRestoreStageTestPeer::SetDeletionGate(
      stage->get(),
      base::BindLambdaForTesting([&attempts] { return ++attempts > 1; }));

  EXPECT_FALSE((*stage)->Delete());
  EXPECT_EQ((*stage)->database_path(), database_path);
  EXPECT_TRUE(base::PathExists(stage_path));
  EXPECT_TRUE((*stage)->Verify());

  EXPECT_TRUE((*stage)->Delete());
  EXPECT_TRUE((*stage)->database_path().empty());
  EXPECT_FALSE(base::PathExists(stage_path));
  EXPECT_FALSE((*stage)->Verify());
  EXPECT_TRUE((*stage)->Delete());
  EXPECT_EQ(attempts, 2);
}

TEST_F(BackupRestoreStageTest, VerificationNeverRecreatesMissingCandidate) {
  AppendMemory();
  auto stage = Build();
  ASSERT_TRUE(stage.has_value());
  const auto path = (*stage)->database_path();
  ASSERT_TRUE(base::DeleteFile(path));
  EXPECT_FALSE((*stage)->Verify());
  EXPECT_FALSE(base::PathExists(path));
  EXPECT_FALSE((*stage)->ReadVerifiedRecords().has_value());
  EXPECT_TRUE((*stage)->Delete());
}

TEST_F(BackupRestoreStageTest,
       InsertRecordSeparatesPayloadFromPhysicalStorageFailure) {
  AppendMemory();
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.OpenInMemory());

  const std::array<uint8_t, 4> malformed = {1, 2, 3, 4};
  auto malformed_result =
      restore_internal::InsertRecord(&database, *plan_->entries[0], malformed);
  ASSERT_FALSE(malformed_result.has_value());
  EXPECT_EQ(malformed_result.error(),
            BackupRestoreStageError::kPayloadMismatch);

  sql::test::ScopedErrorExpecter error_expecter;
  error_expecter.ExpectError(SQLITE_ERROR);
  auto storage_result =
      restore_internal::InsertRecord(&database, *plan_->entries[0], payload_);
  ASSERT_FALSE(storage_result.has_value());
  EXPECT_EQ(storage_result.error(),
            BackupRestoreStageError::kStorageUnavailable);
  EXPECT_TRUE(error_expecter.SawExpectedErrors());
}

}  // namespace
}  // namespace taffy::storage::backup
