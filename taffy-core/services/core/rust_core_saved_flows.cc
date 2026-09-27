// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <utility>

#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/rust_core_bridge_handle.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy {
namespace {
namespace service = core_service::mojom;
namespace wire = core_service::wire;
namespace bridge = core_bridge;

service::SavedFlowReviewPtr Review(bridge::BridgeSavedFlowReview flow) {
  auto provenance = wire::SkillProvenanceFromWire(flow.provenance);
  auto status = wire::SkillStatusFromWire(flow.status);
  if (!provenance || !status || flow.skill_id.empty() ||
      flow.skill_id.size() > service::kMaxSkillIdBytes || flow.origin.empty() ||
      flow.origin.size() > service::kMaxNormalizedOriginBytes ||
      flow.active_version == 0u ||
      flow.active_version > service::kMaxSkillVersionsPerSkill ||
      flow.step_count == 0u || flow.step_count > service::kMaxSkillSteps ||
      flow.reviewed_steps.size() != flow.step_count) {
    return nullptr;
  }
  auto out = service::SavedFlowReview::New();
  out->skill_id = std::string(flow.skill_id);
  out->origin = std::string(flow.origin);
  out->provenance = *provenance;
  out->status = *status;
  out->active_version = flow.active_version;
  out->step_count = flow.step_count;
  out->installed_at_epoch_ms = flow.installed_at_epoch_ms;
  out->updated_at_epoch_ms = flow.updated_at_epoch_ms;
  if (flow.has_recorded_task) {
    if (flow.recorded_from_task_id.empty() ||
        flow.recorded_from_task_id.size() > service::kMaxIdentifierBytes) {
      return nullptr;
    }
    out->recorded_from_task_id = std::string(flow.recorded_from_task_id);
  } else if (!flow.recorded_from_task_id.empty()) {
    return nullptr;
  }
  for (const auto& step : flow.reviewed_steps) {
    if (step.verb.empty() || step.verb.size() > service::kMaxIdentifierBytes ||
        step.arguments.size() > service::kMaxSkillArgumentsPerStep) {
      return nullptr;
    }
    auto target_step = service::SkillObservedStep::New();
    target_step->verb = std::string(step.verb);
    target_step->postcondition = step.postcondition;
    target_step->has_fill = step.has_fill;
    target_step->fill_purpose = step.fill_purpose;
    for (const auto& arg : step.arguments) {
      auto kind = wire::SkillArgumentKindFromWire(arg.kind);
      if (!kind ||
          arg.public_address.size() > service::kMaxDestinationAddressBytes ||
          arg.has_public_address !=
              (*kind == service::SkillArgumentKind::kPublicAddress) ||
          arg.has_semantic_target !=
              (*kind == service::SkillArgumentKind::kSemanticTarget)) {
        return nullptr;
      }
      auto target = service::SkillObservedArgument::New();
      target->parameter = arg.parameter;
      target->kind = *kind;
      target->value = arg.value;
      target->purpose = arg.purpose;
      if (arg.has_public_address) {
        target->public_address = std::string(arg.public_address);
      }
      if (arg.has_semantic_target) {
        target->semantic_target = service::SkillSemanticTarget::New(
            arg.semantic_role, arg.semantic_phrase);
      }
      target_step->arguments.push_back(std::move(target));
    }
    out->reviewed_steps.push_back(std::move(target_step));
  }
  return out;
}
}  // namespace

service::SavedFlowQueryResultPtr RustCore::QuerySavedFlows(
    service::SavedFlowQueryCommandPtr command,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !command || !command->operation ||
      command->goal.size() > service::kMaxGoalBytes ||
      command->skill_id.size() > service::kMaxSkillIdBytes) {
    return nullptr;
  }
  const auto& operation = *command->operation;
  bridge::BridgeSavedFlowQuery input;
  input.operation.operation_id = operation.operation_id;
  input.operation.service_generation = operation.service_generation;
  input.operation.task_revision = operation.task_revision;
  input.operation.deadline_monotonic_ms = operation.deadline_monotonic_ms;
  input.operation.idempotency_key = operation.idempotency_key;
  input.kind = static_cast<uint8_t>(command->kind);
  input.goal = command->goal;
  input.skill_id = command->skill_id;
  input.expected_version = command->expected_version;
  auto reply = bridge::QuerySavedFlows(*bridge_->runtime(), std::move(input),
                                       now_monotonic_ms);
  const auto status = wire::SavedFlowQueryStatusFromWire(reply.status);
  if (!status || reply.operation.operation_id != operation.operation_id ||
      reply.operation.service_generation != operation.service_generation ||
      reply.operation.task_revision != operation.task_revision ||
      reply.operation.deadline_monotonic_ms !=
          operation.deadline_monotonic_ms ||
      reply.operation.idempotency_key != operation.idempotency_key ||
      reply.flows.size() > service::kMaxSavedFlowQueryResults ||
      (*status != service::SavedFlowQueryStatus::kAvailable &&
       !reply.flows.empty())) {
    return nullptr;
  }
  auto out = service::SavedFlowQueryResult::New();
  out->operation = command->operation.Clone();
  out->status = *status;
  for (auto& flow : reply.flows) {
    auto projected = Review(std::move(flow));
    if (!projected) {
      return nullptr;
    }
    out->flows.push_back(std::move(projected));
  }
  return out;
}
}  // namespace taffy
