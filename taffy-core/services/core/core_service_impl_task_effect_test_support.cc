// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/core_service_impl_task_effect_test_support.h"

#include <utility>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace mojom = core_service::mojom;

RecordingTaskEffectCoreHost::RecordingTaskEffectCoreHost() = default;

RecordingTaskEffectCoreHost::~RecordingTaskEffectCoreHost() = default;

mojo::PendingRemote<mojom::CoreHost>
RecordingTaskEffectCoreHost::BindNewPipe() {
  return receiver_.BindNewPipeAndPassRemote();
}

const std::vector<uint64_t>& RecordingTaskEffectCoreHost::published_sequences()
    const {
  return published_sequences_;
}

const std::vector<uint64_t>& RecordingTaskEffectCoreHost::registered_sequences()
    const {
  return registered_sequences_;
}

const std::vector<std::pair<uint64_t, uint64_t>>&
RecordingTaskEffectCoreHost::emitted_appends() const {
  return emitted_appends_;
}

const std::vector<mojom::StorageOperation>&
RecordingTaskEffectCoreHost::emitted_storage_operations() const {
  return emitted_storage_operations_;
}

bool RecordingTaskEffectCoreHost::has_held_registration() const {
  return !registration_callback_.is_null();
}

void RecordingTaskEffectCoreHost::ReleaseRegistration() {
  ASSERT_FALSE(registration_callback_.is_null());
  std::move(registration_callback_)
      .Run(mojom::PendingApprovalRegistrationStatus::kRegistered);
}

void RecordingTaskEffectCoreHost::HoldTaskEffects() {
  hold_task_effects_ = true;
}

const std::vector<mojom::TaskEffectBindingPtr>&
RecordingTaskEffectCoreHost::executed_effects() const {
  return executed_effects_;
}

void RecordingTaskEffectCoreHost::ReleaseTaskEffect(
    size_t index,
    mojom::TaskEffectCompletionPtr completion) {
  ASSERT_LT(index, task_effect_callbacks_.size());
  ASSERT_TRUE(task_effect_callbacks_[index]);
  std::move(task_effect_callbacks_[index]).Run(std::move(completion));
}

void RecordingTaskEffectCoreHost::RegisterCapability(
    mojom::MintedCapabilityGrantPtr grant,
    RegisterCapabilityCallback callback) {
  std::move(callback).Run(mojom::CapabilityRegistrationStatus::kRegistered);
}

void RecordingTaskEffectCoreHost::RegisterPendingApprovals(
    mojom::CoreStateBrowserBindingsPtr bindings,
    RegisterPendingApprovalsCallback callback) {
  ASSERT_TRUE(bindings);
  registered_sequences_.push_back(bindings->state_sequence);
  if (hold_next_registration_) {
    hold_next_registration_ = false;
    registration_callback_ = std::move(callback);
    return;
  }
  std::move(callback).Run(
      mojom::PendingApprovalRegistrationStatus::kRegistered);
}

void RecordingTaskEffectCoreHost::EvaluateTaskPolicy(
    mojom::TaskPolicyEffectPtr effect,
    EvaluateTaskPolicyCallback callback) {
  ADD_FAILURE() << "the staged task effect must not be redispatched";
  std::move(callback).Run(mojom::PolicyEvaluationResult::New());
}

void RecordingTaskEffectCoreHost::ExecuteTaskEffect(
    mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  if (hold_task_effects_) {
    executed_effects_.push_back(std::move(effect));
    task_effect_callbacks_.push_back(std::move(callback));
    return;
  }
  ADD_FAILURE() << "the staged task effect must not be redispatched";
  std::move(callback).Run(mojom::TaskEffectCompletion::New());
}

void RecordingTaskEffectCoreHost::EmitEffect(mojom::EffectEnvelopePtr effect) {
  ASSERT_TRUE(effect);
  ASSERT_TRUE(effect->storage_commit);
  emitted_storage_operations_.push_back(effect->storage_commit->operation_kind);
  if (effect->storage_commit->operation_kind !=
      mojom::StorageOperation::kAppendTaskCommit) {
    return;
  }
  emitted_appends_.emplace_back(effect->storage_commit->expected_revision,
                                effect->storage_commit->resulting_revision);
}

void RecordingTaskEffectCoreHost::PublishTaskAnswerEvents(
    std::vector<mojom::TaskAnswerEventPtr> events,
    PublishTaskAnswerEventsCallback callback) {
  ADD_FAILURE() << "the state-only fixtures must not publish answer events";
  std::move(callback).Run(true);
}

void RecordingTaskEffectCoreHost::PublishState(
    mojom::CoreStateUpdatePtr update) {
  ASSERT_TRUE(update);
  published_sequences_.push_back(update->sequence);
}

}  // namespace taffy
