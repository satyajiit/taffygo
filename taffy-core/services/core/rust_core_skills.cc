// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_skills.h"

#include <utility>

namespace taffy::core_service_internal {
namespace {

core_bridge::BridgeSkillOperation ToOperation(
    const core_service::mojom::OperationEnvelope& input) {
  core_bridge::BridgeSkillOperation output;
  output.operation_id = input.operation_id;
  output.service_generation = input.service_generation;
  output.task_revision = input.task_revision;
  output.deadline_monotonic_ms = input.deadline_monotonic_ms;
  output.idempotency_key = input.idempotency_key;
  return output;
}

}  // namespace

std::optional<core_bridge::BridgeSkillCommand> ToBridgeSkillCommand(
    const core_service::mojom::CoreServiceCommand& command) {
  if (command.kind !=
          core_service::mojom::CoreServiceCommandKind::kMutateSkill ||
      !command.operation || !command.mutate_skill) {
    return std::nullopt;
  }
  const auto& input = *command.mutate_skill;
  core_bridge::BridgeSkillCommand output;
  output.operation = ToOperation(*command.operation);
  output.kind = static_cast<uint8_t>(input.kind);
  output.skill_id = input.skill_id;
  output.expected_version = input.expected_version;
  output.origin = input.origin;
  output.admitted = input.admitted;
  output.enabled = input.enabled;
  output.recorded_at_epoch_ms = input.recorded_at_epoch_ms;
  for (const auto& clause : input.clauses) {
    if (!clause) {
      return std::nullopt;
    }
    core_bridge::BridgeSkillClause projected;
    projected.kind = static_cast<uint8_t>(clause->kind);
    projected.role = clause->role;
    projected.detail = clause->detail;
    output.clauses.push_back(std::move(projected));
  }
  for (const auto& step : input.steps) {
    if (!step) {
      return std::nullopt;
    }
    core_bridge::BridgeSkillStep projected;
    projected.verb = step->verb;
    projected.postcondition = step->postcondition;
    projected.has_fill = step->has_fill;
    projected.fill_purpose = step->fill_purpose;
    for (const auto& argument : step->arguments) {
      if (!argument) {
        return std::nullopt;
      }
      core_bridge::BridgeSkillArgument projected_argument;
      projected_argument.parameter = argument->parameter;
      projected_argument.kind = static_cast<uint8_t>(argument->kind);
      projected_argument.value = argument->value;
      projected_argument.purpose = argument->purpose;
      projected_argument.has_public_address = argument->public_address.has_value();
      projected_argument.public_address = argument->public_address.value_or("");
      projected_argument.has_semantic_target = !!argument->semantic_target;
      projected_argument.semantic_role = argument->semantic_target ? argument->semantic_target->role : 0u;
      projected_argument.semantic_phrase = argument->semantic_target ? argument->semantic_target->phrase : 0u;
      projected.arguments.push_back(std::move(projected_argument));
    }
    output.steps.push_back(std::move(projected));
  }
  return output;
}

}  // namespace taffy::core_service_internal
