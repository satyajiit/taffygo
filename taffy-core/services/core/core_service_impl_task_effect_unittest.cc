// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl_task_effect_test_peer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using core_service_impl_task_effect_test::AppendBatch;
using core_service_impl_task_effect_test::CoreServiceImplTaskEffectTest;
using core_service_impl_task_effect_test::DomainStorageBatch;
using core_service_impl_task_effect_test::kCommittedRevision;
using core_service_impl_task_effect_test::kGeneration;
using core_service_impl_task_effect_test::kTaskRevision;
using core_service_impl_task_effect_test::StateBatch;

TEST_F(CoreServiceImplTaskEffectTest,
       UnrelatedStateCannotRetirePendingTaskEffectCommit) {
  CoreServiceImplTaskEffectTestPeer::DeliverUnrelatedStorageResult(
      impl_, kGeneration, StateBatch(2u, kTaskRevision));
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(host_.registered_sequences().empty());
  EXPECT_TRUE(host_.published_sequences().empty());
  EXPECT_FALSE(host_.has_held_registration());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  ASSERT_TRUE(CoreServiceImplTaskEffectTestPeer::StagePolicyCommit(
      impl_, kGeneration, kTaskRevision, kCommittedRevision));
  CoreServiceImplTaskEffectTestPeer::DeliverPolicyCommitResult(
      impl_, kGeneration, kCommittedRevision,
      StateBatch(3u, kCommittedRevision));
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.registered_sequences());
  EXPECT_TRUE(host_.published_sequences().empty());
  ASSERT_TRUE(host_.has_held_registration());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  host_.ReleaseRegistration();
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<uint64_t>{2u, 3u}), host_.registered_sequences());
  EXPECT_EQ((std::vector<uint64_t>{2u, 3u}), host_.published_sequences());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       AppendCommitRequiresSharedIdentityAndResultingOperationRevision) {
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::TryStagePolicyCommit(
      impl_, kGeneration, kTaskRevision, kCommittedRevision,
      "different-effect-id", kCommittedRevision,
      mojom::StorageOperation::kAppendTaskCommit));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::TryStagePolicyCommit(
      impl_, kGeneration, kTaskRevision, kCommittedRevision,
      "policy-storage-operation", kTaskRevision,
      mojom::StorageOperation::kAppendTaskCommit));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::StagePolicyCommit(
      impl_, kGeneration, kTaskRevision, kCommittedRevision));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       SurfaceTerminalPublishesASeparateReadinessSequence) {
  CoreServiceImplTaskEffectTestPeer::SetActiveTaskEffectKind(
      impl_, mojom::TaskReducerEffectKind::kRequestApproval);
  CoreServiceImplTaskEffectTestPeer::SubmitCompletion(
      impl_, StateBatch(2u, kTaskRevision));
  task_environment_.RunUntilIdle();

  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.registered_sequences());
  EXPECT_TRUE(host_.published_sequences().empty());
  ASSERT_TRUE(host_.has_held_registration());
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasPendingTaskEffectCommit(impl_));

  host_.ReleaseRegistration();
  task_environment_.RunUntilIdle();
  EXPECT_EQ((std::vector<uint64_t>{2u}), host_.published_sequences());
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       DomainMutationMustMatchTheActiveTaskEffectKind) {
  CoreServiceImplTaskEffectTestPeer::SetActiveTaskEffectKind(
      impl_, mojom::TaskReducerEffectKind::kRunLibraryTool);
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::TryStagePolicyCommit(
      impl_, kGeneration, 0u, 0u, "policy-storage-operation", kTaskRevision,
      mojom::StorageOperation::kUpsertMemory));
  CoreServiceImplTaskEffectTestPeer::SetActiveTaskEffectKind(
      impl_, mojom::TaskReducerEffectKind::kRunMemoryTool);
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::TryStagePolicyCommit(
      impl_, kGeneration, 0u, 0u, "policy-storage-operation", kTaskRevision,
      mojom::StorageOperation::kUpsertLibraryEntry));
}

// One-shot answers for an effect the browser did not settle. The bridge
// refusing a completion, or a deadline arriving with no completion at all,
// used to end the core — every task in the profile lost over one effect. The
// utility now speaks for the effect once, and only a second refusal of the
// same effect is fatal.
CoreResponseBatch RefusedBatch() {
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->operation_id = "ask-policy-operation";
  batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
  return batch;
}

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

TEST_F(CoreServiceImplTaskEffectTest,
       ARefusedCompletionIsAnsweredOnceMoreBeforeTheCoreGivesUp) {
  CoreServiceImplTaskEffectTestPeer::SubmitCompletion(impl_, RefusedBatch());
  // The utility asked the core to settle the effect itself and stayed up.
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasSynthesizedCompletion(impl_));

  // A second refusal of the same effect is the end of the road.
  CoreServiceImplTaskEffectTestPeer::SubmitCompletion(impl_, RefusedBatch());
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_FALSE(CoreServiceImplTaskEffectTestPeer::IsHostBound(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       AReplyForAnEffectThatIsNoLongerActiveIsDroppedNotFatal) {
  auto older = mojom::TaskEffectBinding::New();
  older->operation = mojom::OperationEnvelope::New(
      "older-operation", kGeneration, kTaskRevision, 60'000u, "older-key");
  older->effect_id = "older-effect";
  older->task_id = "task-1";
  older->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  auto completion = mojom::TaskEffectCompletion::New();
  completion->operation = older->operation.Clone();
  completion->effect_id = older->effect_id;
  completion->task_id = older->task_id;
  completion->kind = older->kind;
  completion->status = mojom::TaskEffectCompletionStatus::kSucceeded;

  CoreServiceImplTaskEffectTestPeer::DeliverExecuted(impl_, std::move(older),
                                                     std::move(completion));

  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasSynthesizedCompletion(impl_));
}

TEST_F(CoreServiceImplTaskEffectTest,
       AnOverdueActiveEffectIsCompletedByTheUtilityAndOneInTimeIsNot) {
  CoreServiceImplTaskEffectTestPeer::SetActiveEffectDeadline(
      impl_, NowMonotonicMillis() + 3'600'000u);
  CoreServiceImplTaskEffectTestPeer::ExpireDeadline(impl_);
  EXPECT_FALSE(
      CoreServiceImplTaskEffectTestPeer::HasSynthesizedCompletion(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::HasActiveTaskEffect(impl_));

  CoreServiceImplTaskEffectTestPeer::SetActiveEffectDeadline(impl_, 1u);
  CoreServiceImplTaskEffectTestPeer::ExpireDeadline(impl_);
  EXPECT_TRUE(
      CoreServiceImplTaskEffectTestPeer::HasSynthesizedCompletion(impl_));
  EXPECT_TRUE(CoreServiceImplTaskEffectTestPeer::IsReady(impl_));
}

}  // namespace
}  // namespace taffy
