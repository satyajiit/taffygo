// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_library.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/components/storage/browser/core_storage_library_validation.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kEntryId[] = "11111111111111111111111111111111";
constexpr char kCollectionId[] = "22222222222222222222222222222222";
constexpr char kWorkspaceId[] = "33333333333333333333333333333333";
constexpr char kFactId[] = "44444444444444444444444444444444";
constexpr char kSourceId[] = "55555555555555555555555555555555";

mojom::CoreBootstrapPtr Load(CoreStorageBroker* broker) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1u, false, base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
        bootstrap = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

mojom::EffectResultPtr Dispatch(CoreStorageBroker* broker,
                                mojom::EffectEnvelopePtr effect) {
  base::RunLoop loop;
  mojom::EffectResultPtr result;
  broker->DispatchStorage(
      std::move(effect),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr value) {
        result = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return result;
}

mojom::EffectEnvelopePtr BaseEffect(std::string effect_id,
                                    uint64_t expected,
                                    uint64_t resulting) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = effect_id;
  effect->operation->service_generation = 1u;
  effect->operation->deadline_monotonic_ms = 10'000u;
  effect->operation->idempotency_key = "library-key-" + effect_id;
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->task_id_seed.assign(
      storage_internal::kTaskIdSeedBytes, 0u);
  effect->storage_commit->expected_revision = expected;
  effect->storage_commit->resulting_revision = resulting;
  return effect;
}

mojom::EffectEnvelopePtr SaveEffect(std::string effect_id,
                                    uint64_t expected,
                                    uint64_t resulting,
                                    std::string value = "two years") {
  auto effect = BaseEffect(std::move(effect_id), expected, resulting);
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kUpsertLibraryEntry;
  auto source = mojom::LibrarySourceRecord::New(
      kSourceId, "Manufacturer specifications", "maker.example", 1'000u);
  std::vector<mojom::LibrarySourceRecordPtr> sources;
  sources.push_back(std::move(source));
  auto entry = mojom::LibraryEntryRecord::New(
      kEntryId, 1u, kCollectionId, "TV research", kWorkspaceId, 7u, kFactId,
      "warranty", std::move(value), std::nullopt,
      mojom::LibraryFactKind::kFromPage, std::move(sources), 2'000u, 1'000u,
      false);
  effect->storage_commit->library_entry =
      mojom::LibraryPersistEffect::New(std::move(entry), 0u);
  return effect;
}

mojom::EffectEnvelopePtr RemoveEffect(std::string effect_id,
                                      uint64_t expected,
                                      uint64_t resulting) {
  auto effect = BaseEffect(std::move(effect_id), expected, resulting);
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kRemoveLibraryEntry;
  effect->storage_commit->library_deletion =
      mojom::LibraryDeletionEffect::New(kEntryId, 1u, 2u, 3'000u);
  return effect;
}

class CoreStorageLibraryTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageLibraryTest, TypedBodiesAreClosedAndRevisionExact) {
  auto save = SaveEffect("library-save-1", 0u, 1u);
  ASSERT_TRUE(save->storage_commit);
  EXPECT_TRUE(IsValidLibraryStorageCommitBody(*save->storage_commit));

  save->storage_commit->library_entry->entry->revision = 2u;
  EXPECT_FALSE(IsValidLibraryStorageCommitBody(*save->storage_commit));
  save->storage_commit->library_entry->entry->revision = 1u;
  save->storage_commit->library_deletion =
      mojom::LibraryDeletionEffect::New(kEntryId, 1u, 2u, 3'000u);
  EXPECT_FALSE(IsValidLibraryStorageCommitBody(*save->storage_commit));

  auto removal = RemoveEffect("library-remove-1", 1u, 2u);
  ASSERT_TRUE(removal->storage_commit);
  EXPECT_TRUE(IsValidLibraryStorageCommitBody(*removal->storage_commit));
  removal->storage_commit->library_deletion->resulting_entry_revision = 3u;
  EXPECT_FALSE(IsValidLibraryStorageCommitBody(*removal->storage_commit));
}

TEST_F(CoreStorageLibraryTest, CommitsReplaysAndRestoresExactCitedEntry) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);

  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, SaveEffect("library-save-1", 0u, 1u))->status);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, SaveEffect("library-save-1", 0u, 1u))->status);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker, SaveEffect("library-save-1", 0u, 1u, "changed"))
                ->status);
  EXPECT_EQ(
      mojom::EffectStatus::kUnavailable,
      Dispatch(&broker, SaveEffect("library-save-stale", 0u, 1u))->status);

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(1u, bootstrap->library_revision);
  ASSERT_EQ(1u, bootstrap->library_entries.size());
  const mojom::LibraryEntryRecord& entry = *bootstrap->library_entries.front();
  EXPECT_EQ(kEntryId, entry.entry_id);
  EXPECT_EQ("two years", entry.original_value);
  ASSERT_EQ(1u, entry.sources.size());
  EXPECT_EQ("maker.example", entry.sources.front()->host);
}

TEST_F(CoreStorageLibraryTest, RemovalDeletesContentAndKeepsOnlyReplayFacts) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, SaveEffect("library-save-1", 0u, 1u))->status);
  ASSERT_EQ(
      mojom::EffectStatus::kCompleted,
      Dispatch(&broker, RemoveEffect("library-remove-1", 1u, 2u))->status);
  EXPECT_EQ(
      mojom::EffectStatus::kCompleted,
      Dispatch(&broker, RemoveEffect("library-remove-1", 1u, 2u))->status);

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(2u, bootstrap->library_revision);
  EXPECT_TRUE(bootstrap->library_entries.empty());
}

}  // namespace
}  // namespace taffy
