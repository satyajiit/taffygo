// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
using Error = DormantBackupRestoreStageError;

constexpr char kSourceProfileId[] = "11111111-1111-4111-8111-111111111111";
constexpr char kStagingChild[] = "TaffyRestoreStaging";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

class DormantBackupRestoreTargetStagingTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(root_.CreateUniqueTempDir());
    profile_path_ = root_.GetPath().AppendASCII("Profile 1");
    ASSERT_TRUE(base::CreateDirectory(profile_path_));
    target_profile_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto created = DormantBackupRestoreTarget::Create(
        profile_path_, target_profile_id_, false);
    ASSERT_TRUE(created.has_value());
    target_ = std::move(*created);

    auto configuration = mojom::AssistantConfiguration::New(
        7u, std::vector<mojom::AssistantAbility>{},
        mojom::PersonalityPreset::kQuickShopper, 1u, 2u, 0u);
    auto encoded = EncodeAssistantConfigurationV1(*configuration);
    ASSERT_TRUE(encoded.has_value());
    payload_ = std::move(*encoded);
  }

  mojom::BackupRestorePlanResultPtr Plan(uint64_t planning_deadline = 1u) {
    auto operation = mojom::OperationEnvelope::New(
        "restore-plan", 7u, 0u, planning_deadline, "restore-plan-once");
    auto target = mojom::BackupRestoreTarget::New(
        mojom::BackupRestoreTargetKind::kNewRegularProfile, target_profile_id_);
    auto plan = mojom::BackupRestorePlanResult::New();
    plan->operation = operation.Clone();
    plan->status = mojom::BackupPlanningStatus::kSucceeded;
    plan->backup_id = "backup-test-identity";
    plan->snapshot_sha256.assign(32u, 4u);
    plan->target = target.Clone();
    plan->confirmation_sha256.assign(32u, 5u);
    plan->binding = mojom::BackupRestoreBinding::New(
        std::move(operation), kSourceProfileId, std::move(target),
        plan->backup_id, plan->snapshot_sha256, plan->confirmation_sha256);

    auto entry = mojom::BackupRestorePlanEntry::New();
    entry->kind = mojom::BackupRecordKind::kAssistantConfiguration;
    entry->stable_id = kAssistantConfigurationBackupStableId;
    entry->archive_revision = 7u;
    entry->action = mojom::BackupRestoreAction::kStageCreate;
    entry->schema_version = 1u;
    entry->state = mojom::BackupRecordState::kActive;
    entry->plaintext_bytes = payload_.size();
    const auto digest = crypto::hash::Sha256(payload_);
    entry->plaintext_sha256.assign(digest.begin(), digest.end());
    plan->entries.push_back(std::move(entry));
    return plan;
  }

  mojom::BackupRestoreStageAuthorizationPtr Authorization(
      const mojom::BackupRestorePlanResult& plan,
      uint64_t deadline = 0u) {
    return mojom::BackupRestoreStageAuthorization::New(
        plan.binding.Clone(),
        mojom::OperationEnvelope::New(
            "restore-confirm", 7u, 0u,
            deadline == 0u ? NowMonotonicMillis() + 60'000u : deadline,
            "restore-confirm-once"));
  }

  base::File PayloadFile() {
    const auto path = root_.GetPath().AppendASCII(
        "payload-" + std::to_string(++payload_ordinal_));
    EXPECT_TRUE(base::WriteFile(path, payload_));
    return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  }

  base::File TruncatedPayloadFile() {
    const auto path = root_.GetPath().AppendASCII("truncated-payload");
    EXPECT_TRUE(base::WriteFile(path, base::span(payload_).first(1u)));
    return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  }

  base::FilePath StagingPath() const {
    return target_->database_path().DirName().AppendASCII(kStagingChild);
  }

  void ExpectTargetConfigurationEmpty() {
    const base::FilePath database_path = target_->database_path();
    target_.reset();
    sql::Database database(sql::DatabaseOptions().set_read_only(true),
                           sql::test::kTestTag);
    ASSERT_TRUE(database.Open(database_path));
    sql::Statement rows(database.GetUniqueStatement(
        "SELECT COUNT(*) FROM core_assistant_configuration"));
    ASSERT_TRUE(rows.Step());
    EXPECT_EQ(rows.ColumnInt64(0), 0);
  }

  base::ScopedTempDir root_;
  base::FilePath profile_path_;
  std::string target_profile_id_;
  std::unique_ptr<DormantBackupRestoreTarget> target_;
  std::vector<uint8_t> payload_;
  uint64_t payload_ordinal_ = 0u;
};

TEST_F(DormantBackupRestoreTargetStagingTest,
       ExactAuthorityStagesAndReadsBackWithoutChangingTarget) {
  auto plan = Plan();
  auto retry_plan = plan.Clone();
  auto authorization = Authorization(*plan);
  auto retry_authorization = authorization.Clone();
  auto result = target_->StageAuthorized(
      std::move(plan), std::move(authorization), PayloadFile());

  ASSERT_TRUE(result.has_value());
  std::array<uint8_t, 32> expected_digest{};
  expected_digest.fill(4u);
  EXPECT_EQ(result->snapshot_sha256, expected_digest);
  EXPECT_EQ(result->record_count, 1u);
  EXPECT_TRUE(base::DirectoryExists(StagingPath()));

  auto duplicate = target_->StageAuthorized(
      std::move(retry_plan), std::move(retry_authorization), PayloadFile());
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), Error::kAuthorizationConsumed);
  EXPECT_TRUE(target_->AbandonStage().has_value());
  EXPECT_TRUE(target_->AbandonStage().has_value());
  EXPECT_FALSE(base::PathExists(StagingPath()));
  ExpectTargetConfigurationEmpty();
}

TEST_F(DormantBackupRestoreTargetStagingTest,
       ExpiredDecisionDoesNotConsumeAndExpiredPlanningIdentityIsAccepted) {
  auto expired_plan = Plan();
  auto expired = target_->StageAuthorized(
      expired_plan.Clone(), Authorization(*expired_plan, 1u), PayloadFile());
  ASSERT_FALSE(expired.has_value());
  EXPECT_EQ(expired.error(), Error::kInvalidAuthorization);
  EXPECT_FALSE(base::PathExists(StagingPath()));

  auto valid = target_->StageAuthorized(
      expired_plan.Clone(), Authorization(*expired_plan), PayloadFile());
  ASSERT_TRUE(valid.has_value());
  EXPECT_TRUE(target_->AbandonStage().has_value());
}

TEST_F(DormantBackupRestoreTargetStagingTest,
       SplicedBindingGenerationTargetAndSourceAreRefusedBeforeConsumption) {
  auto plan = Plan();

  auto spliced = Authorization(*plan);
  spliced->binding->backup_id = "another-backup";
  EXPECT_EQ(
      target_->StageAuthorized(plan.Clone(), std::move(spliced), PayloadFile())
          .error(),
      Error::kInvalidAuthorization);

  auto generation = Authorization(*plan);
  generation->decision_operation->service_generation = 8u;
  EXPECT_EQ(
      target_
          ->StageAuthorized(plan.Clone(), std::move(generation), PayloadFile())
          .error(),
      Error::kInvalidAuthorization);

  auto digest = Authorization(*plan);
  digest->binding->snapshot_sha256[0] ^= 1u;
  EXPECT_EQ(
      target_->StageAuthorized(plan.Clone(), std::move(digest), PayloadFile())
          .error(),
      Error::kInvalidAuthorization);

  auto same_source = plan.Clone();
  same_source->binding->owner_profile_id = target_profile_id_;
  auto same_source_authorization = Authorization(*same_source);
  EXPECT_EQ(
      target_
          ->StageAuthorized(std::move(same_source),
                            std::move(same_source_authorization), PayloadFile())
          .error(),
      Error::kInvalidAuthorization);

  auto another_target = plan.Clone();
  another_target->target->profile_id = kSourceProfileId;
  another_target->binding->target->profile_id = kSourceProfileId;
  auto another_target_authorization = Authorization(*another_target);
  EXPECT_EQ(target_
                ->StageAuthorized(std::move(another_target),
                                  std::move(another_target_authorization),
                                  PayloadFile())
                .error(),
            Error::kInvalidAuthorization);

  auto valid = target_->StageAuthorized(plan.Clone(), Authorization(*plan),
                                        PayloadFile());
  ASSERT_TRUE(valid.has_value());
  EXPECT_TRUE(target_->AbandonStage().has_value());
}

TEST_F(DormantBackupRestoreTargetStagingTest,
       PayloadFailureConsumesAuthorityAndRetainsCleanupCustody) {
  auto plan = Plan();
  auto retry_plan = plan.Clone();
  auto retry_authorization = Authorization(*plan);
  auto failed = target_->StageAuthorized(
      std::move(plan), Authorization(*retry_plan), TruncatedPayloadFile());
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), Error::kPayloadMismatch);
  EXPECT_TRUE(base::DirectoryExists(StagingPath()));

  auto retry = target_->StageAuthorized(
      std::move(retry_plan), std::move(retry_authorization), PayloadFile());
  ASSERT_FALSE(retry.has_value());
  EXPECT_EQ(retry.error(), Error::kAuthorizationConsumed);
  EXPECT_TRUE(target_->AbandonStage().has_value());
  EXPECT_FALSE(base::PathExists(StagingPath()));
  ExpectTargetConfigurationEmpty();
}

TEST_F(DormantBackupRestoreTargetStagingTest,
       PreexistingStagingLinkIsNeitherAdoptedNorDeleted) {
  const auto foreign = root_.GetPath().AppendASCII("foreign-stage");
  ASSERT_TRUE(base::CreateDirectory(foreign));
  const auto sentinel = foreign.AppendASCII("sentinel");
  ASSERT_TRUE(base::WriteFile(sentinel, "keep"));
  ASSERT_TRUE(base::CreateSymbolicLink(foreign, StagingPath()));
  auto plan = Plan();

  auto result = target_->StageAuthorized(plan.Clone(), Authorization(*plan),
                                         PayloadFile());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kStagingPathOccupied);
  EXPECT_TRUE(target_->AbandonStage().has_value());
  EXPECT_TRUE(base::IsLink(StagingPath()));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(sentinel, &contents));
  EXPECT_EQ(contents, "keep");
}

TEST_F(DormantBackupRestoreTargetStagingTest,
       PreexistingStagingDirectoryIsNeitherAdoptedNorDeleted) {
  ASSERT_TRUE(base::CreateDirectory(StagingPath()));
  const auto sentinel = StagingPath().AppendASCII("unowned");
  ASSERT_TRUE(base::WriteFile(sentinel, "keep"));
  auto plan = Plan();

  auto result = target_->StageAuthorized(plan.Clone(), Authorization(*plan),
                                         PayloadFile());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kStagingPathOccupied);
  EXPECT_TRUE(target_->AbandonStage().has_value());
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(sentinel, &contents));
  EXPECT_EQ(contents, "keep");
}

TEST_F(DormantBackupRestoreTargetStagingTest,
       ReplacedTargetDatabaseCannotAuthorizeAStage) {
  const auto database_path = target_->database_path();
  const auto retained =
      database_path.DirName().AppendASCII("retained-core.sqlite3");
  ASSERT_TRUE(base::Move(database_path, retained));
  ASSERT_TRUE(base::WriteFile(database_path, "unowned replacement"));
  auto plan = Plan();

  auto result = target_->StageAuthorized(plan.Clone(), Authorization(*plan),
                                         PayloadFile());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kTargetChanged);
  EXPECT_FALSE(base::PathExists(StagingPath()));
  EXPECT_TRUE(target_->AbandonStage().has_value());
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(database_path, &contents));
  EXPECT_EQ(contents, "unowned replacement");
  EXPECT_TRUE(base::PathExists(retained));
}

TEST_F(DormantBackupRestoreTargetStagingTest,
       ReplacedOwnedStageIsRetainedForExplicitReconciliation) {
  auto plan = Plan();
  ASSERT_TRUE(
      target_
          ->StageAuthorized(plan.Clone(), Authorization(*plan), PayloadFile())
          .has_value());
  const auto retained =
      target_->database_path().DirName().AppendASCII("retained-stage");
  ASSERT_TRUE(base::Move(StagingPath(), retained));
  ASSERT_TRUE(base::CreateDirectory(StagingPath()));
  const auto sentinel = StagingPath().AppendASCII("unowned");
  ASSERT_TRUE(base::WriteFile(sentinel, "keep"));

  auto cleanup = target_->AbandonStage();
  ASSERT_FALSE(cleanup.has_value());
  EXPECT_EQ(cleanup.error(), Error::kCleanupFailed);
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(sentinel, &contents));
  EXPECT_EQ(contents, "keep");
  EXPECT_TRUE(base::PathExists(target_->database_path()));
  EXPECT_TRUE(base::PathExists(retained));
}

}  // namespace
}  // namespace taffy::storage::backup
