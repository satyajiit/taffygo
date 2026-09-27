// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/strings/string_util.h"
#include "taffy/browser/core_api/core_api_command_factory.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

std::optional<service::SkillMutationKind> ProjectMutation(
    api::SiteSkillMutationKind value) {
  switch (value) {
    case api::SiteSkillMutationKind::kTeach:
      return service::SkillMutationKind::kTeach;
    case api::SiteSkillMutationKind::kUpdate:
      return service::SkillMutationKind::kUpdate;
    case api::SiteSkillMutationKind::kSetEnabled:
      return service::SkillMutationKind::kSetEnabled;
    case api::SiteSkillMutationKind::kRemove:
      return service::SkillMutationKind::kRemove;
  }
}

std::optional<service::SkillClauseKind> ProjectClauseKind(
    api::SiteSkillClauseKind value) {
  switch (value) {
    case api::SiteSkillClauseKind::kRolePresent:
      return service::SkillClauseKind::kRolePresent;
    case api::SiteSkillClauseKind::kPhraseAt:
      return service::SkillClauseKind::kPhraseAt;
    case api::SiteSkillClauseKind::kStateAt:
      return service::SkillClauseKind::kStateAt;
  }
}

std::optional<service::SkillArgumentKind> ProjectArgumentKind(
    api::SiteSkillArgumentKind value) {
  switch (value) {
    case api::SiteSkillArgumentKind::kFromEarlierStep:
      return service::SkillArgumentKind::kFromEarlierStep;
    case api::SiteSkillArgumentKind::kFromPerson:
      return service::SkillArgumentKind::kFromPerson;
    case api::SiteSkillArgumentKind::kChoice:
      return service::SkillArgumentKind::kChoice;
    case api::SiteSkillArgumentKind::kCount:
      return service::SkillArgumentKind::kCount;
    case api::SiteSkillArgumentKind::kFlag:
      return service::SkillArgumentKind::kFlag;
    case api::SiteSkillArgumentKind::kPublicAddress:
      return service::SkillArgumentKind::kPublicAddress;
    case api::SiteSkillArgumentKind::kSemanticTarget:
      return service::SkillArgumentKind::kSemanticTarget;
  }
}

bool IsSkillId(std::string_view value) {
  if (value.empty() || value.size() > api::kMaxSkillIdBytes) {
    return false;
  }
  for (char character : value) {
    if (!base::IsAsciiLower(character) && !base::IsAsciiDigit(character) &&
        character != '-' && character != '.') {
      return false;
    }
  }
  return true;
}

bool HasMutationShape(api::SiteSkillMutationKind kind,
                      uint32_t expected_version,
                      const std::string& origin,
                      size_t clauses,
                      size_t steps,
                      uint32_t admitted,
                      bool enabled) {
  switch (kind) {
    case api::SiteSkillMutationKind::kTeach:
      return expected_version == 0u && !origin.empty() && clauses > 0u &&
             steps > 0u && static_cast<size_t>(admitted) == steps && !enabled;
    case api::SiteSkillMutationKind::kUpdate:
      return expected_version > 0u && !origin.empty() && clauses > 0u &&
             steps > 0u && static_cast<size_t>(admitted) == steps && !enabled;
    case api::SiteSkillMutationKind::kSetEnabled:
      return expected_version > 0u && origin.empty() && clauses == 0u &&
             steps == 0u && admitted == 0u;
    case api::SiteSkillMutationKind::kRemove:
      return expected_version > 0u && origin.empty() && clauses == 0u &&
             steps == 0u && admitted == 0u && !enabled;
  }
}

}  // namespace

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildMutateSiteSkill(
    api::SiteSkillMutationKind kind,
    std::string skill_id,
    uint32_t expected_version,
    std::string origin,
    std::vector<api::SiteSkillObservedClausePtr> clauses,
    std::vector<api::SiteSkillObservedStepPtr> steps,
    uint32_t admitted,
    bool enabled,
    uint64_t recorded_at_epoch_ms,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsSkillId(skill_id) || recorded_at_epoch_ms == 0u ||
      origin.size() > api::kMaxSkillOriginBytes ||
      clauses.size() > api::kMaxSkillMatchClauses ||
      steps.size() > api::kMaxSkillSteps ||
      !HasMutationShape(kind, expected_version, origin, clauses.size(),
                        steps.size(), admitted, enabled)) {
    return std::nullopt;
  }
  const std::optional<service::SkillMutationKind> service_kind =
      ProjectMutation(kind);
  if (!service_kind) {
    return std::nullopt;
  }
  std::vector<service::SkillObservedClausePtr> service_clauses;
  service_clauses.reserve(clauses.size());
  for (const auto& clause : clauses) {
    if (!clause) {
      return std::nullopt;
    }
    const std::optional<service::SkillClauseKind> clause_kind =
        ProjectClauseKind(clause->kind);
    if (!clause_kind) {
      return std::nullopt;
    }
    service_clauses.push_back(service::SkillObservedClause::New(
        *clause_kind, clause->role, clause->detail));
  }
  std::vector<service::SkillObservedStepPtr> service_steps;
  service_steps.reserve(steps.size());
  for (const auto& step : steps) {
    if (!step || step->verb.empty() ||
        step->verb.size() > api::kMaxSkillToolNameBytes ||
        step->arguments.size() > api::kMaxSkillArgumentsPerStep) {
      return std::nullopt;
    }
    std::vector<service::SkillObservedArgumentPtr> arguments;
    arguments.reserve(step->arguments.size());
    for (const auto& argument : step->arguments) {
      if (!argument) {
        return std::nullopt;
      }
      const std::optional<service::SkillArgumentKind> argument_kind =
          ProjectArgumentKind(argument->kind);
      if (!argument_kind) {
        return std::nullopt;
      }
      arguments.push_back(service::SkillObservedArgument::New(
          argument->parameter, *argument_kind, argument->value,
          argument->purpose, argument->public_address,
          argument->semantic_target ? service::SkillSemanticTarget::New(
                                          argument->semantic_target->role,
                                          argument->semantic_target->phrase)
                                    : nullptr));
    }
    service_steps.push_back(service::SkillObservedStep::New(
        step->verb, std::move(arguments), step->postcondition, step->has_fill,
        step->fill_purpose));
  }

  auto core_operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  auto service_operation = ProjectOperation(*core_operation);
  auto core_command = api::CoreCommand::New();
  core_command->operation = std::move(core_operation);
  core_command->kind = api::CoreCommandKind::kMutateSiteSkill;
  core_command->mutate_site_skill = api::SiteSkillMutationBody::New(
      kind, skill_id, expected_version, origin, std::move(clauses),
      std::move(steps), admitted, enabled, recorded_at_epoch_ms);
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = std::move(service_operation);
  service_command->kind = service::CoreServiceCommandKind::kMutateSkill;
  service_command->mutate_skill = service::MutateSkillCommand::New(
      *service_kind, std::move(skill_id), expected_version, std::move(origin),
      std::move(service_clauses), std::move(service_steps), admitted, enabled,
      recorded_at_epoch_ms);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

}  // namespace taffy
