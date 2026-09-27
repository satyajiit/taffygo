// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_response.h"

#include <optional>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/logging.h"
#include "crypto/secure_util.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_account.h"
#include "taffy/services/core/rust_core_assets.h"
#include "taffy/services/core/rust_core_endpoint_probe.h"
#include "taffy/services/core/rust_core_listing.h"
#include "taffy/services/core/rust_core_probe.h"
#include "taffy/services/core/rust_core_state.h"
#include "taffy/services/core/rust_core_workspace.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;
namespace wire = core_service::wire;

namespace {

constexpr size_t kTaskIdSeedBytes = 32u;

mojom::OperationEnvelopePtr ToMojoOperation(const bridge::BridgeOperation& in) {
  auto out = mojom::OperationEnvelope::New();
  out->operation_id = std::string(in.operation_id);
  out->service_generation = in.service_generation;
  out->task_revision = in.task_revision;
  out->deadline_monotonic_ms = in.deadline_monotonic_ms;
  out->idempotency_key = std::string(in.idempotency_key);
  return out;
}

bridge::BridgeModelStreamOperation ToBridgeModelStreamOperation(
    const mojom::OperationEnvelope& in) {
  bridge::BridgeModelStreamOperation out;
  out.operation_id = in.operation_id;
  out.service_generation = in.service_generation;
  out.task_revision = in.task_revision;
  out.deadline_monotonic_ms = in.deadline_monotonic_ms;
  out.idempotency_key = in.idempotency_key;
  return out;
}

template <typename AnswerEvent>
mojom::TaskAnswerEventPtr ToMojoTaskAnswerEvent(const AnswerEvent& in) {
  const std::string task_id(in.task_id);
  const std::string call_id(in.call_id);
  const std::string text(in.text);
  if (task_id.empty() || task_id.size() > mojom::kMaxIdentifierBytes ||
      call_id.empty() || call_id.size() > mojom::kMaxIdentifierBytes ||
      in.terminal != !in.has_text || (in.complete && !in.terminal) ||
      (in.has_text &&
       (text.empty() || text.size() > mojom::kMaxTaskAnswerDeltaBytes)) ||
      (!in.has_text && !text.empty())) {
    return nullptr;
  }
  auto out = mojom::TaskAnswerEvent::New();
  out->task_id = task_id;
  out->call_id = call_id;
  out->sequence = in.sequence;
  if (in.has_text) {
    out->text = text;
  }
  out->terminal = in.terminal;
  out->complete = in.complete;
  return out;
}

mojom::AdmissionStatus AdmissionStatusFromWire(uint8_t status) {
  switch (status) {
    case 0:
      return mojom::AdmissionStatus::kAccepted;
    case 1:
      return mojom::AdmissionStatus::kStaleGeneration;
    case 2:
      return mojom::AdmissionStatus::kStaleRevision;
    case 3:
      return mojom::AdmissionStatus::kDeadlineExceeded;
    case 4:
      return mojom::AdmissionStatus::kBackpressure;
    case 6:
      return mojom::AdmissionStatus::kCoreUnavailable;
    case 7:
      return mojom::AdmissionStatus::kDuplicate;
    case 5:
    default:
      return mojom::AdmissionStatus::kInvalidCommand;
  }
}

}  // namespace

std::optional<bridge::BridgeModelStreamChunk> ToBridgeModelStreamChunk(
    const mojom::ModelStreamChunk& chunk) {
  if (!chunk.operation || chunk.effect_id.empty() || chunk.data.empty() ||
      chunk.data.size() > mojom::kMaxModelStreamChunkBytes) {
    return std::nullopt;
  }
  bridge::BridgeModelStreamChunk out;
  out.operation = ToBridgeModelStreamOperation(*chunk.operation);
  out.effect_id = chunk.effect_id;
  out.sequence = chunk.sequence;
  out.data.reserve(chunk.data.size());
  for (uint8_t byte : chunk.data) {
    out.data.push_back(byte);
  }
  return out;
}

CoreModelStreamDelivery ToModelStreamDelivery(
    bridge::BridgeModelStreamDelivery delivery) {
  CoreModelStreamDelivery out;
  const std::optional<mojom::ModelStreamChunkStatus> status =
      wire::ModelStreamChunkStatusFromWire(delivery.status);
  if (!status ||
      delivery.answer_events.size() > mojom::kMaxTaskAnswerEventsPerBatch) {
    out.status = mojom::ModelStreamChunkStatus::kInvalid;
    return out;
  }
  out.status = *status;
  for (const bridge::BridgeModelStreamAnswerEvent& event :
       delivery.answer_events) {
    mojom::TaskAnswerEventPtr projected = ToMojoTaskAnswerEvent(event);
    if (!projected) {
      out.status = mojom::ModelStreamChunkStatus::kInvalid;
      out.answer_events.clear();
      return out;
    }
    out.answer_events.push_back(std::move(projected));
  }
  if (out.status != mojom::ModelStreamChunkStatus::kAccepted &&
      !out.answer_events.empty()) {
    out.status = mojom::ModelStreamChunkStatus::kInvalid;
    out.answer_events.clear();
  }
  return out;
}

mojom::EffectEnvelopePtr ToMojoStorageEffect(
    const bridge::BridgeStorageEffect& in) {
  const std::optional<mojom::StorageOperation> operation_kind =
      wire::StorageOperationFromWire(in.operation_kind);
  if (!operation_kind) {
    return nullptr;
  }
  if (*operation_kind != mojom::StorageOperation::kAppendTaskCommit &&
      *operation_kind != mojom::StorageOperation::kInstallSkill &&
      *operation_kind != mojom::StorageOperation::kSetSkillStatus &&
      *operation_kind != mojom::StorageOperation::kRecordSkillRun &&
      *operation_kind != mojom::StorageOperation::kForgetSkill &&
      *operation_kind != mojom::StorageOperation::kSetAssistantConfiguration) {
    return nullptr;
  }
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = ToMojoOperation(in.operation);
  effect->effect_id = std::string(in.effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  // `task_id_seed` is a fixed-size Mojo array even for storage domains that
  // do not use task identity. Generated pointer defaults leave vectors empty,
  // which cannot be serialized as `array<uint8, 32>`. Start from the exact
  // neutral wire value and replace it below for a task-journal append.
  effect->storage_commit->task_id_seed.assign(kTaskIdSeedBytes, 0u);
  effect->storage_commit->operation_kind = *operation_kind;
  if (effect->storage_commit->operation_kind ==
      mojom::StorageOperation::kRecordSkillRun) {
    const std::optional<mojom::SkillRunOutcome> outcome =
        wire::SkillRunOutcomeFromWire(in.skill_run_outcome);
    if (!outcome) {
      return nullptr;
    }
    effect->storage_commit->skill_run = mojom::SkillRunEffect::New();
    effect->storage_commit->skill_run->skill_id = std::string(in.skill_id);
    effect->storage_commit->skill_run->version = in.skill_version;
    effect->storage_commit->skill_run->task_id = std::string(in.skill_task_id);
    effect->storage_commit->skill_run->outcome = *outcome;
    effect->storage_commit->skill_run->ran_at_utc_ms = in.skill_ran_at_utc_ms;
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kInstallSkill) {
    const std::optional<mojom::SkillProvenance> provenance =
        wire::SkillProvenanceFromWire(in.skill_provenance);
    if (!provenance) {
      return nullptr;
    }
    effect->storage_commit->install_skill = mojom::SkillInstallEffect::New();
    effect->storage_commit->install_skill->skill_id = std::string(in.skill_id);
    effect->storage_commit->install_skill->origin =
        std::string(in.skill_origin);
    effect->storage_commit->install_skill->provenance = *provenance;
    effect->storage_commit->install_skill->version = in.skill_version;
    effect->storage_commit->install_skill->definition.assign(
        in.skill_definition.begin(), in.skill_definition.end());
    effect->storage_commit->install_skill->step_count = in.skill_step_count;
    effect->storage_commit->install_skill->recorded_at_utc_ms =
        in.skill_changed_at_utc_ms;
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kSetSkillStatus) {
    const std::optional<mojom::SkillStatus> status =
        wire::SkillStatusFromWire(in.skill_status);
    if (!status) {
      return nullptr;
    }
    effect->storage_commit->skill_status = mojom::SkillStatusEffect::New();
    effect->storage_commit->skill_status->skill_id =
        std::string(in.skill_id);
    effect->storage_commit->skill_status->status = *status;
    effect->storage_commit->skill_status->changed_at_utc_ms =
        in.skill_changed_at_utc_ms;
    effect->storage_commit->skill_status->version = in.skill_version;
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kForgetSkill) {
    effect->storage_commit->forget_skill = mojom::SkillForgetEffect::New();
    effect->storage_commit->forget_skill->skill_id =
        std::string(in.skill_id);
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kSetAssistantConfiguration) {
    effect->storage_commit->expected_revision = in.expected_revision;
    effect->storage_commit->resulting_revision = in.resulting_revision;
    effect->storage_commit->assistant_configuration =
        mojom::AssistantConfigurationPersistEffect::New();
    for (uint8_t ability_wire : in.configuration_disabled_abilities) {
      const std::optional<mojom::AssistantAbility> ability =
          wire::AssistantAbilityFromWire(ability_wire);
      if (!ability) {
        return nullptr;
      }
      effect->storage_commit->assistant_configuration->disabled_abilities
          .push_back(*ability);
    }
    const std::optional<mojom::PersonalityPreset> preset =
        wire::PersonalityPresetFromWire(in.configuration_preset);
    if (!preset) {
      return nullptr;
    }
    effect->storage_commit->assistant_configuration->preset = *preset;
    effect->storage_commit->assistant_configuration->pace =
        in.configuration_pace;
    effect->storage_commit->assistant_configuration->length =
        in.configuration_length;
    effect->storage_commit->assistant_configuration->check_in =
        in.configuration_check_in;
  } else {
    effect->storage_commit->task_id = std::string(in.task_id);
    effect->storage_commit->expected_revision = in.expected_revision;
    effect->storage_commit->resulting_revision = in.resulting_revision;
    effect->storage_commit->transaction_batch.assign(
        in.transaction_batch.begin(), in.transaction_batch.end());
    effect->storage_commit->task_id_seed.assign(in.task_id_seed.begin(),
                                                in.task_id_seed.end());
    if (!in.workspace_id.empty()) {
      effect->storage_commit->workspace = mojom::WorkspacePersistEffect::New();
      effect->storage_commit->workspace->workspace_id =
          std::string(in.workspace_id);
      effect->storage_commit->workspace->expected_revision =
          in.workspace_expected_revision;
      effect->storage_commit->workspace->resulting_revision =
          in.workspace_resulting_revision;
      effect->storage_commit->workspace->snapshot.assign(
          in.workspace_snapshot.begin(), in.workspace_snapshot.end());
    }
  }
  return effect;
}

CoreResponseBatch ToResponseBatch(bridge::BridgeResponse response) {
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->operation_id = std::string(response.admission.operation_id);
  batch.admission->status = AdmissionStatusFromWire(response.admission.status);
  for (const bridge::BridgeStorageEffect& effect : response.storage_effects) {
    mojom::EffectEnvelopePtr projected = ToMojoStorageEffect(effect);
    if (!projected) {
      batch.effects.clear();
      batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
      return batch;
    }
    batch.effects.push_back(std::move(projected));
  }
  for (const bridge::BridgeWorkspaceEffect& effect :
       response.workspace_effects) {
    batch.effects.push_back(ToMojoWorkspaceEffect(effect));
  }
  for (bridge::BridgeAccountEffect& effect : response.account_effects) {
    mojom::EffectEnvelopePtr projected = ToMojoAccountEffect(effect);
    crypto::SecureZeroBuffer(base::span(effect.material));
    effect.material.clear();
    if (!projected) {
      batch.effects.clear();
      batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
      break;
    }
    batch.effects.push_back(std::move(projected));
  }
  for (const bridge::BridgeAssetEffect& effect : response.asset_effects) {
    batch.effects.push_back(ToMojoAssetEffect(effect));
  }
  // No refusal path, deliberately. A listing fetch rides the batch that also
  // carries the provider write's published state, so clearing the batch over a
  // malformed listing would turn an accepted credential save into an invalid
  // command — the fetch is the smaller loss of the two. The projection is
  // total, so there is nothing to refuse in any case.
  for (const bridge::BridgeListingEffect& effect : response.listing_effects) {
    batch.effects.push_back(ToMojoProviderListingEffect(effect));
  }
  // No refusal path either, and for a sharper version of the same reason. This
  // effect rides the batch that carries the published state of the flight it
  // claimed, so clearing the batch would leave the flight claimed with nothing
  // dispatched — a setup sheet that never stops saying it is testing. The
  // projection is total, so there is nothing to refuse in any case.
  for (const bridge::BridgeEndpointProbeEffect& effect :
       response.endpoint_probe_effects) {
    batch.effects.push_back(ToMojoEndpointProbeEffect(effect));
  }
  for (const bridge::BridgeProbeEffect& effect : response.probe_effects) {
    mojom::EffectEnvelopePtr projected = ToMojoProbeEffect(effect);
    if (!projected) {
      batch.effects.clear();
      batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
      break;
    }
    batch.effects.push_back(std::move(projected));
  }
  if (response.task_answer_events.size() >
      mojom::kMaxTaskAnswerEventsPerBatch) {
    batch.effects.clear();
    batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
    return batch;
  }
  for (const bridge::BridgeTaskAnswerEvent& event :
       response.task_answer_events) {
    mojom::TaskAnswerEventPtr projected = ToMojoTaskAnswerEvent(event);
    if (!projected) {
      batch.effects.clear();
      batch.task_answer_events.clear();
      batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
      return batch;
    }
    batch.task_answer_events.push_back(std::move(projected));
  }
  for (const bridge::BridgeState& state : response.states) {
    std::optional<CoreStatePublication> projected = ToStatePublication(state);
    if (!projected) {
      // `ToStatePublication` has already named the clause it refused. This
      // line names the consequence, which is the one the phone shows: every
      // effect of this batch withdrawn and the core answering unavailable
      // from here on, for a response the browser never saw refused.
      LOG(ERROR) << "[taffy_core_response_refused] reason=state-native-projection"
                 << " op=" << response.admission.operation_id;
      batch.effects.clear();
      batch.states.clear();
      batch.admission->status = mojom::AdmissionStatus::kCoreUnavailable;
      return batch;
    }
    batch.states.push_back(std::move(*projected));
  }
  return batch;
}

}  // namespace taffy::core_service_internal
