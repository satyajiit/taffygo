// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/time/time.h"
#include "crypto/secure_util.h"
#include "openssl/sha.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_account.h"
#include "taffy/services/core/rust_core_assets.h"
#include "taffy/services/core/rust_core_bridge_handle.h"
#include "taffy/services/core/rust_core_command_conversions.h"
#include "taffy/services/core/rust_core_composer.h"
#include "taffy/services/core/rust_core_entitlement.h"
#include "taffy/services/core/rust_core_policy.h"
#include "taffy/services/core/rust_core_probe.h"
#include "taffy/services/core/rust_core_provider.h"
#include "taffy/services/core/rust_core_response.h"
#include "taffy/services/core/rust_core_state.h"
#include "taffy/services/core/rust_core_task.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "taffy/services/core/rust_core_workspace.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_bootstrap_ffi.rs.h"

namespace taffy {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

namespace {

const char* BridgeInitializationRefusalReason(
    bridge::BridgeInitializationStatus status) {
  switch (status) {
    case bridge::BridgeInitializationStatus::RuntimeConfigurationRefused:
      return "runtime-configuration";
    case bridge::BridgeInitializationStatus::AccountRestoreRefused:
      return "account-restore";
    case bridge::BridgeInitializationStatus::TaskRestoreRefused:
      return "task-restore";
    case bridge::BridgeInitializationStatus::WorkspaceRestoreRefused:
      return "workspace-restore";
    case bridge::BridgeInitializationStatus::LibraryRestoreRefused:
      return "library-restore";
    case bridge::BridgeInitializationStatus::MemoryRestoreRefused:
      return "memory-restore";
    case bridge::BridgeInitializationStatus::AssetRestoreRefused:
      return "asset-restore";
    case bridge::BridgeInitializationStatus::StateProjectionRefused:
      return "state-projection";
    case bridge::BridgeInitializationStatus::ReviewedWorkflowRestoreRefused:
      return "reviewed-workflow-restore";
    case bridge::BridgeInitializationStatus::SkillRunRestoreRefused:
      return "skill-run-restore";
    case bridge::BridgeInitializationStatus::Ready:
      return "ready";
    default:
      return "unknown";
  }
}

void LogInitializationRefusal(const char* reason) {
  LOG(ERROR) << "[taffy_core_initialization_refused] reason=" << reason;
}

}  // namespace

namespace core_bridge {

rust::Vec<uint8_t> ChromiumSha256(rust::Slice<const uint8_t> input) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> digest{};
  SHA256(input.data(), input.size(), digest.data());
  rust::Vec<uint8_t> output;
  output.reserve(digest.size());
  for (uint8_t byte : digest) {
    output.push_back(byte);
  }
  return output;
}

void TaskTrace(rust::Str line) {
  LOG(WARNING) << "[taffy_trace] " << std::string(line);
}

}  // namespace core_bridge

CoreStatePublication::CoreStatePublication() = default;
CoreStatePublication::CoreStatePublication(CoreStatePublication&&) = default;
CoreStatePublication& CoreStatePublication::operator=(CoreStatePublication&&) =
    default;
CoreStatePublication::~CoreStatePublication() = default;

CoreInitializationBatch::CoreInitializationBatch() = default;
CoreInitializationBatch::CoreInitializationBatch(CoreInitializationBatch&&) =
    default;
CoreInitializationBatch& CoreInitializationBatch::operator=(
    CoreInitializationBatch&&) = default;
CoreInitializationBatch::~CoreInitializationBatch() = default;

CoreResponseBatch::CoreResponseBatch() = default;
CoreResponseBatch::CoreResponseBatch(CoreResponseBatch&&) = default;
CoreResponseBatch& CoreResponseBatch::operator=(CoreResponseBatch&&) = default;
CoreResponseBatch::~CoreResponseBatch() = default;

CoreModelStreamDelivery::CoreModelStreamDelivery() = default;
CoreModelStreamDelivery::CoreModelStreamDelivery(CoreModelStreamDelivery&&) =
    default;
CoreModelStreamDelivery& CoreModelStreamDelivery::operator=(
    CoreModelStreamDelivery&&) = default;
CoreModelStreamDelivery::~CoreModelStreamDelivery() = default;


CoreEntitlementDeliveryBatch::CoreEntitlementDeliveryBatch() = default;
CoreEntitlementDeliveryBatch::CoreEntitlementDeliveryBatch(
    CoreEntitlementDeliveryBatch&&) = default;
CoreEntitlementDeliveryBatch& CoreEntitlementDeliveryBatch::operator=(
    CoreEntitlementDeliveryBatch&&) = default;
CoreEntitlementDeliveryBatch::~CoreEntitlementDeliveryBatch() = default;

RustCore::RustCore() = default;
RustCore::~RustCore() = default;

RustCore::Bridge::Bridge(core_bridge::BridgeBootstrap bootstrap)
    : runtime_(core_bridge::CreateServiceBridge(std::move(bootstrap))) {}

RustCore::Bridge::~Bridge() = default;

CoreInitializationBatch RustCore::Initialize(
    mojom::CoreBootstrapPtr bootstrap) {
  CoreInitializationBatch batch;
  batch.result = mojom::CoreBootstrapResult::New();
  batch.result->status = mojom::InitializationStatus::kInvalidBootstrap;
  if (!bootstrap) {
    LogInitializationRefusal("missing-bootstrap");
    return batch;
  }
  if (bridge_) {
    LogInitializationRefusal("bridge-already-initialized");
    return batch;
  }
  std::optional<bridge::BridgeBootstrap> projected =
      core_service_internal::ToBridgeBootstrap(*bootstrap);
  if (!projected) {
    LogInitializationRefusal("bootstrap-projection");
    return batch;
  }
  bridge_ = std::make_unique<Bridge>(std::move(*projected));
  const bridge::BridgeInitialization initialized =
      bridge::Initialization(*bridge_->runtime());
  batch.result->accepted_generation = initialized.accepted_generation;
  auto refuse_bridge_initialization =
      [&batch, this](const char* reason) -> CoreInitializationBatch {
    LOG(ERROR) << "[taffy_core_initialization_refused] reason=" << reason
               << " label="
               << std::string(bridge::LastRefusal(*bridge_->runtime()));
    batch.result->accepted_generation = 0u;
    batch.states.clear();
    bridge_.reset();
    return std::move(batch);
  };
  if (initialized.status != bridge::BridgeInitializationStatus::Ready) {
    return refuse_bridge_initialization(
        BridgeInitializationRefusalReason(initialized.status));
  }
  if (initialized.accepted_generation != bootstrap->service_generation) {
    return refuse_bridge_initialization("bridge-generation-mismatch");
  }
  if (initialized.states.size() != 1u) {
    return refuse_bridge_initialization("bridge-state-count");
  }
  std::optional<CoreStatePublication> state =
      core_service_internal::ToStatePublication(initialized.states.front());
  if (!state) {
    return refuse_bridge_initialization("state-native-projection");
  }
  if (state->state->service_generation != bootstrap->service_generation) {
    return refuse_bridge_initialization("state-generation-mismatch");
  }
  if (state->state->sequence != 1u) {
    return refuse_bridge_initialization("state-sequence");
  }
  for (const bridge::BridgeStorageEffect& effect :
       initialized.storage_effects) {
    mojom::EffectEnvelopePtr projected_effect =
        core_service_internal::ToMojoStorageEffect(effect);
    if (!projected_effect) {
      return refuse_bridge_initialization("storage-effect-projection");
    }
    state->effects.push_back(std::move(projected_effect));
  }
  // The delivery plane's start-up plan travels with the first state, so a
  // profile that opens with an artifact missing begins fetching it without
  // anything having to ask.
  for (const bridge::BridgeAssetEffect& effect : initialized.asset_effects) {
    mojom::EffectEnvelopePtr projected_effect =
        core_service_internal::ToMojoAssetEffect(effect);
    if (!projected_effect) {
      return refuse_bridge_initialization("asset-effect-projection");
    }
    state->effects.push_back(std::move(projected_effect));
  }
  batch.result->status = mojom::InitializationStatus::kReady;
  batch.states.push_back(std::move(*state));
  return batch;
}

mojom::AccountTokenValidationResultPtr RustCore::ValidateAccountTokenResponse(
    mojom::AccountTokenValidationRequestPtr request,
    uint64_t expected_generation,
    uint64_t now_monotonic_ms) {
  std::optional<bridge::BridgeAccountTokenValidationRequest> projected =
      core_service_internal::ToBridgeAccountTokenValidationRequest(
          request.get());
  if (!projected) {
    return nullptr;
  }
  return core_service_internal::ToMojoAccountTokenValidationResult(
      bridge::ValidateAccountTokenResponse(
          std::move(*projected), expected_generation, now_monotonic_ms));
}

CoreResponseBatch RustCore::CompleteTaskSettlement(
    mojom::TaskSettlementBindingPtr settlement,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !settlement) {
    return CoreResponseBatch();
  }
  return core_service_internal::ToResponseBatch(bridge::CompleteTaskSettlement(
      *bridge_->runtime(),
      core_service_internal::ToBridgeTaskSettlement(*settlement),
      now_monotonic_ms, NowUtcMillis()));
}

CoreResponseBatch RustCore::Cancel(mojom::OperationEnvelopePtr operation,
                                   uint64_t) {
  if (!bridge_ || !operation) {
    return CoreResponseBatch();
  }
  return core_service_internal::ToResponseBatch(bridge::CancelOperation(
      *bridge_->runtime(),
      core_service_internal::ToBridgeOperation(*operation)));
}

CoreResponseBatch RustCore::ExpireDueOperations(uint64_t now_monotonic_ms) {
  if (!bridge_) {
    return CoreResponseBatch();
  }
  return core_service_internal::ToResponseBatch(
      bridge::ExpireDueOperations(*bridge_->runtime(), now_monotonic_ms));
}

uint64_t RustCore::NextOperationDeadline() {
  if (!bridge_) {
    return 0u;
  }
  return bridge::NextOperationDeadline(*bridge_->runtime());
}

mojom::PolicyEvaluationResultPtr RustCore::EvaluatePolicy(
    mojom::PolicyEvaluationRequestPtr request) {
  if (!bridge_ || !request) {
    return nullptr;
  }
  std::optional<bridge::BridgePolicyRequest> projected =
      core_service_internal::ToBridgePolicyRequest(*request);
  if (!projected) {
    return nullptr;
  }
  mojom::PolicyEvaluationResultPtr result =
      core_service_internal::ToMojoPolicyResult(
          bridge::EvaluatePolicy(*bridge_->runtime(), std::move(*projected)));
  // INVALID_REQUEST is the one policy answer the task engine turns into
  // `Deny(Unsupported)` against the action, and the ordered core reaches it
  // from fourteen decode clauses. The bridge records which; this is where the
  // name is spent, beside the two commands that already do it.
  if (result &&
      result->status == mojom::PolicyEvaluationStatus::kInvalidRequest) {
    LOG(ERROR) << "[taffy_core_policy_invalid] label="
               << std::string(bridge::LastRefusal(*bridge_->runtime()));
  }
  return result;
}

CoreResponseBatch RustCore::CompleteTaskPolicy(
    mojom::TaskEffectBindingPtr effect,
    mojom::PolicyEvaluationResultPtr result,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !effect || !result) {
    return CoreResponseBatch();
  }
  // The browser's answer, as the utility read it. A policy ask is the one
  // effect whose terminal never reaches `[taffy_task_effect_completed]`, so
  // this hop was the whole of the walk with nothing written down: a proposal
  // could be settled `Deny(Unsupported)` with no line naming who said so.
  LOG(WARNING) << "[taffy_task_policy_answer] status="
               << static_cast<int>(result->status) << " denial="
               << (result->denial ? static_cast<int>(result->denial->code) : -1)
               << " grant=" << (result->minted_grant ? 1 : 0);
  std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskPolicyTerminal(*effect, *result);
  if (!terminal) {
    // Dropping the terminal leaves the action open with nothing recorded and
    // the effect waiting for its deadline. It was silent.
    LOG(ERROR) << "[taffy_task_policy_terminal_dropped] status="
               << static_cast<int>(result->status);
    return CoreResponseBatch();
  }
  return core_service_internal::ToResponseBatch(bridge::CompleteTaskEffect(
      *bridge_->runtime(), std::move(*terminal), now_monotonic_ms));
}

CoreResponseBatch RustCore::CompleteTaskEffect(
    mojom::TaskEffectBindingPtr effect,
    mojom::TaskEffectCompletionPtr completion,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !effect || !completion) {
    return CoreResponseBatch();
  }
  std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*effect, *completion);
  if (!terminal) {
    return CoreResponseBatch();
  }
  return Answer(
      core_service_internal::ToResponseBatch(bridge::CompleteTaskEffect(
          *bridge_->runtime(), std::move(*terminal), now_monotonic_ms)),
      "task-effect");
}

CoreResponseBatch RustCore::Answer(CoreResponseBatch batch, const char* site) {
  if (!bridge_ || !batch.admission) {
    return batch;
  }
  // Two statuses mean "the ordered core would not do this", and both reach a
  // person as one sentence, because the wire carries a status and nothing
  // else. The bridge records which branch answered; this is where that name
  // is spent. A refused command with no label is a branch that forgot to
  // record one, and reads as `label=` rather than as a different fault.
  if (batch.admission->status == mojom::AdmissionStatus::kCoreUnavailable) {
    LOG(ERROR) << "[taffy_core_unavailable] at=" << site << " label="
               << std::string(bridge::LastRefusal(*bridge_->runtime()));
  } else if (batch.admission->status ==
             mojom::AdmissionStatus::kInvalidCommand) {
    LOG(ERROR) << "[taffy_core_invalid_command] at=" << site << " label="
               << std::string(bridge::LastRefusal(*bridge_->runtime()));
  }
  return batch;
}

CoreModelStreamDelivery RustCore::DeliverModelStreamChunk(
    mojom::ModelStreamChunkPtr chunk) {
  if (!bridge_ || !chunk) {
    return CoreModelStreamDelivery();
  }
  std::optional<bridge::BridgeModelStreamChunk> projected =
      core_service_internal::ToBridgeModelStreamChunk(*chunk);
  if (!projected) {
    CoreModelStreamDelivery invalid;
    invalid.status = mojom::ModelStreamChunkStatus::kInvalid;
    return invalid;
  }
  return core_service_internal::ToModelStreamDelivery(
      bridge::DeliverModelStreamChunk(*bridge_->runtime(),
                                      std::move(*projected)));
}

mojom::EffectEnvelopePtr RustCore::PlanEntitlementRefresh(
    mojom::EntitlementFetchReason reason) {
  if (!bridge_) {
    return nullptr;
  }
  const bridge::BridgeEntitlementPlan plan = bridge::PlanEntitlementRefresh(
      *bridge_->runtime(), static_cast<uint8_t>(reason), NowUtcMillis());
  if (!plan.has_effect) {
    return nullptr;
  }
  return core_service_internal::ToMojoEntitlementFetchEffect(plan.effect);
}

CoreEntitlementDeliveryBatch RustCore::DeliverEntitlementFetchResult(
    mojom::EffectResultPtr result) {
  CoreEntitlementDeliveryBatch delivered;
  if (!bridge_ || !result || !result->operation) {
    return delivered;
  }
  std::optional<bridge::BridgeEntitlementFetchResult> projected =
      core_service_internal::ToBridgeEntitlementFetchResult(*result);
  if (!projected) {
    return delivered;
  }
  bridge::BridgeEntitlementDelivery answer =
      bridge::DeliverEntitlementFetchResult(
          *bridge_->runtime(), std::move(*projected), NowUtcMillis());
  delivered.installed = answer.installed;
  delivered.batch =
      core_service_internal::ToResponseBatch(std::move(answer.response));
  return delivered;
}

CoreResponseBatch RustCore::PrepareForShutdown() {
  if (bridge_) {
    bridge::PrepareForShutdown(*bridge_->runtime());
    bridge_.reset();
  }
  return CoreResponseBatch();
}

}  // namespace taffy
