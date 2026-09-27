// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_command_validation_internal.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

size_t CommandBodyCount(const mojom::CoreServiceCommand& command) {
  return static_cast<size_t>(!!command.start_task) +
         static_cast<size_t>(!!command.cancel_task) +
         static_cast<size_t>(!!command.user_decision) +
         static_cast<size_t>(!!command.auth_callback) +
         static_cast<size_t>(!!command.permission_result) +
         static_cast<size_t>(!!command.start_auth) +
         static_cast<size_t>(!!command.request_email_link) +
         static_cast<size_t>(!!command.sign_out) +
         static_cast<size_t>(!!command.auth_credential_result) +
         static_cast<size_t>(!!command.correct_workspace_fact) +
         static_cast<size_t>(!!command.exclude_workspace_source) +
         static_cast<size_t>(!!command.request_workspace_export) +
         static_cast<size_t>(!!command.set_asset_delivery_policy) +
         static_cast<size_t>(!!command.request_asset) +
         static_cast<size_t>(!!command.remove_asset) +
         static_cast<size_t>(!!command.save_provider_credential) +
         static_cast<size_t>(!!command.set_provider_credential_state) +
         static_cast<size_t>(!!command.probe_provider_credential) +
         static_cast<size_t>(!!command.forget_provider_credential) +
         static_cast<size_t>(!!command.start_provider_auth) +
         static_cast<size_t>(!!command.provider_auth_callback) +
         static_cast<size_t>(!!command.save_custom_provider) +
         static_cast<size_t>(!!command.remove_custom_provider) +
         static_cast<size_t>(!!command.complete_handover) +
         static_cast<size_t>(!!command.expire_handover) +
         static_cast<size_t>(!!command.supply_user_input) +
         static_cast<size_t>(!!command.follow_up) +
         static_cast<size_t>(!!command.supply_field_values) +
         static_cast<size_t>(!!command.set_provider_model_preference) +
         static_cast<size_t>(!!command.probe_custom_endpoint) +
         static_cast<size_t>(!!command.request_composer_completion) +
         static_cast<size_t>(!!command.cancel_composer_completion) +
         static_cast<size_t>(!!command.pause_task) +
         static_cast<size_t>(!!command.resume_task) +
         static_cast<size_t>(!!command.take_over) +
         static_cast<size_t>(!!command.set_assistant_configuration) +
         static_cast<size_t>(!!command.save_workspace) +
         static_cast<size_t>(!!command.rename_workspace) +
         static_cast<size_t>(!!command.delete_workspace) +
         static_cast<size_t>(!!command.discard_workspace) +
         static_cast<size_t>(!!command.search_library) +
         static_cast<size_t>(!!command.save_library_fact) +
         static_cast<size_t>(!!command.remove_library_entry) +
         static_cast<size_t>(!!command.request_library_export) +
         static_cast<size_t>(!!command.search_memory) +
         static_cast<size_t>(!!command.upsert_memory) +
         static_cast<size_t>(!!command.delete_memory) +
         static_cast<size_t>(!!command.accept_task_artifact) +
         static_cast<size_t>(!!command.export_task_artifact) +
         static_cast<size_t>(!!command.replace_saved_data_snapshot) +
         static_cast<size_t>(!!command.mutate_skill) +
         static_cast<size_t>(!!command.cancel_provider_auth);
}

}  // namespace

bool HasMatchingCommandBody(const mojom::CoreServiceCommand& command) {
  if (CommandBodyCount(command) != 1u) {
    return false;
  }
  switch (command.kind) {
    case mojom::CoreServiceCommandKind::kStartTask:
      return !!command.start_task;
    case mojom::CoreServiceCommandKind::kCancelTask:
      return !!command.cancel_task;
    case mojom::CoreServiceCommandKind::kUserDecision:
      return !!command.user_decision;
    case mojom::CoreServiceCommandKind::kAuthCallback:
      return !!command.auth_callback;
    case mojom::CoreServiceCommandKind::kPermissionResult:
      return !!command.permission_result;
    case mojom::CoreServiceCommandKind::kStartAuth:
      return !!command.start_auth;
    case mojom::CoreServiceCommandKind::kRequestEmailLink:
      return !!command.request_email_link;
    case mojom::CoreServiceCommandKind::kSignOut:
      return !!command.sign_out;
    case mojom::CoreServiceCommandKind::kAuthCredentialResult:
      return !!command.auth_credential_result;
    case mojom::CoreServiceCommandKind::kCorrectWorkspaceFact:
      return !!command.correct_workspace_fact;
    case mojom::CoreServiceCommandKind::kExcludeWorkspaceSource:
      return !!command.exclude_workspace_source;
    case mojom::CoreServiceCommandKind::kRequestWorkspaceExport:
      return !!command.request_workspace_export;
    case mojom::CoreServiceCommandKind::kSaveWorkspace:
      return !!command.save_workspace;
    case mojom::CoreServiceCommandKind::kRenameWorkspace:
      return !!command.rename_workspace;
    case mojom::CoreServiceCommandKind::kDeleteWorkspace:
      return !!command.delete_workspace;
    case mojom::CoreServiceCommandKind::kDiscardWorkspace:
      return !!command.discard_workspace;
    case mojom::CoreServiceCommandKind::kSearchLibrary:
      return !!command.search_library;
    case mojom::CoreServiceCommandKind::kSaveLibraryFact:
      return !!command.save_library_fact;
    case mojom::CoreServiceCommandKind::kRemoveLibraryEntry:
      return !!command.remove_library_entry;
    case mojom::CoreServiceCommandKind::kRequestLibraryExport:
      return !!command.request_library_export;
    case mojom::CoreServiceCommandKind::kSearchMemory:
      return !!command.search_memory;
    case mojom::CoreServiceCommandKind::kUpsertMemory:
      return !!command.upsert_memory;
    case mojom::CoreServiceCommandKind::kDeleteMemory:
      return !!command.delete_memory;
    case mojom::CoreServiceCommandKind::kAcceptTaskArtifact:
      return !!command.accept_task_artifact;
    case mojom::CoreServiceCommandKind::kExportTaskArtifact:
      return !!command.export_task_artifact;
    case mojom::CoreServiceCommandKind::kSetAssetDeliveryPolicy:
      return !!command.set_asset_delivery_policy;
    case mojom::CoreServiceCommandKind::kRequestAsset:
      return !!command.request_asset;
    case mojom::CoreServiceCommandKind::kRemoveAsset:
      return !!command.remove_asset;
    case mojom::CoreServiceCommandKind::kSaveProviderCredential:
      return !!command.save_provider_credential;
    case mojom::CoreServiceCommandKind::kForgetProviderCredential:
      return !!command.forget_provider_credential;
    case mojom::CoreServiceCommandKind::kSetProviderCredentialState:
      return !!command.set_provider_credential_state;
    case mojom::CoreServiceCommandKind::kProbeProviderCredential:
      return !!command.probe_provider_credential;
    case mojom::CoreServiceCommandKind::kStartProviderAuth:
      return !!command.start_provider_auth;
    case mojom::CoreServiceCommandKind::kProviderAuthCallback:
      return !!command.provider_auth_callback;
    case mojom::CoreServiceCommandKind::kSaveCustomProvider:
      return !!command.save_custom_provider;
    case mojom::CoreServiceCommandKind::kRemoveCustomProvider:
      return !!command.remove_custom_provider;
    case mojom::CoreServiceCommandKind::kCompleteHandover:
      return !!command.complete_handover;
    case mojom::CoreServiceCommandKind::kExpireHandover:
      return !!command.expire_handover;
    case mojom::CoreServiceCommandKind::kSupplyUserInput:
      return !!command.supply_user_input;
    case mojom::CoreServiceCommandKind::kFollowUp:
      return !!command.follow_up;
    case mojom::CoreServiceCommandKind::kSupplyFieldValues:
      return !!command.supply_field_values;
    case mojom::CoreServiceCommandKind::kSetProviderModelPreference:
      return !!command.set_provider_model_preference;
    case mojom::CoreServiceCommandKind::kProbeCustomEndpoint:
      return !!command.probe_custom_endpoint;
    case mojom::CoreServiceCommandKind::kRequestComposerCompletion:
      return !!command.request_composer_completion;
    case mojom::CoreServiceCommandKind::kCancelComposerCompletion:
      return !!command.cancel_composer_completion;
    case mojom::CoreServiceCommandKind::kPauseTask:
      return !!command.pause_task;
    case mojom::CoreServiceCommandKind::kResumeTask:
      return !!command.resume_task;
    case mojom::CoreServiceCommandKind::kTakeOver:
      return !!command.take_over;
    case mojom::CoreServiceCommandKind::kSetAssistantConfiguration:
      return !!command.set_assistant_configuration;
    case mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot:
      return !!command.replace_saved_data_snapshot;
    case mojom::CoreServiceCommandKind::kMutateSkill:
      return !!command.mutate_skill;
    case mojom::CoreServiceCommandKind::kCancelProviderAuth:
      return !!command.cancel_provider_auth;
  }
  return false;
}

std::optional<size_t> SkillCommandByteSize(
    const mojom::CoreServiceCommand& command,
    size_t ceiling) {
  if (!command.mutate_skill) {
    return std::nullopt;
  }
  const mojom::MutateSkillCommand& skill = *command.mutate_skill;
  size_t total = 0u;
  if (skill.skill_id.empty() ||
      skill.skill_id.size() > mojom::kMaxSkillIdBytes ||
      !AddBounded(skill.skill_id.size(), ceiling, &total) ||
      skill.recorded_at_epoch_ms == 0u) {
    return std::nullopt;
  }
  for (char character : skill.skill_id) {
    if (!((character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || character == '-' ||
          character == '.')) {
      return std::nullopt;
    }
  }
  const bool has_recording = !skill.origin.empty() &&
                             skill.origin.size() <=
                                 mojom::kMaxNormalizedOriginBytes &&
                             !skill.clauses.empty() &&
                             skill.clauses.size() <=
                                 mojom::kMaxSkillMatchClauses &&
                             !skill.steps.empty() &&
                             skill.steps.size() <= mojom::kMaxSkillSteps &&
                             skill.admitted == skill.steps.size();
  switch (skill.kind) {
    case mojom::SkillMutationKind::kTeach:
      if (skill.expected_version != 0u || skill.enabled || !has_recording) {
        return std::nullopt;
      }
      break;
    case mojom::SkillMutationKind::kUpdate:
      if (skill.expected_version == 0u || skill.enabled || !has_recording) {
        return std::nullopt;
      }
      break;
    case mojom::SkillMutationKind::kSetEnabled:
      if (skill.expected_version == 0u || !skill.origin.empty() ||
          !skill.clauses.empty() || !skill.steps.empty() ||
          skill.admitted != 0u) {
        return std::nullopt;
      }
      break;
    case mojom::SkillMutationKind::kRemove:
      if (skill.expected_version == 0u || skill.enabled ||
          !skill.origin.empty() || !skill.clauses.empty() ||
          !skill.steps.empty() || skill.admitted != 0u) {
        return std::nullopt;
      }
      break;
  }
  if (!skill.origin.empty() &&
      !AddBounded(skill.origin.size(), ceiling, &total)) {
    return std::nullopt;
  }
  for (const auto& clause : skill.clauses) {
    if (!clause) {
      return std::nullopt;
    }
  }
  for (const auto& step : skill.steps) {
    if (!step || step->verb.empty() ||
        step->verb.size() > mojom::kMaxToolIdBytes ||
        step->arguments.size() > mojom::kMaxSkillArgumentsPerStep ||
        !AddBounded(step->verb.size(), ceiling, &total)) {
      return std::nullopt;
    }
    for (const auto& argument : step->arguments) {
      if (!argument) {
        return std::nullopt;
      }
    }
  }
  return total;
}

}  // namespace taffy
