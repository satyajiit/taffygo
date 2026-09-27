// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_MANAGER_TASK_EFFECT_TEST_PEER_H_
#define TAFFY_BROWSER_CORE_SERVICE_MANAGER_TASK_EFFECT_TEST_PEER_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_deferred_task_surface_owner.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

class CoreServiceManagerTaskEffectTestPeer final {
 public:
  static mojo::PendingReceiver<service_mojom::TaffyCoreService> BindService(
      CoreServiceManager& manager) {
    return manager.service_.remote().BindNewPipeAndPassReceiver();
  }

  static mojo::PendingReceiver<service_mojom::CoreSession> MakeReady(
      CoreServiceManager& manager) {
    manager.availability_ = CoreServiceManager::Availability::kReady;
    manager.disconnect_handled_ = false;
    return manager.session_.BindNewPipeAndPassReceiver();
  }

  static void SetBrowserProfileId(CoreServiceManager& manager,
                                  std::string browser_profile_id) {
    manager.browser_profile_id_ = std::move(browser_profile_id);
  }

  // Builds the same three generation-bound browser registries production
  // starts after reading the profile identity from storage. Profile browser
  // tests that replace only the CoreSession use this to keep authority real:
  // neither a lease, a capability nor a value reference is inserted here.
  static void BeginBrowserAuthorityGeneration(CoreServiceManager& manager,
                                              std::string browser_profile_id) {
    manager.browser_profile_id_ = std::move(browser_profile_id);
    manager.actor_leases_.BeginGeneration(manager.browser_profile_id_,
                                          manager.service_generation_);
    manager.capabilities_.BeginGeneration(manager.browser_profile_id_,
                                          manager.service_generation_);
    manager.value_references_.BeginGeneration(manager.browser_profile_id_,
                                              manager.service_generation_);
  }

  static void ActivateEffectBroker(CoreServiceManager& manager) {
    manager.effect_broker_->SetActiveGeneration(manager.service_generation_);
  }

  // Exposes the existing profile broker for test-only handler decoration.
  static CoreEffectBroker* GetEffectBroker(CoreServiceManager& manager) {
    return manager.effect_broker_.get();
  }

  // The authority fixture's confirmed-start fact, distinct from merely
  // putting an item in Chromium's download manager or observing a list.
  static void RecordStartedTaskDownload(CoreServiceManager& manager,
                                        const std::string& task_id,
                                        const std::string& session_id,
                                        const std::string& download_id) {
    manager.task_download_ownership_.Record(TaskId{task_id}, session_id,
                                            TabId{"tab-1"}, download_id);
  }

  static void AllowAllTaskSources(CoreServiceManager& manager) {
    manager.live_task_source_validator_ = base::BindRepeating(
        [](const service_mojom::TaskConsentSource&) {
          return IssuedSourceLiveness::kLive;
        });
  }

  static AuthoritySubmissionStage StageSubmittedCommand(
      CoreServiceManager& manager,
      const service_mojom::CoreServiceCommand& command,
      uint64_t now_monotonic_ms,
      uint64_t now_utc_ms) {
    return manager.accepted_approvals_.StageSubmittedCommand(
        command, manager.browser_profile_id_, manager.browser_session_id_,
        manager.state_bindings_, now_monotonic_ms, now_utc_ms);
  }

  // Completes the browser ledger's ordinary two-part durable proof for a
  // command already submitted through CoreServiceManager. This stands in for
  // the test CoreSession's transaction bytes; it does not create approval or
  // consent authority, which still requires the later exact state binding.
  static bool CompleteStagedStorageCommit(
      CoreServiceManager& manager,
      const service_mojom::CoreServiceCommand& command,
      const std::string& task_id,
      uint64_t resulting_revision,
      uint64_t now_monotonic_ms) {
    auto effect = service_mojom::EffectEnvelope::New();
    effect->operation = command.operation.Clone();
    effect->operation->task_revision = resulting_revision;
    effect->effect_id = command.operation->operation_id;
    effect->kind = service_mojom::EffectKind::kStorageCommit;
    effect->retry_class = service_mojom::RetryClass::kIdempotent;
    effect->storage_commit = service_mojom::StorageCommitEffect::New();
    effect->storage_commit->operation_kind =
        service_mojom::StorageOperation::kAppendTaskCommit;
    effect->storage_commit->task_id = task_id;
    effect->storage_commit->expected_revision =
        command.operation->task_revision;
    effect->storage_commit->resulting_revision = resulting_revision;
    if (manager.accepted_approvals_.BindStorageCommit(
            *effect, now_monotonic_ms) != AuthorityStorageBinding::kBound) {
      return false;
    }
    auto result = service_mojom::EffectResult::New();
    result->operation = effect->operation.Clone();
    result->effect_id = effect->effect_id;
    result->kind = service_mojom::EffectKind::kStorageCommit;
    result->status = service_mojom::EffectStatus::kCompleted;
    result->storage =
        service_mojom::StorageEffectResult::New(resulting_revision);
    return manager.accepted_approvals_.RecordStorageCompletion(*result) ==
           AuthorityStorageCompletion::kCommitted;
  }

  static void EmitEffect(CoreServiceManager& manager,
                         service_mojom::EffectEnvelopePtr effect) {
    manager.EmitEffect(std::move(effect));
  }

  static void CompleteEffect(CoreServiceManager& manager,
                             uint64_t generation,
                             service_mojom::EffectResultPtr result) {
    manager.OnEffectCompleted(generation, std::move(result));
  }

  static service_mojom::PendingApprovalRegistrationStatus RegisterBindings(
      CoreServiceManager& manager,
      service_mojom::CoreStateBrowserBindingsPtr bindings) {
    service_mojom::PendingApprovalRegistrationStatus status =
        service_mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    manager.RegisterPendingApprovals(
        std::move(bindings),
        base::BindLambdaForTesting(
            [&](service_mojom::PendingApprovalRegistrationStatus result) {
              status = result;
            }));
    return status;
  }

  static service_mojom::CapabilityRegistrationStatus RegisterCapability(
      CoreServiceManager& manager,
      service_mojom::MintedCapabilityGrantPtr grant) {
    service_mojom::CapabilityRegistrationStatus status =
        service_mojom::CapabilityRegistrationStatus::kInvalidGrant;
    manager.RegisterCapability(
        std::move(grant),
        base::BindLambdaForTesting(
            [&](service_mojom::CapabilityRegistrationStatus result) {
              status = result;
            }));
    return status;
  }

  static void Execute(CoreServiceManager& manager,
                      service_mojom::TaskEffectBindingPtr binding,
                      CoreServiceManager::ExecuteTaskEffectCallback callback) {
    manager.ExecuteTaskEffect(std::move(binding), std::move(callback));
  }

  // The completion half of a dispatched action, so the branch that refuses a
  // move which landed somewhere the ledger holds no source for is reachable
  // without a live navigation (decision 0165). The branch reads `crosses_origin`
  // off the binding it is given — the executable's destination origin against
  // the frozen document's — so a binding is the whole of what it needs.
  static service_mojom::TaskEffectCompletionPtr CompleteAction(
      CoreServiceManager& manager,
      service_mojom::TaskEffectBindingPtr binding,
      std::optional<ActionResult> result) {
    service_mojom::TaskEffectCompletionPtr completion;
    manager.OnTaskActionCompleted(
        std::move(binding),
        base::BindLambdaForTesting(
            [&](service_mojom::TaskEffectCompletionPtr value) {
              completion = std::move(value);
            }),
        std::move(result));
    return completion;
  }

  static service_mojom::TaskEffectCompletionPtr CompleteObservation(
      CoreServiceManager& manager,
      service_mojom::TaskEffectBindingPtr binding,
      service_mojom::EffectResultPtr result) {
    service_mojom::TaskEffectCompletionPtr completion;
    manager.OnTaskObservationCompleted(
        std::move(binding),
        base::BindLambdaForTesting(
            [&](service_mojom::TaskEffectCompletionPtr value) {
              completion = std::move(value);
            }),
        std::move(result));
    return completion;
  }

  static void QueueTaskSurface(
      CoreServiceManager& manager,
      service_mojom::TaskEffectBindingPtr binding,
      CoreServiceManager::ExecuteTaskEffectCallback callback) {
    manager.QueueTaskSurface(std::move(binding), std::move(callback));
  }

  static void SubmitSuppliedFieldValues(CoreServiceManager& manager,
                                        const std::string& task_id,
                                        const std::string& request_id,
                                        uint32_t supplied) {
    // One field identity per value, as the coordinator reports them.
    std::vector<std::string> fields;
    for (uint32_t index = 0u; index < supplied; ++index) {
      fields.push_back("field-" + base::NumberToString(index));
    }
    manager.SubmitSuppliedFieldValues(
        task_id, request_id, supplied,
        core_service::mojom::FieldValueAskOutcome::kAnswered, fields);
  }

  static void CloseFieldValueRequest(CoreServiceManager& manager,
                                     const std::string& request_id) {
    manager.OnFieldValueRequestClosed(request_id);
  }

  static size_t PendingAdmissionCount(const CoreServiceManager& manager) {
    return manager.pending_admissions_.size();
  }

  // The one command waiting for admission, or null when there is not exactly
  // one, so a test can read what the browser submitted without sending it.
  static const service_mojom::CoreServiceCommand* OnlyPendingCommand(
      const CoreServiceManager& manager) {
    if (manager.pending_admissions_.size() != 1u) {
      return nullptr;
    }
    return manager.pending_admissions_.begin()->second.command.get();
  }

  static size_t DeferredTaskSurfaceCount(const CoreServiceManager& manager) {
    return manager.deferred_task_surfaces_.size();
  }

  static size_t DeferredDiscoveryBootstrapCount(
      const CoreServiceManager& manager) {
    return manager.deferred_discovery_bootstraps_.size();
  }

  static uint64_t PublishedStateSequence(const CoreServiceManager& manager) {
    return manager.state_cache_.last_sequence();
  }

  static bool PendingAdmissionWasSent(const CoreServiceManager& manager,
                                      const std::string& operation_id) {
    const auto pending = manager.pending_admissions_.find(operation_id);
    return pending != manager.pending_admissions_.end() && pending->second.sent;
  }

  static bool AnyPendingAdmissionWasSent(const CoreServiceManager& manager) {
    for (const auto& [operation_id, pending] : manager.pending_admissions_) {
      static_cast<void>(operation_id);
      if (pending.sent) {
        return true;
      }
    }
    return false;
  }

  static void CompleteAdmission(CoreServiceManager& manager,
                                const std::string& operation_id,
                                service_mojom::AdmissionStatus status) {
    manager.OnAdmission(manager.service_generation_, operation_id,
                        service_mojom::Admission::New(operation_id, status));
  }

  static void Disconnect(CoreServiceManager& manager) {
    manager.HandleDisconnect(/*unexpected=*/false);
  }

  static void EvaluatePolicy(
      CoreServiceManager& manager,
      service_mojom::TaskPolicyEffectPtr effect,
      CoreServiceManager::EvaluateTaskPolicyCallback callback) {
    manager.EvaluateTaskPolicy(std::move(effect), std::move(callback));
  }

  static void Publish(CoreServiceManager& manager,
                      service_mojom::CoreStateUpdatePtr state) {
    manager.PublishState(std::move(state));
  }

  static bool PublishAnswers(
      CoreServiceManager& manager,
      std::vector<service_mojom::TaskAnswerEventPtr> events) {
    bool accepted = false;
    manager.PublishTaskAnswerEvents(
        std::move(events),
        base::BindLambdaForTesting([&](bool result) { accepted = result; }));
    return accepted;
  }

  static size_t CompletedTaskSettlementCount(
      const CoreServiceManager& manager) {
    return manager.completed_task_settlements_.size();
  }

  static size_t AcceptedConsentCount(const CoreServiceManager& manager) {
    return manager.accepted_approvals_.accepted_consent_count_for_testing();
  }

  static size_t SuspendedConsentCount(const CoreServiceManager& manager) {
    return manager.accepted_approvals_.suspended_consent_count_for_testing();
  }

  static size_t ActiveTaskAnswerCount(const CoreServiceManager& manager) {
    return manager.active_task_answer_count_;
  }

  static size_t PendingHostTaskEffectCount(const CoreServiceManager& manager) {
    return manager.pending_host_task_effect_ids_.size();
  }

  static size_t EmittedFieldValueRequestCount(
      const CoreServiceManager& manager) {
    return manager.emitted_field_value_requests_.size();
  }

  static void OpenFieldValueRequestWithoutSurface(
      CoreServiceManager& manager,
      const std::string& request_id) {
    manager.emitted_field_value_requests_.insert(request_id);
    manager.OpenFieldValueRequest(request_id, "task-1", "tab-1", "node-1",
                                  /*companion_node_ids=*/{});
  }
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_MANAGER_TASK_EFFECT_TEST_PEER_H_
