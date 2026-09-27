// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
constexpr char kSkillId[] = "persisted.document";
constexpr uint64_t kNow = 1'000u;

mojom::CoreBootstrapPtr Reload(CoreStorageBroker& storage,
                               uint64_t generation) {
  base::test::TestFuture<mojom::CoreBootstrapPtr> loaded;
  storage.LoadBootstrap(generation, false, loaded.GetCallback());
  auto bootstrap = loaded.Take();
  if (bootstrap) {
    bootstrap->browser_profile_id = "profile-1";
    bootstrap->browser_session_id = "browser-session-1";
    bootstrap->generation_capability_entropy.clear();
    for (uint8_t value = 1u; value <= 32u; ++value) {
      bootstrap->generation_capability_entropy.push_back(value);
    }
  }
  return bootstrap;
}

mojom::CoreServiceCommandPtr Mutation(uint64_t generation,
                                      const std::string& id,
                                      mojom::SkillMutationKind kind,
                                      bool enabled) {
  auto command = mojom::CoreServiceCommand::New();
  command->kind = mojom::CoreServiceCommandKind::kMutateSkill;
  command->operation = mojom::OperationEnvelope::New(
      id, generation, 0u, kNow + 10'000u, id + "-once");
  command->mutate_skill = mojom::MutateSkillCommand::New();
  auto& body = *command->mutate_skill;
  body.kind = kind;
  body.skill_id = kSkillId;
  body.expected_version = 1u;
  body.enabled = enabled;
  body.recorded_at_epoch_ms = 10'000u + generation;
  if (kind == mojom::SkillMutationKind::kTeach) {
    body.expected_version = 0u;
    body.origin = "https://example.test";
    // Schema ordinal zero is Document and NoMutation respectively. These
    // public inputs go through the production recorder and definition codec.
    body.clauses.push_back(mojom::SkillObservedClause::New(
        mojom::SkillClauseKind::kRolePresent, 0u, 0u));
    body.steps.push_back(mojom::SkillObservedStep::New(
        "browser.dom.read", std::vector<mojom::SkillObservedArgumentPtr>{}, 0u,
        false, 0u));
    body.admitted = 1u;
  }
  return command;
}

void PersistMutation(RustCore& core,
                     CoreStorageBroker& storage,
                     mojom::CoreServiceCommandPtr command) {
  auto proposed = core.Submit(std::move(command), kNow);
  ASSERT_TRUE(proposed.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, proposed.admission->status);
  ASSERT_EQ(1u, proposed.effects.size());
  ASSERT_TRUE(proposed.effects.front()->storage_commit);
  base::test::TestFuture<mojom::EffectResultPtr> committed;
  storage.DispatchStorage(std::move(proposed.effects.front()),
                          committed.GetCallback());
  auto terminal = committed.Take();
  ASSERT_TRUE(terminal);
  ASSERT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  auto delivered = core.DeliverEffectResult(std::move(terminal), kNow + 1u);
  ASSERT_TRUE(delivered.admission);
  ASSERT_EQ(mojom::AdmissionStatus::kAccepted, delivered.admission->status);
  ASSERT_EQ(1u, delivered.states.size());
}

void ExpectReview(RustCore& core,
                  uint64_t generation,
                  mojom::SkillStatus status) {
  auto result = core.QuerySavedFlows(
      mojom::SavedFlowQueryCommand::New(
          mojom::OperationEnvelope::New("review", generation, 0u,
                                        kNow + 10'000u, "review-once"),
          mojom::SavedFlowQueryKind::kReview, "", kSkillId, 1u),
      kNow);
  ASSERT_TRUE(result);
  ASSERT_EQ(mojom::SavedFlowQueryStatus::kAvailable, result->status);
  ASSERT_EQ(1u, result->flows.size());
  EXPECT_EQ(kSkillId, result->flows.front()->skill_id);
  EXPECT_EQ(1u, result->flows.front()->active_version);
  EXPECT_EQ(status, result->flows.front()->status);
  ASSERT_EQ(1u, result->flows.front()->reviewed_steps.size());
  EXPECT_EQ("browser.dom.read", result->flows.front()->reviewed_steps[0]->verb);
}

TEST(RustSkillLifecycleRestoreTest,
     AcceptedAndDisabledRowsSurviveNewStorageAndCoreInstances) {
  base::test::TaskEnvironment tasks;
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("core.sqlite3");
  std::vector<uint8_t> immutable_definition;
  {
    CoreStorageBroker storage(path, false);
    RustCore core;
    auto initialized = core.Initialize(Reload(storage, 1u));
    ASSERT_TRUE(initialized.result);
    ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
    ASSERT_NO_FATAL_FAILURE(PersistMutation(
        core, storage,
        Mutation(1u, "teach", mojom::SkillMutationKind::kTeach, false)));
    auto draft = Reload(storage, 1u);
    ASSERT_TRUE(draft);
    ASSERT_EQ(1u, draft->skills.size());
    ASSERT_EQ(mojom::SkillStatus::kDraft, draft->skills.front()->status);
    immutable_definition = draft->skills.front()->definition;
    ASSERT_NO_FATAL_FAILURE(PersistMutation(
        core, storage,
        Mutation(1u, "accept", mojom::SkillMutationKind::kSetEnabled, true)));
  }
  tasks.RunUntilIdle();
  uint64_t generation = 2u;
  for (const auto expected :
       {mojom::SkillStatus::kActive, mojom::SkillStatus::kDisabled,
        mojom::SkillStatus::kDraft}) {
    {
      CoreStorageBroker storage(path, false);
      auto bootstrap = Reload(storage, generation);
      ASSERT_TRUE(bootstrap);
      ASSERT_EQ(1u, bootstrap->skills.size());
      EXPECT_EQ(expected, bootstrap->skills.front()->status);
      EXPECT_EQ(immutable_definition, bootstrap->skills.front()->definition);
      RustCore core;
      auto initialized = core.Initialize(std::move(bootstrap));
      ASSERT_TRUE(initialized.result);
      ASSERT_EQ(mojom::InitializationStatus::kReady,
                initialized.result->status);
      EXPECT_EQ(generation, initialized.result->accepted_generation);
      ASSERT_NO_FATAL_FAILURE(ExpectReview(core, generation, expected));
      if (generation < 4u) {
        // Re-enabling a disabled flow returns it to reviewable Draft, never
        // silently back to Active. Both lifecycle changes are real commits.
        ASSERT_NO_FATAL_FAILURE(PersistMutation(
            core, storage,
            Mutation(generation, "status-" + std::to_string(generation),
                     mojom::SkillMutationKind::kSetEnabled, generation == 3u)));
      }
    }
    tasks.RunUntilIdle();
    ++generation;
  }
}

}  // namespace
}  // namespace taffy
