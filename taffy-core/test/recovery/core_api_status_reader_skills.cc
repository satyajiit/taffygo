// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/core_api_status_reader.h"

#include <array>
#include <utility>

namespace taffy::test::internal {

bool CoreStatusWireReader::SkipSavedData() {
  if (!ReadClosedEnum(api::SavedDataAvailability::kLoading,
                      api::SavedDataAvailability::kUnavailable) ||
      !ReadU64()) {
    return false;
  }
  const auto sign_ins = ReadLength(api::kMaxSavedSignIns);
  if (!sign_ins) {
    return false;
  }
  for (uint32_t index = 0u; index < *sign_ins; ++index) {
    if (!SkipString(api::kMaxIdentifierBytes) ||
        !SkipString(api::kMaxSavedSignInSiteBytes) ||
        !SkipString(api::kMaxSavedSignInUsernameBytes) || !ReadU64()) {
      return false;
    }
  }
  if (!ReadClosedEnum(api::SavedDataAvailability::kLoading,
                      api::SavedDataAvailability::kUnavailable) ||
      !ReadU64()) {
    return false;
  }
  const auto people = ReadLength(api::kMaxSavedDetails);
  if (!people) {
    return false;
  }
  constexpr std::array<uint64_t, 8> kPersonFieldBounds = {
      api::kMaxIdentifierBytes, api::kMaxSavedDetailNameBytes,
      api::kMaxSavedDetailNameBytes, api::kMaxSavedDetailEmailBytes,
      api::kMaxSavedDetailPhoneBytes, api::kMaxSavedDetailAddressBytes,
      api::kMaxSavedDetailPostcodeBytes, api::kMaxSavedDetailCountryBytes};
  for (uint32_t index = 0u; index < *people; ++index) {
    for (uint64_t bound : kPersonFieldBounds) {
      if (!SkipString(bound)) {
        return false;
      }
    }
  }
  return true;
}

std::optional<ObservedSiteSkill> CoreStatusWireReader::ReadSiteSkill() {
  auto skill_id = ReadString(api::kMaxSkillIdBytes);
  auto origin = ReadString(api::kMaxSkillOriginBytes);
  const auto provenance = ReadClosedEnum(
      api::SiteSkillProvenanceView::kAuthored,
      api::SiteSkillProvenanceView::kRecordedFromTask);
  const auto status = ReadClosedEnum(api::SiteSkillStatusView::kDraft,
                                   api::SiteSkillStatusView::kDisabled);
  const auto version = ReadU32();
  const auto step_count = ReadU32();
  const auto installed = ReadU64();
  const auto updated = ReadU64();
  const auto recorded_task_present = ReadBool();
  if (!skill_id || !origin || !provenance || !status || !version ||
      !step_count || !installed || !updated || !recorded_task_present) {
    return std::nullopt;
  }
  ObservedSiteSkill skill;
  skill.skill_id = std::move(*skill_id);
  skill.origin = std::move(*origin);
  skill.provenance = *provenance;
  skill.status = *status;
  skill.active_version = *version;
  skill.step_count = *step_count;
  skill.installed_at_epoch_ms = *installed;
  skill.updated_at_epoch_ms = *updated;
  if (*recorded_task_present) {
    skill.recorded_from_task_id = ReadString(api::kMaxIdentifierBytes);
    if (!skill.recorded_from_task_id) {
      return std::nullopt;
    }
  }
  const auto steps = ReadLength(api::kMaxSkillSteps);
  if (!steps) {
    return std::nullopt;
  }
  for (uint32_t index = 0u; index < *steps; ++index) {
    auto step = ReadSkillStep();
    if (!step) {
      return std::nullopt;
    }
    skill.reviewed_steps.push_back(std::move(*step));
  }
  skill.review_complete = !skill.reviewed_steps.empty() &&
                          skill.reviewed_steps.size() == skill.step_count;
  if (!skill.reviewed_steps.empty()) {
    const auto& first = skill.reviewed_steps.front();
    if (first.verb == "browser.navigate" && first.arguments.size() == 1u) {
      const auto& address = first.arguments.front();
      if (address.parameter == 0u &&
          address.kind == api::SiteSkillArgumentKind::kPublicAddress) {
        skill.starting_address = address.public_address;
      }
    }
  }
  return skill;
}

std::optional<ObservedSkillStep> CoreStatusWireReader::ReadSkillStep() {
  auto verb = ReadString(api::kMaxSkillToolNameBytes);
  const auto arguments = ReadLength(api::kMaxSkillArgumentsPerStep);
  if (!verb || !arguments) {
    return std::nullopt;
  }
  ObservedSkillStep step;
  step.verb = std::move(*verb);
  for (uint32_t index = 0u; index < *arguments; ++index) {
    auto argument = ReadSkillArgument();
    if (!argument) {
      return std::nullopt;
    }
    step.arguments.push_back(std::move(*argument));
  }
  const auto postcondition = ReadU32();
  const auto has_fill = ReadBool();
  const auto fill_purpose = ReadU32();
  if (!postcondition || !has_fill || !fill_purpose) {
    return std::nullopt;
  }
  step.postcondition = *postcondition;
  step.has_fill = *has_fill;
  step.fill_purpose = *fill_purpose;
  return step;
}

std::optional<ObservedSkillArgument> CoreStatusWireReader::ReadSkillArgument() {
  const auto parameter = ReadU32();
  const auto kind = ReadClosedEnum(api::SiteSkillArgumentKind::kFromEarlierStep,
                                   api::SiteSkillArgumentKind::kSemanticTarget);
  const auto value = ReadU64();
  const auto purpose = ReadU32();
  const auto address_present = ReadBool();
  if (!parameter || !kind || !value || !purpose || !address_present) {
    return std::nullopt;
  }
  ObservedSkillArgument argument;
  argument.parameter = *parameter;
  argument.kind = *kind;
  argument.value = *value;
  argument.purpose = *purpose;
  if (*address_present) {
    argument.public_address = ReadString(api::kMaxTaskGoalBytes);
    if (!argument.public_address) {
      return std::nullopt;
    }
  }
  const auto target_present = ReadBool();
  if (!target_present) {
    return std::nullopt;
  }
  if (*target_present) {
    const auto role = ReadU32();
    const auto phrase = ReadU32();
    if (!role || !phrase) {
      return std::nullopt;
    }
    argument.semantic_target = ObservedSkillSemanticTarget{*role, *phrase};
  }
  return argument;
}

bool CoreStatusWireReader::ReadProjectionTail(bool* site_skills_complete) {
  const auto builtins = ReadLength(api::kMaxBuiltinSkills);
  if (!builtins) {
    return false;
  }
  for (uint32_t index = 0u; index < *builtins; ++index) {
    if (!ReadClosedEnum(api::BuiltinSkillIdView::kGeneralWebResearch,
                        api::BuiltinSkillIdView::kDocumentGenerator) ||
        !ReadU32() ||
        !ReadClosedEnum(api::AssistantAbilityView::kPagesLookup,
                        api::AssistantAbilityView::kKeep) ||
        !ReadBool() ||
        !ReadClosedEnum(api::BuiltinSkillAvailabilityView::kAvailable,
                        api::BuiltinSkillAvailabilityView::kPolicyUnavailable) ||
        !ReadU32() || !ReadU32() || !ReadU32() || !ReadU32()) {
      return false;
    }
  }
  const auto mode = ReadClosedEnum(api::CoreStatusProjectionMode::kComplete,
                                  api::CoreStatusProjectionMode::kRecoveryRequired);
  const auto omissions = ReadLength(api::kMaxCoreStatusProjectionOmissions);
  if (!mode || !omissions) {
    return false;
  }
  *site_skills_complete = *mode == api::CoreStatusProjectionMode::kComplete;
  for (uint32_t index = 0u; index < *omissions; ++index) {
    const auto family = ReadClosedEnum(
        api::CoreStatusProjectionFamily::kActiveTasks,
        api::CoreStatusProjectionFamily::kSiteSkills);
    if (!family || !ReadU64() || !ReadU32()) {
      return false;
    }
    if (*family == api::CoreStatusProjectionFamily::kSiteSkills) {
      *site_skills_complete = false;
    }
  }
  return (*mode == api::CoreStatusProjectionMode::kComplete) == (*omissions == 0u);
}

}  // namespace taffy::test::internal
