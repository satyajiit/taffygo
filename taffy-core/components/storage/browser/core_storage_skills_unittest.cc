// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/components/storage/browser/core_storage_schema_test_util.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kOrigin[] = "https://example.test";
constexpr char kOtherOrigin[] = "https://other.test";
constexpr char kWorkspaceId[] = "11111111111111111111111111111111";

void AppendU32(std::vector<uint8_t>* bytes, uint32_t value) {
  for (size_t index = 0u; index < 4u; ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendU64(std::vector<uint8_t>* bytes, uint64_t value) {
  for (size_t index = 0u; index < 8u; ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendString(std::vector<uint8_t>* bytes, std::string_view value) {
  AppendU32(bytes, static_cast<uint32_t>(value.size()));
  bytes->insert(bytes->end(), value.begin(), value.end());
}

std::vector<uint8_t> Snapshot(uint64_t revision) {
  std::vector<uint8_t> bytes{'T', 'A', 'F', 'F', 'Y', 'W', 'S', '1'};
  AppendU32(&bytes, 1u);
  AppendString(&bytes, kWorkspaceId);
  AppendU64(&bytes, revision);
  AppendString(&bytes, "source deletion");
  bytes.push_back(0u);
  AppendU64(&bytes, 1u);
  bytes.push_back(0u);
  AppendU32(&bytes, 0u);
  AppendU32(&bytes, 0u);
  return bytes;
}

mojom::CoreBootstrapPtr Load(CoreStorageBroker* broker) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1, false, base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
        bootstrap = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

mojom::EffectEnvelopePtr StorageEffect(std::string effect_id,
                                       mojom::StorageOperation operation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = effect_id;
  effect->operation->service_generation = 1;
  effect->operation->task_revision = 0;
  effect->operation->deadline_monotonic_ms = 10'000;
  effect->operation->idempotency_key = "skill-key-" + effect_id;
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->task_id_seed.assign(
      storage_internal::kTaskIdSeedBytes, 0u);
  effect->storage_commit->operation_kind = operation;
  return effect;
}

mojom::EffectEnvelopePtr InstallEffect(std::string effect_id,
                                       std::string skill_id,
                                       std::string origin,
                                       uint32_t version,
                                       std::vector<uint8_t> definition) {
  mojom::EffectEnvelopePtr effect = StorageEffect(
      std::move(effect_id), mojom::StorageOperation::kInstallSkill);
  effect->storage_commit->install_skill = mojom::SkillInstallEffect::New();
  effect->storage_commit->install_skill->skill_id = std::move(skill_id);
  effect->storage_commit->install_skill->origin = std::move(origin);
  effect->storage_commit->install_skill->provenance =
      mojom::SkillProvenance::kRecordedFromTask;
  effect->storage_commit->install_skill->version = version;
  effect->storage_commit->install_skill->definition = std::move(definition);
  effect->storage_commit->install_skill->step_count = 3;
  effect->storage_commit->install_skill->recorded_at_utc_ms = 1'000 + version;
  return effect;
}

mojom::EffectEnvelopePtr StatusEffect(std::string effect_id,
                                      std::string skill_id,
                                      uint32_t version,
                                      mojom::SkillStatus status) {
  mojom::EffectEnvelopePtr effect = StorageEffect(
      std::move(effect_id), mojom::StorageOperation::kSetSkillStatus);
  effect->storage_commit->skill_status = mojom::SkillStatusEffect::New();
  effect->storage_commit->skill_status->skill_id = std::move(skill_id);
  effect->storage_commit->skill_status->status = status;
  effect->storage_commit->skill_status->changed_at_utc_ms = 2'000;
  effect->storage_commit->skill_status->version = version;
  return effect;
}

mojom::EffectEnvelopePtr RunEffect(std::string effect_id,
                                   std::string skill_id,
                                   uint32_t version,
                                   uint64_t ran_at_utc_ms) {
  mojom::EffectEnvelopePtr effect = StorageEffect(
      std::move(effect_id), mojom::StorageOperation::kRecordSkillRun);
  effect->storage_commit->skill_run = mojom::SkillRunEffect::New();
  effect->storage_commit->skill_run->skill_id = std::move(skill_id);
  effect->storage_commit->skill_run->version = version;
  effect->storage_commit->skill_run->task_id = "task-1";
  effect->storage_commit->skill_run->outcome =
      mojom::SkillRunOutcome::kCompleted;
  effect->storage_commit->skill_run->ran_at_utc_ms = ran_at_utc_ms;
  return effect;
}

mojom::EffectEnvelopePtr SourceDeletionEffect(std::string effect_id,
                                              std::string origin,
                                              uint64_t expected_revision) {
  mojom::EffectEnvelopePtr effect = StorageEffect(
      std::move(effect_id), mojom::StorageOperation::kDeleteSource);
  effect->storage_commit->workspace = mojom::WorkspacePersistEffect::New();
  effect->storage_commit->workspace->workspace_id = kWorkspaceId;
  effect->storage_commit->workspace->expected_revision = expected_revision;
  effect->storage_commit->workspace->resulting_revision =
      expected_revision + 1u;
  effect->storage_commit->workspace->snapshot =
      Snapshot(expected_revision + 1u);
  effect->storage_commit->source_deletion = mojom::SourceDeletionEffect::New();
  effect->storage_commit->source_deletion->source_id = "source-1";
  effect->storage_commit->source_deletion->origin = std::move(origin);
  return effect;
}

mojom::EffectResultPtr Dispatch(CoreStorageBroker* broker,
                                mojom::EffectEnvelopePtr effect) {
  base::RunLoop loop;
  mojom::EffectResultPtr result;
  broker->DispatchStorage(
      std::move(effect),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr terminal) {
        result = std::move(terminal);
        loop.Quit();
      }));
  loop.Run();
  return result;
}

mojom::EffectStatus Status(CoreStorageBroker* broker,
                           mojom::EffectEnvelopePtr effect) {
  mojom::EffectResultPtr result = Dispatch(broker, std::move(effect));
  return result ? result->status : mojom::EffectStatus::kInvalidResult;
}

class CoreStorageSkillsTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageSkillsTest, MigratesVersionTenToSkillSchema) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  ASSERT_TRUE(storage_test::CreateHistoricalSchema(&database, 10u));
  database.Close();

  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(Load(&broker));
  }
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(database.Open(path));
  sql::Statement tables(database.GetUniqueStatement(
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "name LIKE 'core_skill%' ORDER BY name"));
  std::vector<std::string> names;
  while (tables.Step()) {
    names.push_back(tables.ColumnString(0));
  }
  ASSERT_TRUE(tables.Succeeded());
  EXPECT_EQ((std::vector<std::string>{"core_skill_installation",
                                      "core_skill_run", "core_skill_version"}),
            names);
}

TEST_F(CoreStorageSkillsTest, InstallsSupersedesAndRestores) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  const std::vector<uint8_t> first{1, 2, 3};
  const std::vector<uint8_t> second{4, 5, 6, 7};

  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, InstallEffect("install-1", "book.table", kOrigin, 1,
                                          first)));
  // A re-delivered effect writes nothing and still succeeds.
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, InstallEffect("install-1", "book.table", kOrigin, 1,
                                          first)));
  // A version that already exists under a different effect is refused rather
  // than overwritten: a version is what an audit record names.
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Status(&broker, InstallEffect("install-2", "book.table", kOrigin, 1,
                                          second)));
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, InstallEffect("install-3", "book.table", kOrigin, 2,
                                          second)));
  // A skill does not move site.
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Status(&broker, InstallEffect("install-4", "book.table",
                                          kOtherOrigin, 3, second)));

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_EQ(1u, bootstrap->skills.size());
  const mojom::SkillRecord& record = *bootstrap->skills.front();
  EXPECT_EQ("book.table", record.skill_id);
  EXPECT_EQ(kOrigin, record.origin);
  EXPECT_EQ(mojom::SkillProvenance::kRecordedFromTask, record.provenance);
  EXPECT_EQ(mojom::SkillStatus::kDraft, record.status);
  EXPECT_EQ(2u, record.active_version);
  EXPECT_EQ(second, record.definition);
  EXPECT_EQ(3u, record.step_count);
}

TEST_F(CoreStorageSkillsTest, StatusMovesOnlyTheVersionItNames) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker,
                   InstallEffect("install-1", "book.table", kOrigin, 1, {1})));

  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, StatusEffect("status-1", "book.table", 1,
                                         mojom::SkillStatus::kActive)));
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, StatusEffect("status-1", "book.table", 1,
                                         mojom::SkillStatus::kActive)));
  // Version two does not exist, so a move naming it lands on nothing.
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Status(&broker, StatusEffect("status-2", "book.table", 2,
                                         mojom::SkillStatus::kActive)));
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Status(&broker, StatusEffect("status-3", "absent.skill", 1,
                                         mojom::SkillStatus::kActive)));

  mojom::CoreBootstrapPtr active = Load(&broker);
  ASSERT_TRUE(active);
  ASSERT_EQ(1u, active->skills.size());
  EXPECT_EQ(mojom::SkillStatus::kActive, active->skills.front()->status);

  // The new version is a draft, whatever the old one had reached.
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker,
                   InstallEffect("install-2", "book.table", kOrigin, 2, {2})));
  mojom::CoreBootstrapPtr after = Load(&broker);
  ASSERT_TRUE(after);
  ASSERT_EQ(1u, after->skills.size());
  EXPECT_EQ(2u, after->skills.front()->active_version);
  EXPECT_EQ(mojom::SkillStatus::kDraft, after->skills.front()->status);
}

TEST_F(CoreStorageSkillsTest, SupersedingDoesNotLiftAPersonsDisable) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Status(&broker, InstallEffect("install-1", "book.table", kOrigin,
                                            1, {1})));
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Status(&broker, StatusEffect("status-1", "book.table", 1,
                                           mojom::SkillStatus::kDisabled)));
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Status(&broker, InstallEffect("install-2", "book.table", kOrigin,
                                            2, {2})));
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  sql::Statement versions(database.GetUniqueStatement(
      "SELECT version,status FROM core_skill_version WHERE skill_id=? "
      "ORDER BY version"));
  versions.BindString(0, "book.table");
  ASSERT_TRUE(versions.Step());
  EXPECT_EQ(1, versions.ColumnInt64(0));
  // Superseding says which steps are current. Only a person may lift a
  // disable, so version one stays where the person left it.
  EXPECT_EQ(static_cast<int>(mojom::SkillStatus::kDisabled),
            versions.ColumnInt(1));
  ASSERT_TRUE(versions.Step());
  EXPECT_EQ(2, versions.ColumnInt64(0));
  EXPECT_EQ(static_cast<int>(mojom::SkillStatus::kDraft),
            versions.ColumnInt(1));
}

TEST_F(CoreStorageSkillsTest, RecallIsNewestFirstAndRunsAreNotDoubled) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker,
                   InstallEffect("install-1", "book.table", kOrigin, 1, {1})));

  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, RunEffect("run-1", "book.table", 1, 100)));
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, RunEffect("run-2", "book.table", 1, 300)));
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, RunEffect("run-3", "book.table", 1, 200)));
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, RunEffect("run-2", "book.table", 1, 300)));
  // A run of a version that was never installed is not a run.
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Status(&broker, RunEffect("run-4", "book.table", 2, 400)));

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_EQ(3u, bootstrap->recall.size());
  EXPECT_EQ(300u, bootstrap->recall[0]->ran_at_utc_ms);
  EXPECT_EQ(200u, bootstrap->recall[1]->ran_at_utc_ms);
  EXPECT_EQ(100u, bootstrap->recall[2]->ran_at_utc_ms);
}

TEST_F(CoreStorageSkillsTest, ForgettingRemovesEveryVersionAndRun) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Status(&broker, InstallEffect("install-1", "book.table", kOrigin,
                                            1, {1})));
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Status(&broker, RunEffect("run-1", "book.table", 1, 100)));

    mojom::EffectEnvelopePtr forget =
        StorageEffect("forget-1", mojom::StorageOperation::kForgetSkill);
    forget->storage_commit->forget_skill = mojom::SkillForgetEffect::New();
    forget->storage_commit->forget_skill->skill_id = "book.table";
    EXPECT_EQ(mojom::EffectStatus::kCompleted,
              Status(&broker, std::move(forget)));

    mojom::CoreBootstrapPtr bootstrap = Load(&broker);
    ASSERT_TRUE(bootstrap);
    EXPECT_TRUE(bootstrap->skills.empty());
    EXPECT_TRUE(bootstrap->recall.empty());
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  sql::Statement rows(database.GetUniqueStatement(
      "SELECT (SELECT COUNT(*) FROM core_skill_version)+"
      "(SELECT COUNT(*) FROM core_skill_run)"));
  ASSERT_TRUE(rows.Step());
  EXPECT_EQ(0, rows.ColumnInt64(0));
}

TEST_F(CoreStorageSkillsTest, DeletingASourceTakesItsSiteSkillsWithIt) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker,
                   InstallEffect("install-1", "book.table", kOrigin, 1, {1})));
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, RunEffect("run-1", "book.table", 1, 100)));
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, InstallEffect("install-2", "other.skill",
                                          kOtherOrigin, 1, {2})));

  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker, SourceDeletionEffect("delete-1", kOrigin, 0)));

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_EQ(1u, bootstrap->skills.size());
  EXPECT_EQ("other.skill", bootstrap->skills.front()->skill_id);
  EXPECT_TRUE(bootstrap->recall.empty());
  ASSERT_EQ(1u, bootstrap->workspaces.size());
  EXPECT_EQ(1u, bootstrap->workspaces.front()->revision);
}

TEST_F(CoreStorageSkillsTest, SourceDeletionKeepsItsHalvesTogether) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Status(&broker,
                   InstallEffect("install-1", "book.table", kOrigin, 1, {1})));

  // The snapshot half cannot land: revision one does not follow revision four.
  // The skill removal must not land either, or the two halves were never one
  // transaction and a crash could leave a skill about a forgotten site.
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Status(&broker, SourceDeletionEffect("delete-1", kOrigin, 4)));

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(1u, bootstrap->skills.size());
  EXPECT_TRUE(bootstrap->workspaces.empty());
}

TEST_F(CoreStorageSkillsTest, ThereIsNoQuerySurface) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  EXPECT_EQ(
      mojom::EffectStatus::kUnavailable,
      Status(&broker, StorageEffect("query-1",
                                    mojom::StorageOperation::kQueryWorkspace)));
}

}  // namespace
}  // namespace taffy
