// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_TASK_EFFECT_TEST_PEER_H_
#define TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_TASK_EFFECT_TEST_PEER_H_

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"
#include "taffy/services/core/core_service_impl_task_effect_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

// The seam the task-effect suites drive: one ready utility with a staged
// ASK_POLICY effect, the peer that reaches its private state, and the batch
// shapes a core answers with. Shared by the completion and continuation
// suites so each stays under the line cap and neither redefines the other's
// fixture.

namespace taffy {

namespace mojom = core_service::mojom;

class CoreServiceImplTaskEffectTestPeer final {
 public:
  static void MakeReady(CoreServiceImpl& impl,
                        uint64_t generation,
                        mojo::PendingRemote<mojom::CoreHost> host) {
    impl.generation_ = generation;
    impl.host_.Bind(std::move(host));
    impl.ready_ = true;
  }

  static void StagePolicyEffect(CoreServiceImpl& impl,
                                uint64_t generation,
                                uint64_t task_revision) {
    auto active = mojom::TaskEffectBinding::New();
    active->operation = mojom::OperationEnvelope::New();
    active->operation->operation_id = "ask-policy-operation";
    active->operation->service_generation = generation;
    active->operation->task_revision = task_revision;
    // A browser-minted deadline is absolute on the utility's monotonic clock
    // and a minute ahead of it; a fixed value would be in the past on any
    // device up longer than a minute, and the utility now speaks for an
    // effect whose deadline has arrived (decision 0136 section 2).
    active->operation->deadline_monotonic_ms = impl.NowMonotonicMillis() + 60'000u;
    active->operation->idempotency_key = "ask-policy-key";
    active->effect_id = "ask-policy-effect";
    active->task_id = "task-1";
    active->kind = mojom::TaskReducerEffectKind::kAskPolicy;
    active->policy = mojom::TaskPolicyEffect::New();
    impl.active_task_effect_ = std::move(active);
  }

  static void SetActiveTaskEffectKind(CoreServiceImpl& impl,
                                      mojom::TaskReducerEffectKind kind) {
    ASSERT_TRUE(impl.active_task_effect_);
    impl.active_task_effect_->kind = kind;
    impl.active_task_effect_->policy.reset();
  }

  static bool StagePolicyCommit(CoreServiceImpl& impl,
                                uint64_t generation,
                                uint64_t expected_revision,
                                uint64_t resulting_revision) {
    return TryStagePolicyCommit(impl, generation, expected_revision,
                                resulting_revision, "policy-storage-operation",
                                resulting_revision,
                                mojom::StorageOperation::kAppendTaskCommit);
  }

  static bool TryStagePolicyCommit(CoreServiceImpl& impl,
                                   uint64_t generation,
                                   uint64_t expected_revision,
                                   uint64_t resulting_revision,
                                   std::string effect_id,
                                   uint64_t operation_task_revision,
                                   mojom::StorageOperation operation_kind) {
    auto effect = mojom::EffectEnvelope::New();
    effect->operation = mojom::OperationEnvelope::New();
    effect->operation->operation_id = "policy-storage-operation";
    effect->operation->service_generation = generation;
    effect->operation->task_revision = operation_task_revision;
    effect->operation->deadline_monotonic_ms = 60'000u;
    effect->operation->idempotency_key = "policy-storage-key";
    effect->effect_id = std::move(effect_id);
    effect->kind = mojom::EffectKind::kStorageCommit;
    effect->retry_class = mojom::RetryClass::kConsequential;
    effect->storage_commit = mojom::StorageCommitEffect::New();
    effect->storage_commit->operation_kind = operation_kind;
    effect->storage_commit->task_id = "task-1";
    effect->storage_commit->expected_revision = expected_revision;
    effect->storage_commit->resulting_revision = resulting_revision;
    return impl.SetPendingTaskEffectCommit(*effect);
  }

  static void DeliverUnrelatedStorageResult(CoreServiceImpl& impl,
                                            uint64_t generation,
                                            CoreResponseBatch batch) {
    CoreServiceImpl::EffectIdentity identity;
    identity.effect_id = "unrelated-storage-effect";
    identity.operation_id = "unrelated-storage-operation";
    identity.service_generation = generation;
    identity.task_revision = 0u;
    identity.deadline_monotonic_ms = 59'000u;
    identity.idempotency_key = "unrelated-storage-key";
    impl.OnEffectResultDelivered(std::move(identity), true, std::move(batch));
  }

  static void DeliverPolicyCommitResult(CoreServiceImpl& impl,
                                        uint64_t generation,
                                        uint64_t committed_revision,
                                        CoreResponseBatch batch) {
    impl.OnEffectResultDelivered(CommitIdentity(generation, committed_revision),
                                 true, std::move(batch));
  }

  static void SubmitCompletion(CoreServiceImpl& impl, CoreResponseBatch batch) {
    impl.OnTaskEffectCompletionSubmitted(std::move(batch));
  }

  static void DeliverCommitResult(CoreServiceImpl& impl,
                                  uint64_t generation,
                                  std::string operation_id,
                                  std::string idempotency_key,
                                  uint64_t committed_revision,
                                  CoreResponseBatch batch) {
    CoreServiceImpl::EffectIdentity identity =
        CommitIdentity(generation, committed_revision);
    identity.effect_id = operation_id;
    identity.operation_id = std::move(operation_id);
    identity.idempotency_key = std::move(idempotency_key);
    impl.OnEffectResultDelivered(std::move(identity), true, std::move(batch));
  }

  static void DeliverExecuted(CoreServiceImpl& impl,
                              mojom::TaskEffectBindingPtr effect,
                              mojom::TaskEffectCompletionPtr completion) {
    impl.OnTaskEffectExecuted(std::move(effect), std::move(completion));
  }
  static void ExpireDeadline(CoreServiceImpl& impl) {
    impl.OnDeadlineExpired();
  }
  static void SetActiveEffectDeadline(CoreServiceImpl& impl,
                                      uint64_t deadline_monotonic_ms) {
    ASSERT_TRUE(impl.active_task_effect_);
    impl.active_task_effect_->operation->deadline_monotonic_ms =
        deadline_monotonic_ms;
  }
  static bool HasSynthesizedCompletion(const CoreServiceImpl& impl) {
    return impl.task_effect_completion_synthesized_;
  }
  static bool IsReady(const CoreServiceImpl& impl) { return impl.ready_; }

  static bool IsHostBound(const CoreServiceImpl& impl) {
    return impl.host_.is_bound();
  }

  static bool HasActiveTaskEffect(const CoreServiceImpl& impl) {
    return !!impl.active_task_effect_;
  }

  static bool HasPendingTaskEffectCommit(const CoreServiceImpl& impl) {
    return impl.task_effect_completion_commit_.has_value();
  }

 private:
  static CoreServiceImpl::EffectIdentity CommitIdentity(
      uint64_t generation,
      uint64_t committed_revision) {
    CoreServiceImpl::EffectIdentity identity;
    identity.effect_id = "policy-storage-operation";
    identity.operation_id = "policy-storage-operation";
    identity.service_generation = generation;
    identity.task_revision = committed_revision;
    identity.deadline_monotonic_ms = 60'000u;
    identity.idempotency_key = "policy-storage-key";
    return identity;
  }
};

namespace core_service_impl_task_effect_test {

inline constexpr uint64_t kGeneration = 7u;
inline constexpr uint64_t kTaskRevision = 17u;
inline constexpr uint64_t kCommittedRevision = 18u;

inline CoreResponseBatch DomainStorageBatch(std::string operation_id,
                                            std::string idempotency_key,
                                            mojom::StorageOperation kind) {
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->operation_id = operation_id;
  batch.admission->status = mojom::AdmissionStatus::kAccepted;
  auto effect = mojom::EffectEnvelope::New();
  effect->operation =
      mojom::OperationEnvelope::New(operation_id, kGeneration, kTaskRevision,
                                    60'000u, std::move(idempotency_key));
  effect->effect_id = std::move(operation_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind = kind;
  effect->storage_commit->task_id_seed.assign(32u, 0u);
  batch.effects.push_back(std::move(effect));
  return batch;
}

inline CoreResponseBatch AppendBatch(std::string operation_id,
                                     std::string idempotency_key,
                                     uint64_t expected_revision,
                                     uint64_t resulting_revision) {
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->operation_id = operation_id;
  batch.admission->status = mojom::AdmissionStatus::kAccepted;
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New(operation_id, kGeneration,
                                                    resulting_revision, 60'000u,
                                                    std::move(idempotency_key));
  effect->effect_id = std::move(operation_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->task_id_seed.assign(32u, 0u);
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = "task-1";
  effect->storage_commit->expected_revision = expected_revision;
  effect->storage_commit->resulting_revision = resulting_revision;
  batch.effects.push_back(std::move(effect));
  return batch;
}

inline CoreResponseBatch StateBatch(uint64_t sequence, uint64_t task_revision) {
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->status = mojom::AdmissionStatus::kAccepted;

  CoreStatePublication publication;
  publication.state = mojom::CoreStateUpdate::New();
  publication.state->service_generation = kGeneration;
  publication.state->sequence = sequence;
  publication.state->payload = {0x01u};
  publication.browser_bindings = mojom::CoreStateBrowserBindings::New();
  publication.browser_bindings->service_generation = kGeneration;
  publication.browser_bindings->state_sequence = sequence;
  auto revision = mojom::TaskRevisionBinding::New();
  revision->task_id = "task-1";
  revision->service_generation = kGeneration;
  revision->task_revision = task_revision;
  publication.browser_bindings->task_revisions.push_back(std::move(revision));
  batch.states.push_back(std::move(publication));
  return batch;
}

class CoreServiceImplTaskEffectTest : public testing::Test {
 protected:
  CoreServiceImplTaskEffectTest();
  ~CoreServiceImplTaskEffectTest() override;

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  RecordingTaskEffectCoreHost host_;
  CoreServiceImpl impl_;
};

}  // namespace core_service_impl_task_effect_test
}  // namespace taffy

#endif  // TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_TASK_EFFECT_TEST_PEER_H_
