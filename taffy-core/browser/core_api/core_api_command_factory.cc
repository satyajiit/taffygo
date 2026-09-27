// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <array>
#include <limits>
#include <string_view>
#include <utility>

#include "base/rand_util.h"
#include "base/uuid.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kCommandDeadlineMs = 30'000;

class ChromiumCoreApiEntropySource final : public CoreApiEntropySource {
 public:
  std::string NewOpaqueId(std::string_view domain) override {
    return std::string(domain) + "-" +
           base::Uuid::GenerateRandomV4().AsLowercaseString();
  }

  std::array<uint8_t, 32> NewTaskSeed() override {
    std::array<uint8_t, 32> seed{};
    base::RandBytes(seed);
    return seed;
  }
};

uint64_t DeadlineFrom(uint64_t now_monotonic_ms) {
  if (now_monotonic_ms >
      std::numeric_limits<uint64_t>::max() - kCommandDeadlineMs) {
    return std::numeric_limits<uint64_t>::max();
  }
  return now_monotonic_ms + kCommandDeadlineMs;
}

size_t CommandBodyCount(const api::CoreCommand& command) {
  return static_cast<size_t>(!!command.start_task) +
         static_cast<size_t>(!!command.cancel_task) +
         static_cast<size_t>(!!command.approve_action) +
         static_cast<size_t>(!!command.retry_core) +
         static_cast<size_t>(!!command.permission_result) +
         static_cast<size_t>(!!command.start_auth) +
         static_cast<size_t>(!!command.request_email_link) +
         static_cast<size_t>(!!command.sign_out) +
         static_cast<size_t>(!!command.auth_credential_result) +
         static_cast<size_t>(!!command.correct_workspace_fact) +
         static_cast<size_t>(!!command.exclude_workspace_source) +
         static_cast<size_t>(!!command.request_workspace_export) +
         static_cast<size_t>(!!command.request_asset) +
         static_cast<size_t>(!!command.remove_asset) +
         static_cast<size_t>(!!command.set_asset_policy) +
         static_cast<size_t>(!!command.save_provider_credential) +
         static_cast<size_t>(!!command.forget_provider_credential) +
         static_cast<size_t>(!!command.set_provider_credential_state) +
         static_cast<size_t>(!!command.probe_provider_key) +
         static_cast<size_t>(!!command.start_provider_auth) +
         static_cast<size_t>(!!command.save_custom_provider) +
         static_cast<size_t>(!!command.remove_custom_provider) +
         static_cast<size_t>(!!command.complete_handover) +
         static_cast<size_t>(!!command.supply_user_input) +
         static_cast<size_t>(!!command.follow_up) +
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
         static_cast<size_t>(!!command.request_task_artifact_export) +
         static_cast<size_t>(!!command.mutate_site_skill) +
         static_cast<size_t>(!!command.cancel_provider_auth) +
         static_cast<size_t>(!!command.start_library_refresh);
}

std::optional<service::AssistantAbility> ProjectAssistantAbility(
    api::AssistantAbilityView ability) {
  switch (ability) {
    case api::AssistantAbilityView::kPagesLookup:
      return service::AssistantAbility::kPagesLookup;
    case api::AssistantAbilityView::kPagesCompare:
      return service::AssistantAbility::kPagesCompare;
    case api::AssistantAbilityView::kPagesSummarize:
      return service::AssistantAbility::kPagesSummarize;
    case api::AssistantAbilityView::kPagesTable:
      return service::AssistantAbility::kPagesTable;
    case api::AssistantAbilityView::kProducts:
      return service::AssistantAbility::kProducts;
    case api::AssistantAbilityView::kOffers:
      return service::AssistantAbility::kOffers;
    case api::AssistantAbilityView::kForm:
      return service::AssistantAbility::kForm;
    case api::AssistantAbilityView::kDownloads:
      return service::AssistantAbility::kDownloads;
    case api::AssistantAbilityView::kPdf:
      return service::AssistantAbility::kPdf;
    case api::AssistantAbilityView::kSheet:
      return service::AssistantAbility::kSheet;
    case api::AssistantAbilityView::kDocument:
      return service::AssistantAbility::kDocument;
    case api::AssistantAbilityView::kDepth:
      return service::AssistantAbility::kDepth;
    case api::AssistantAbilityView::kTrip:
      return service::AssistantAbility::kTrip;
    case api::AssistantAbilityView::kPictures:
      return service::AssistantAbility::kPictures;
    case api::AssistantAbilityView::kVideo:
      return service::AssistantAbility::kVideo;
    case api::AssistantAbilityView::kKeep:
      return service::AssistantAbility::kKeep;
  }
  return std::nullopt;
}

std::optional<service::PersonalityPreset> ProjectPersonalityPreset(
    api::PersonalityPresetView preset) {
  switch (preset) {
    case api::PersonalityPresetView::kCarefulResearcher:
      return service::PersonalityPreset::kCarefulResearcher;
    case api::PersonalityPresetView::kQuickShopper:
      return service::PersonalityPreset::kQuickShopper;
    case api::PersonalityPresetView::kTripPlanner:
      return service::PersonalityPreset::kTripPlanner;
  }
  return std::nullopt;
}

}  // namespace

std::unique_ptr<CoreApiEntropySource> CreateCoreApiEntropySource() {
  return std::make_unique<ChromiumCoreApiEntropySource>();
}

CoreApiCommandFactory::CoreApiCommandFactory(
    std::string browser_profile_id,
    std::unique_ptr<CoreApiEntropySource> entropy_source)
    : browser_profile_id_(std::move(browser_profile_id)),
      entropy_source_(std::move(entropy_source)) {}

CoreApiCommandFactory::~CoreApiCommandFactory() = default;

api::CoreCommandPtr CoreApiCommandFactory::BuildRetryCore(
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  auto command = api::CoreCommand::New();
  command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  command->kind = api::CoreCommandKind::kRetryCore;
  command->retry_core = api::RetryCoreBody::New(service_generation);
  return HasValidGeneratedBody(*command) ? std::move(command) : nullptr;
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildSetAssistantConfiguration(
    uint64_t expected_revision,
    std::vector<api::AssistantAbilityView> disabled_abilities,
    api::PersonalityPresetView preset,
    uint32_t pace,
    uint32_t length,
    uint32_t check_in,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (disabled_abilities.size() > api::kMaxAssistantAbilities ||
      pace > api::kMaxPersonalityScale || length > api::kMaxPersonalityScale ||
      check_in > api::kMaxPersonalityScale) {
    return std::nullopt;
  }
  for (size_t index = 1; index < disabled_abilities.size(); ++index) {
    if (static_cast<uint32_t>(disabled_abilities[index - 1]) >=
        static_cast<uint32_t>(disabled_abilities[index])) {
      return std::nullopt;
    }
  }
  std::vector<service::AssistantAbility> service_abilities;
  service_abilities.reserve(disabled_abilities.size());
  for (api::AssistantAbilityView ability : disabled_abilities) {
    std::optional<service::AssistantAbility> projected =
        ProjectAssistantAbility(ability);
    if (!projected) {
      return std::nullopt;
    }
    service_abilities.push_back(*projected);
  }
  std::optional<service::PersonalityPreset> service_preset =
      ProjectPersonalityPreset(preset);
  if (!service_preset) {
    return std::nullopt;
  }
  auto core_operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  auto service_operation = ProjectOperation(*core_operation);
  auto core_command = api::CoreCommand::New();
  core_command->operation = std::move(core_operation);
  core_command->kind = api::CoreCommandKind::kSetAssistantConfiguration;
  core_command->set_assistant_configuration =
      api::SetAssistantConfigurationBody::New(expected_revision,
                                              std::move(disabled_abilities),
                                              preset, pace, length, check_in);
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = std::move(service_operation);
  service_command->kind =
      service::CoreServiceCommandKind::kSetAssistantConfiguration;
  service_command->set_assistant_configuration =
      service::SetAssistantConfigurationCommand::New(
          expected_revision, std::move(service_abilities), *service_preset,
          pace, length, check_in);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

// static
bool CoreApiCommandFactory::IsSafeCommandKind(api::CoreCommandKind kind) {
  switch (kind) {
    case api::CoreCommandKind::kStartTask:
    case api::CoreCommandKind::kRetryCore:
    case api::CoreCommandKind::kStartAuth:
    case api::CoreCommandKind::kRequestEmailLink:
    case api::CoreCommandKind::kSignOut:
    case api::CoreCommandKind::kAuthCredentialResult:
    case api::CoreCommandKind::kCorrectWorkspaceFact:
    case api::CoreCommandKind::kExcludeWorkspaceSource:
    case api::CoreCommandKind::kRequestWorkspaceExport:
    case api::CoreCommandKind::kSaveWorkspace:
    case api::CoreCommandKind::kRenameWorkspace:
    case api::CoreCommandKind::kDeleteWorkspace:
    case api::CoreCommandKind::kDiscardWorkspace:
    case api::CoreCommandKind::kSearchLibrary:
    case api::CoreCommandKind::kSaveLibraryFact:
    case api::CoreCommandKind::kRemoveLibraryEntry:
    case api::CoreCommandKind::kRequestLibraryExport:
    case api::CoreCommandKind::kStartLibraryRefresh:
    case api::CoreCommandKind::kSearchMemory:
    case api::CoreCommandKind::kUpsertMemory:
    case api::CoreCommandKind::kDeleteMemory:
    case api::CoreCommandKind::kAcceptTaskArtifact:
    case api::CoreCommandKind::kRequestTaskArtifactExport:
    case api::CoreCommandKind::kCancelTask:
    case api::CoreCommandKind::kApproveAction:
    case api::CoreCommandKind::kPermissionResult:
    case api::CoreCommandKind::kRequestAsset:
    case api::CoreCommandKind::kRemoveAsset:
    case api::CoreCommandKind::kSetAssetPolicy:
    case api::CoreCommandKind::kPauseTask:
    case api::CoreCommandKind::kResumeTask:
    case api::CoreCommandKind::kTakeOver:
    case api::CoreCommandKind::kSetAssistantConfiguration:
    case api::CoreCommandKind::kMutateSiteSkill:
      return true;
    // The provider seam, built by core_api_command_factory_provider.cc. Each
    // has a Core Service body to project onto and a plane behind it that
    // performs the work, which is what this allowlist asserts.
    case api::CoreCommandKind::kSaveProviderCredential:
    case api::CoreCommandKind::kForgetProviderCredential:
    case api::CoreCommandKind::kSetProviderCredentialState:
    case api::CoreCommandKind::kProbeProviderKey:
    case api::CoreCommandKind::kStartProviderAuth:
    case api::CoreCommandKind::kSaveCustomProvider:
    case api::CoreCommandKind::kRemoveCustomProvider:
    case api::CoreCommandKind::kCompleteHandover:
    case api::CoreCommandKind::kSupplyUserInput:
    case api::CoreCommandKind::kFollowUp:
    case api::CoreCommandKind::kSetProviderModelPreference:
    case api::CoreCommandKind::kProbeCustomEndpoint:
    case api::CoreCommandKind::kCancelProviderAuth:
      return true;
    // The composer's completion request and its withdrawal. They are on this
    // list for the same reason the provider kinds are — a Core Service body to
    // project onto and a plane behind it — and they are listed apart from them
    // because they are about what a person is typing rather than about a
    // provider record.
    case api::CoreCommandKind::kRequestComposerCompletion:
    case api::CoreCommandKind::kCancelComposerCompletion:
      return true;
  }
  return false;
}

api::OperationEnvelopePtr CoreApiCommandFactory::NewCoreOperation(
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return api::OperationEnvelope::New(
      entropy_source_->NewOpaqueId("operation"), service_generation,
      task_revision, DeadlineFrom(now_monotonic_ms),
      entropy_source_->NewOpaqueId("idempotency"));
}

service::OperationEnvelopePtr CoreApiCommandFactory::ProjectOperation(
    const api::OperationEnvelope& operation) const {
  return service::OperationEnvelope::New(
      operation.operation_id, operation.service_generation,
      operation.task_revision, operation.deadline_monotonic_ms,
      operation.idempotency_key);
}

bool CoreApiCommandFactory::HasValidGeneratedBody(
    const api::CoreCommand& command) const {
  if (CommandBodyCount(command) != 1u || !IsSafeCommandKind(command.kind)) {
    return false;
  }
  switch (command.kind) {
    case api::CoreCommandKind::kStartTask:
      return !!command.start_task;
    case api::CoreCommandKind::kCancelTask:
      return !!command.cancel_task;
    case api::CoreCommandKind::kApproveAction:
      return !!command.approve_action;
    case api::CoreCommandKind::kRetryCore:
      return !!command.retry_core;
    case api::CoreCommandKind::kStartAuth:
      return !!command.start_auth;
    case api::CoreCommandKind::kRequestEmailLink:
      return !!command.request_email_link;
    case api::CoreCommandKind::kSignOut:
      return !!command.sign_out;
    case api::CoreCommandKind::kAuthCredentialResult:
      return !!command.auth_credential_result;
    case api::CoreCommandKind::kCorrectWorkspaceFact:
      return !!command.correct_workspace_fact;
    case api::CoreCommandKind::kExcludeWorkspaceSource:
      return !!command.exclude_workspace_source;
    case api::CoreCommandKind::kRequestWorkspaceExport:
      return !!command.request_workspace_export;
    case api::CoreCommandKind::kPermissionResult:
      return !!command.permission_result;
    case api::CoreCommandKind::kRequestAsset:
      return !!command.request_asset;
    case api::CoreCommandKind::kRemoveAsset:
      return !!command.remove_asset;
    case api::CoreCommandKind::kSetAssetPolicy:
      return !!command.set_asset_policy;
    case api::CoreCommandKind::kSaveProviderCredential:
      return !!command.save_provider_credential;
    case api::CoreCommandKind::kForgetProviderCredential:
      return !!command.forget_provider_credential;
    case api::CoreCommandKind::kSetProviderCredentialState:
      return !!command.set_provider_credential_state;
    case api::CoreCommandKind::kProbeProviderKey:
      return !!command.probe_provider_key;
    case api::CoreCommandKind::kStartProviderAuth:
      return !!command.start_provider_auth;
    case api::CoreCommandKind::kSaveCustomProvider:
      return !!command.save_custom_provider;
    case api::CoreCommandKind::kRemoveCustomProvider:
      return !!command.remove_custom_provider;
    case api::CoreCommandKind::kCompleteHandover:
      return !!command.complete_handover;
    case api::CoreCommandKind::kSupplyUserInput:
      return !!command.supply_user_input;
    case api::CoreCommandKind::kFollowUp:
      return !!command.follow_up;
    case api::CoreCommandKind::kSetProviderModelPreference:
      return !!command.set_provider_model_preference;
    case api::CoreCommandKind::kProbeCustomEndpoint:
      return !!command.probe_custom_endpoint;
    case api::CoreCommandKind::kRequestComposerCompletion:
      return !!command.request_composer_completion;
    case api::CoreCommandKind::kCancelComposerCompletion:
      return !!command.cancel_composer_completion;
    case api::CoreCommandKind::kPauseTask:
      return !!command.pause_task;
    case api::CoreCommandKind::kResumeTask:
      return !!command.resume_task;
    case api::CoreCommandKind::kTakeOver:
      return !!command.take_over;
    case api::CoreCommandKind::kSetAssistantConfiguration:
      return !!command.set_assistant_configuration;
    case api::CoreCommandKind::kSaveWorkspace:
      return !!command.save_workspace;
    case api::CoreCommandKind::kRenameWorkspace:
      return !!command.rename_workspace;
    case api::CoreCommandKind::kDeleteWorkspace:
      return !!command.delete_workspace;
    case api::CoreCommandKind::kDiscardWorkspace:
      return !!command.discard_workspace;
    case api::CoreCommandKind::kSearchLibrary:
      return !!command.search_library;
    case api::CoreCommandKind::kSaveLibraryFact:
      return !!command.save_library_fact;
    case api::CoreCommandKind::kRemoveLibraryEntry:
      return !!command.remove_library_entry;
    case api::CoreCommandKind::kRequestLibraryExport:
      return !!command.request_library_export;
    case api::CoreCommandKind::kSearchMemory:
      return !!command.search_memory;
    case api::CoreCommandKind::kUpsertMemory:
      return !!command.upsert_memory;
    case api::CoreCommandKind::kDeleteMemory:
      return !!command.delete_memory;
    case api::CoreCommandKind::kAcceptTaskArtifact:
      return !!command.accept_task_artifact;
    case api::CoreCommandKind::kRequestTaskArtifactExport:
      return !!command.request_task_artifact_export;
    case api::CoreCommandKind::kMutateSiteSkill:
      return !!command.mutate_site_skill;
    case api::CoreCommandKind::kCancelProviderAuth:
      return !!command.cancel_provider_auth;
    case api::CoreCommandKind::kStartLibraryRefresh:
      return !!command.start_library_refresh;
  }
  return false;
}

}  // namespace taffy
