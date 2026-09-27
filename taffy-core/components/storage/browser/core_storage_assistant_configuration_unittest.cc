// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
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

mojom::CoreBootstrapPtr Load(CoreStorageBroker* broker) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1u, false,
      base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
        bootstrap = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

mojom::EffectEnvelopePtr ConfigurationEffect(
    std::string effect_id,
    uint64_t expected_revision,
    std::vector<mojom::AssistantAbility> disabled,
    mojom::PersonalityPreset preset =
        mojom::PersonalityPreset::kCarefulResearcher) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = effect_id;
  effect->operation->service_generation = 1u;
  effect->operation->task_revision = 0u;
  effect->operation->deadline_monotonic_ms = 10'000u;
  effect->operation->idempotency_key = "configuration-key-" + effect_id;
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->task_id_seed.assign(
      storage_internal::kTaskIdSeedBytes, 0u);
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kSetAssistantConfiguration;
  effect->storage_commit->expected_revision = expected_revision;
  effect->storage_commit->resulting_revision = expected_revision + 1u;
  effect->storage_commit->assistant_configuration =
      mojom::AssistantConfigurationPersistEffect::New();
  effect->storage_commit->assistant_configuration->disabled_abilities =
      std::move(disabled);
  effect->storage_commit->assistant_configuration->preset = preset;
  effect->storage_commit->assistant_configuration->pace = 0u;
  effect->storage_commit->assistant_configuration->length = 2u;
  effect->storage_commit->assistant_configuration->check_in = 1u;
  return effect;
}

mojom::EffectStatus Dispatch(CoreStorageBroker* broker,
                             mojom::EffectEnvelopePtr effect) {
  base::RunLoop loop;
  mojom::EffectStatus status = mojom::EffectStatus::kInvalidResult;
  broker->DispatchStorage(
      std::move(effect),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        status = result->status;
        loop.Quit();
      }));
  loop.Run();
  return status;
}

class CoreStorageAssistantConfigurationTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageAssistantConfigurationTest,
       PersistsRestartsAndReplaysExactly) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    mojom::CoreBootstrapPtr first = Load(&broker);
    ASSERT_TRUE(first);
    EXPECT_FALSE(first->assistant_configuration);
    EXPECT_EQ(mojom::EffectStatus::kCompleted,
              Dispatch(&broker,
                       ConfigurationEffect(
                           "configuration-1", 0u,
                           {mojom::AssistantAbility::kDownloads,
                            mojom::AssistantAbility::kVideo},
                           mojom::PersonalityPreset::kTripPlanner)));
  }
  task_environment_.RunUntilIdle();

  CoreStorageBroker restarted(path, false);
  mojom::CoreBootstrapPtr restored = Load(&restarted);
  ASSERT_TRUE(restored);
  ASSERT_TRUE(restored->assistant_configuration);
  EXPECT_EQ(1u, restored->assistant_configuration->revision);
  EXPECT_EQ((std::vector<mojom::AssistantAbility>{
                mojom::AssistantAbility::kDownloads,
                mojom::AssistantAbility::kVideo}),
            restored->assistant_configuration->disabled_abilities);
  EXPECT_EQ(mojom::PersonalityPreset::kTripPlanner,
            restored->assistant_configuration->preset);
  EXPECT_EQ(0u, restored->assistant_configuration->pace);
  EXPECT_EQ(2u, restored->assistant_configuration->length);
  EXPECT_EQ(1u, restored->assistant_configuration->check_in);
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&restarted,
                     ConfigurationEffect(
                         "configuration-1", 0u,
                         {mojom::AssistantAbility::kDownloads,
                          mojom::AssistantAbility::kVideo},
                         mojom::PersonalityPreset::kTripPlanner)));
}

TEST_F(CoreStorageAssistantConfigurationTest,
       RejectsStaleUnsortedAndReusedEffects) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker,
                     ConfigurationEffect(
                         "configuration-1", 0u,
                         {mojom::AssistantAbility::kDownloads})));
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker,
                     ConfigurationEffect(
                         "configuration-stale", 0u,
                         {mojom::AssistantAbility::kVideo})));
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker,
                     ConfigurationEffect(
                         "configuration-unsorted", 1u,
                         {mojom::AssistantAbility::kVideo,
                          mojom::AssistantAbility::kDownloads})));
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker,
                     ConfigurationEffect(
                         "configuration-1", 0u,
                         {mojom::AssistantAbility::kVideo})));
}

TEST_F(CoreStorageAssistantConfigurationTest, MigratesVersionEleven) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  ASSERT_TRUE(storage_test::CreateHistoricalSchema(&database, 11u));
  database.Close();
  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(Load(&broker));
  }
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(database.Open(path));
  sql::Statement table(database.GetUniqueStatement(
      "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
      "name='core_assistant_configuration'"));
  ASSERT_TRUE(table.Step());
  EXPECT_EQ(1, table.ColumnInt(0));
}

// The defect this covers aborted the browser process on a phone, and neither
// test above could see it. Both of them perform exactly one successful write
// per `CoreStorageBroker`, and a broker owns its `sql::Database` — so the
// insert and the update each got a fresh statement cache and never collided.
// The cache is per database, so the collision needs both branches driven
// through one broker, which is what a person does when they change an
// assistant setting twice without restarting the browser.
TEST_F(CoreStorageAssistantConfigurationTest, SecondWriteUpdatesTheSameDatabase) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  // No row yet, so this takes the insert branch and revision 0 becomes 1.
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker,
                     ConfigurationEffect("configuration-1", 0u,
                                         {mojom::AssistantAbility::kDownloads})));
  // The update branch, against the same database and therefore the same
  // statement cache. Before the write was split into two call sites this
  // aborted the process in any build with DCHECKs on.
  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, ConfigurationEffect(
                                  "configuration-2", 1u,
                                  {mojom::AssistantAbility::kVideo},
                                  mojom::PersonalityPreset::kQuickShopper)));
  mojom::CoreBootstrapPtr loaded = Load(&broker);
  ASSERT_TRUE(loaded);
  ASSERT_TRUE(loaded->assistant_configuration);
  EXPECT_EQ(2u, loaded->assistant_configuration->revision);
  EXPECT_EQ((std::vector<mojom::AssistantAbility>{
                mojom::AssistantAbility::kVideo}),
            loaded->assistant_configuration->disabled_abilities);
  EXPECT_EQ(mojom::PersonalityPreset::kQuickShopper,
            loaded->assistant_configuration->preset);
}

}  // namespace
}  // namespace taffy
