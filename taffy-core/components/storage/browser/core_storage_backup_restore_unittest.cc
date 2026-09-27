// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "crypto/hash.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Error = storage::backup::BackupRestoreStageError;

class CoreStorageBackupRestoreTest : public testing::Test {
 protected:
  void TearDown() override {
    broker_.reset();
    environment_.RunUntilIdle();
  }

  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    database_path_ = directory_.GetPath().AppendASCII("core.sqlite3");
    payload_path_ = directory_.GetPath().AppendASCII("payload");
    broker_ = std::make_unique<CoreStorageBroker>(database_path_, false);
    mojom::CoreBootstrapPtr bootstrap;
    base::RunLoop load;
    broker_->LoadBootstrap(
        1, false,
        base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
          bootstrap = std::move(value);
          load.Quit();
        }));
    load.Run();
    ASSERT_TRUE(bootstrap);
    plan_ = mojom::BackupRestorePlanResult::New();
    plan_->operation = mojom::OperationEnvelope::New(
        "restore-operation", 1, 0,
        (base::TimeTicks::Now() + base::Minutes(5))
            .since_origin()
            .InMilliseconds(),
        "restore-operation");
    plan_->status = mojom::BackupPlanningStatus::kSucceeded;
    plan_->backup_id = "backup-test-identity";
    plan_->target = mojom::BackupRestoreTarget::New(
        mojom::BackupRestoreTargetKind::kNewRegularProfile,
        bootstrap->browser_profile_id);
    plan_->snapshot_sha256.assign(32, 1);
    plan_->confirmation_sha256.assign(32, 2);
    auto configuration = mojom::AssistantConfiguration::New(
        7, std::vector<mojom::AssistantAbility>{},
        mojom::PersonalityPreset::kQuickShopper, 1, 2, 0);
    auto encoded =
        storage::backup::EncodeAssistantConfigurationV1(*configuration);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_TRUE(base::WriteFile(payload_path_, *encoded));
    auto entry = mojom::BackupRestorePlanEntry::New();
    entry->kind = mojom::BackupRecordKind::kAssistantConfiguration;
    entry->stable_id = storage::backup::kAssistantConfigurationBackupStableId;
    entry->archive_revision = 7;
    entry->action = mojom::BackupRestoreAction::kStageCreate;
    entry->schema_version = 1;
    entry->state = mojom::BackupRecordState::kActive;
    entry->plaintext_bytes = encoded->size();
    const auto digest = crypto::hash::Sha256(*encoded);
    entry->plaintext_sha256.assign(digest.begin(), digest.end());
    plan_->entries.push_back(std::move(entry));
  }

  base::expected<void, Error> Stage(CoreStorageBroker* broker = nullptr) {
    std::optional<base::expected<void, Error>> result;
    base::RunLoop loop;
    (broker ? broker : broker_.get())
        ->StageBackupRestore(
            *plan_, plan_->confirmation_sha256,
            base::File(payload_path_,
                       base::File::FLAG_OPEN | base::File::FLAG_READ),
            base::BindLambdaForTesting([&](base::expected<void, Error> value) {
              result.emplace(std::move(value));
              loop.Quit();
            }));
    loop.Run();
    return std::move(*result);
  }

  bool Verify() {
    bool result = false;
    base::RunLoop loop;
    broker_->VerifyBackupRestore(*plan_->operation, plan_->snapshot_sha256,
                                 plan_->confirmation_sha256,
                                 base::BindLambdaForTesting([&](bool value) {
                                   result = value;
                                   loop.Quit();
                                 }));
    loop.Run();
    return result;
  }

  bool Abandon(std::string operation_id) {
    bool result = false;
    base::RunLoop loop;
    broker_->AbandonBackupRestore(std::move(operation_id),
                                  base::BindLambdaForTesting([&](bool value) {
                                    result = value;
                                    loop.Quit();
                                  }));
    loop.Run();
    return result;
  }

  void LoadGeneration(uint64_t generation) {
    base::RunLoop loop;
    broker_->LoadBootstrap(
        generation, false,
        base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
          EXPECT_TRUE(value);
          loop.Quit();
        }));
    loop.Run();
  }

  void WriteConfiguration() {
    auto effect = mojom::EffectEnvelope::New();
    effect->operation = plan_->operation.Clone();
    effect->effect_id = "unrelated-configuration-write";
    effect->kind = mojom::EffectKind::kStorageCommit;
    effect->storage_commit = mojom::StorageCommitEffect::New();
    effect->storage_commit->task_id_seed.assign(32, 0);
    effect->storage_commit->operation_kind =
        mojom::StorageOperation::kSetAssistantConfiguration;
    effect->storage_commit->resulting_revision = 1;
    effect->storage_commit->assistant_configuration =
        mojom::AssistantConfigurationPersistEffect::New();
    base::RunLoop loop;
    broker_->DispatchStorage(
        std::move(effect),
        base::BindLambdaForTesting([&](mojom::EffectResultPtr value) {
          EXPECT_TRUE(value);
          if (value) {
            EXPECT_EQ(value->status, mojom::EffectStatus::kCompleted);
          }
          loop.Quit();
        }));
    loop.Run();
  }

  void ExpectLiveConfigurationRevision(uint64_t revision) {
    base::RunLoop loop;
    broker_->ReadBackupSnapshot(
        {mojom::BackupRecordKind::kAssistantConfiguration},
        base::BindLambdaForTesting(
            [&](storage::backup::BackupSnapshotResult value) {
              EXPECT_TRUE(value.has_value());
              if (value.has_value() && value->size() == 1) {
                EXPECT_EQ(value->front().descriptor->revision, revision);
              } else {
                ADD_FAILURE() << "Expected exactly one live configuration";
              }
              loop.Quit();
            }));
    loop.Run();
  }

  base::test::TaskEnvironment environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir directory_;
  base::FilePath database_path_;
  base::FilePath payload_path_;
  std::unique_ptr<CoreStorageBroker> broker_;
  mojom::BackupRestorePlanResultPtr plan_;
};

TEST_F(CoreStorageBackupRestoreTest, StageIsVerifiedButChangesNoLiveRecord) {
  ASSERT_TRUE(Stage().has_value());
  EXPECT_TRUE(Verify());
  std::optional<storage::backup::BackupSnapshotResult> live;
  base::RunLoop loop;
  broker_->ReadBackupSnapshot(
      {mojom::BackupRecordKind::kAssistantConfiguration},
      base::BindLambdaForTesting(
          [&](storage::backup::BackupSnapshotResult value) {
            live.emplace(std::move(value));
            loop.Quit();
          }));
  loop.Run();
  ASSERT_TRUE(live.has_value());
  ASSERT_TRUE(live->has_value());
  EXPECT_TRUE((**live).empty());
  EXPECT_TRUE(Abandon(plan_->operation->operation_id));
  EXPECT_FALSE(Verify());
  EXPECT_TRUE(Abandon(plan_->operation->operation_id));
}

TEST_F(CoreStorageBackupRestoreTest,
       BusyStageCannotBeReplacedByAnotherRequest) {
  ASSERT_TRUE(Stage().has_value());
  auto duplicate = Stage();
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), Error::kStageBusy);
  plan_->operation->operation_id = "different-operation";
  auto other = Stage();
  ASSERT_FALSE(other.has_value());
  EXPECT_EQ(other.error(), Error::kStageBusy);
  EXPECT_FALSE(Abandon("different-operation"));
  plan_->operation->operation_id = "restore-operation";
  EXPECT_TRUE(Verify());
}

TEST_F(CoreStorageBackupRestoreTest, ChangedGenerationOrDigestCannotVerify) {
  ASSERT_TRUE(Stage().has_value());
  ++plan_->operation->service_generation;
  EXPECT_FALSE(Verify());
  --plan_->operation->service_generation;
  plan_->snapshot_sha256[0] ^= 1;
  EXPECT_FALSE(Verify());
  plan_->snapshot_sha256[0] ^= 1;
  plan_->confirmation_sha256[0] ^= 1;
  EXPECT_FALSE(Verify());
  plan_->confirmation_sha256[0] ^= 1;
  EXPECT_TRUE(Verify());
}

TEST_F(CoreStorageBackupRestoreTest, ExpiredOperationCannotStageOrVerify) {
  ASSERT_TRUE(Stage().has_value());
  environment_.FastForwardBy(base::Minutes(6));
  EXPECT_FALSE(Verify());
  EXPECT_TRUE(Abandon(plan_->operation->operation_id));
  auto result = Stage();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kPlanRefused);
}

TEST_F(CoreStorageBackupRestoreTest, AnotherTargetProfileCannotUseThisWriter) {
  plan_->target->profile_id = "another-profile";
  auto result = Stage();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kPlanRefused);
  EXPECT_FALSE(base::PathExists(
      directory_.GetPath().AppendASCII("TaffyRestoreStaging")));
}

TEST_F(CoreStorageBackupRestoreTest, PrivateWriterRefusesBeforeCreatingFiles) {
  const auto private_path = directory_.GetPath().AppendASCII("private.sqlite3");
  CoreStorageBroker private_broker(private_path, true);
  auto result = Stage(&private_broker);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kPlanRefused);
  EXPECT_FALSE(base::PathExists(private_path));
  EXPECT_FALSE(base::PathExists(
      directory_.GetPath().AppendASCII("TaffyRestoreStaging")));
}

TEST_F(CoreStorageBackupRestoreTest,
       OldUncommittedStagesAreCleanedInExactChild) {
  const auto staging = directory_.GetPath().AppendASCII("TaffyRestoreStaging");
  ASSERT_TRUE(base::CreateDirectory(staging.AppendASCII("old-operation")));
  const auto abandoned = staging.AppendASCII("old-operation/payload");
  ASSERT_TRUE(base::WriteFile(abandoned, "left by process death"));
  const auto unrelated = directory_.GetPath().AppendASCII("keep-this-file");
  ASSERT_TRUE(base::WriteFile(unrelated, "outside staging ownership"));
  ASSERT_TRUE(Stage().has_value());
  EXPECT_FALSE(base::PathExists(abandoned));
  EXPECT_TRUE(base::PathExists(unrelated));
  EXPECT_TRUE(Verify());
}

TEST_F(CoreStorageBackupRestoreTest, ActualNewCoreGenerationRevokesOldStage) {
  ASSERT_TRUE(Stage().has_value());
  LoadGeneration(2);
  EXPECT_FALSE(Verify());
  auto stale = Stage();
  ASSERT_FALSE(stale.has_value());
  EXPECT_EQ(stale.error(), Error::kPlanRefused);
  plan_->operation->service_generation = 2;
  ASSERT_TRUE(Stage().has_value());
  EXPECT_TRUE(Verify());
}

TEST_F(CoreStorageBackupRestoreTest, ExpiredStageDoesNotOccupySlotOnRetry) {
  ASSERT_TRUE(Stage().has_value());
  environment_.FastForwardBy(base::Minutes(6));
  plan_->operation->operation_id = "fresh-restore";
  plan_->operation->deadline_monotonic_ms =
      (base::TimeTicks::Now() + base::Minutes(5))
          .since_origin()
          .InMilliseconds();
  ASSERT_TRUE(Stage().has_value());
  EXPECT_TRUE(Verify());
}

TEST_F(CoreStorageBackupRestoreTest, TargetChangedAfterPlanCannotBeStaged) {
  WriteConfiguration();
  auto result = Stage();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kPlanRefused);
  ExpectLiveConfigurationRevision(1);
}

TEST_F(CoreStorageBackupRestoreTest, TargetChangedAfterStageCannotBeVerified) {
  ASSERT_TRUE(Stage().has_value());
  WriteConfiguration();
  EXPECT_FALSE(Verify());
  EXPECT_TRUE(Abandon(plan_->operation->operation_id));
  ExpectLiveConfigurationRevision(1);
}

TEST_F(CoreStorageBackupRestoreTest, InvalidOperationTextNeverCreatesStage) {
  for (const std::string& operation :
       {std::string(mojom::kMaxOperationIdBytes + 1, 'x'),
        std::string("control\nbyte"), std::string("\xff")}) {
    plan_->operation->operation_id = operation;
    EXPECT_FALSE(Stage().has_value());
  }
  EXPECT_FALSE(base::PathExists(
      directory_.GetPath().AppendASCII("TaffyRestoreStaging")));
}

TEST_F(CoreStorageBackupRestoreTest, InvalidIdempotencyOrTaskRevisionRefused) {
  for (const std::string& key :
       {std::string(), std::string(mojom::kMaxIdempotencyKeyBytes + 1, 'x'),
        std::string("control\nbyte"), std::string("\xff")}) {
    plan_->operation->idempotency_key = key;
    EXPECT_FALSE(Stage().has_value());
  }
  plan_->operation->idempotency_key = "backup-idempotency";
  plan_->operation->task_revision = 1;
  EXPECT_FALSE(Stage().has_value());
  EXPECT_FALSE(base::PathExists(
      directory_.GetPath().AppendASCII("TaffyRestoreStaging")));
}

}  // namespace
}  // namespace taffy
