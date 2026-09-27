// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_TASK_EFFECT_TEST_SUPPORT_H_
#define TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_TASK_EFFECT_TEST_SUPPORT_H_

#include <stdint.h>

#include <utility>
#include <vector>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/contracts/core-service/core_service.mojom.h"

namespace taffy {

// A deliberately narrow CoreHost fake for causal task-effect publication
// tests. It holds the first state registration so tests can observe the exact
// boundary between registration and publication.
class RecordingTaskEffectCoreHost final : public core_service::mojom::CoreHost {
 public:
  RecordingTaskEffectCoreHost();
  ~RecordingTaskEffectCoreHost() override;

  mojo::PendingRemote<core_service::mojom::CoreHost> BindNewPipe();

  const std::vector<uint64_t>& published_sequences() const;
  const std::vector<uint64_t>& registered_sequences() const;
  const std::vector<std::pair<uint64_t, uint64_t>>& emitted_appends() const;
  const std::vector<core_service::mojom::StorageOperation>&
  emitted_storage_operations() const;
  bool has_held_registration() const;
  void ReleaseRegistration();
  void HoldTaskEffects();
  const std::vector<core_service::mojom::TaskEffectBindingPtr>&
  executed_effects() const;
  void ReleaseTaskEffect(
      size_t index,
      core_service::mojom::TaskEffectCompletionPtr completion);

 private:
  void RegisterCapability(core_service::mojom::MintedCapabilityGrantPtr grant,
                          RegisterCapabilityCallback callback) override;
  void RegisterPendingApprovals(
      core_service::mojom::CoreStateBrowserBindingsPtr bindings,
      RegisterPendingApprovalsCallback callback) override;
  void EvaluateTaskPolicy(core_service::mojom::TaskPolicyEffectPtr effect,
                          EvaluateTaskPolicyCallback callback) override;
  void ExecuteTaskEffect(core_service::mojom::TaskEffectBindingPtr effect,
                         ExecuteTaskEffectCallback callback) override;
  void EmitEffect(core_service::mojom::EffectEnvelopePtr effect) override;
  void PublishTaskAnswerEvents(
      std::vector<core_service::mojom::TaskAnswerEventPtr> events,
      PublishTaskAnswerEventsCallback callback) override;
  void PublishState(core_service::mojom::CoreStateUpdatePtr update) override;

  std::vector<uint64_t> published_sequences_;
  std::vector<uint64_t> registered_sequences_;
  std::vector<core_service::mojom::StorageOperation>
      emitted_storage_operations_;
  std::vector<std::pair<uint64_t, uint64_t>> emitted_appends_;
  bool hold_task_effects_ = false;
  std::vector<core_service::mojom::TaskEffectBindingPtr> executed_effects_;
  std::vector<ExecuteTaskEffectCallback> task_effect_callbacks_;
  bool hold_next_registration_ = true;
  RegisterPendingApprovalsCallback registration_callback_;
  mojo::Receiver<core_service::mojom::CoreHost> receiver_{this};
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_TASK_EFFECT_TEST_SUPPORT_H_
