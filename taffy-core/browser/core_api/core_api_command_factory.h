// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_H_
#define TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_H_

#include <stddef.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/browser/core_api/core_api_projected_command.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Constructs and validates the generated Core API command before projecting
// it to Core Service. Only generated closed enums cross this boundary.
class CoreApiCommandFactory final {
 public:
  CoreApiCommandFactory(std::string browser_profile_id,
                        std::unique_ptr<CoreApiEntropySource> entropy_source);
  CoreApiCommandFactory(const CoreApiCommandFactory&) = delete;
  CoreApiCommandFactory& operator=(const CoreApiCommandFactory&) = delete;
  ~CoreApiCommandFactory();

  // `tool_allowlist` is the allowlist this task is being asked for. The
  // factory carries it and checks that it is well formed; it holds no list of
  // its own, because which tool names exist is decided in the sandboxed core
  // and a second copy here would be a vocabulary that drifts from the first
  // with no gate comparing them (decision 0057).
  std::optional<ProjectedCoreCommand> BuildStartTask(
      std::string goal,
      core_api::mojom::TaskTemplateId template_id,
      std::optional<std::string> workspace_id,
      core_api::mojom::TaskConsentPreviewPtr consent_intent,
      core_service::mojom::TaskConsentPreviewPtr resolved_consent,
      std::vector<std::string> tool_allowlist,
      std::string browser_session_id,
      uint64_t service_generation,
      uint64_t now_monotonic_ms,
      std::optional<std::string> skill_offer_id = std::nullopt,
      std::optional<std::string> skill_version_id = std::nullopt);
  std::optional<ProjectedCoreCommand> BuildStartLibraryRefresh(
      std::string preview_id,
      std::string collection_id,
      uint64_t expected_library_revision,
      uint64_t expected_workspace_revision,
      uint32_t source_count,
      std::string browser_session_id,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildCancelTask(
      std::string task_id,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildPauseTask(
      std::string task_id,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildResumeTask(
      std::string task_id,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildTakeOver(std::string task_id,
                                                    uint64_t task_revision,
                                                    uint64_t service_generation,
                                                    uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildCompleteHandover(
      std::string task_id,
      std::string handover_id,
      std::string lease_before,
      std::string resumed_with,
      uint32_t person_input,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSupplyUserInput(
      std::string task_id,
      std::string answer,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildFollowUp(
      std::string task_id,
      std::string question,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildAcceptTaskArtifact(
      std::string task_id,
      std::string artifact_id,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRequestTaskArtifactExport(
      std::string request_id,
      std::string task_id,
      std::string artifact_id,
      core_api::mojom::TaskArtifactKind kind,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  // How many values the person supplied for one field-value request, and
  // nothing else about any of them (decision 0088).
  //
  // The only method here that answers with a bare Core Service command. There
  // is no Core API command kind for it on purpose: the person types into a
  // control the browser owns, the browser mints each answer into its own
  // vault, and a method on the surface interface that carried the bytes would
  // be a route from a keyboard to the plane that is projected and journalled.
  // core_api_command_factory_field_values.cc holds the argument in full.
  //
  // Null when the identities are not identifiers or the revision is zero. No
  // operation identity is minted and no idempotency key is spent on a refusal.
  core_service::mojom::CoreServiceCommandPtr BuildSupplyFieldValues(
      std::string task_id,
      std::string request_id,
      uint32_t supplied,
      core_service::mojom::FieldValueAskOutcome outcome,
      std::vector<std::string> field_node_ids,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildApproveAction(
      std::string task_id,
      std::string action_id,
      std::string proposal_digest,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms,
      uint64_t now_utc_ms,
      std::string browser_session_id);
  std::optional<ProjectedCoreCommand> BuildPermissionResult(
      std::string task_id,
      std::string request_id,
      core_api::mojom::PlatformPermission permission,
      core_api::mojom::PermissionDecision decision,
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildStartAuth(
      core_api::mojom::AuthProvider provider,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRequestEmailLink(
      std::string email,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSignOut(
      std::optional<std::string> account_id,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildAuthCredentialResult(
      std::string flow_id,
      core_api::mojom::AuthProvider provider,
      core_api::mojom::AuthCredentialStatus status,
      std::optional<std::string> credential_handle,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildCorrectWorkspaceFact(
      std::string workspace_id,
      uint64_t expected_revision,
      std::string fact_id,
      std::string value,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildExcludeWorkspaceSource(
      std::string workspace_id,
      uint64_t expected_revision,
      std::string source_id,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRequestWorkspaceExport(
      std::string request_id,
      std::string workspace_id,
      uint64_t expected_revision,
      core_api::mojom::WorkspaceExportFormat format,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSaveWorkspace(
      std::string workspace_id,
      uint64_t expected_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRenameWorkspace(
      std::string workspace_id,
      uint64_t expected_revision,
      std::string display_name,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildDeleteWorkspace(
      std::string workspace_id,
      uint64_t expected_revision,
      std::string confirmation_token,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildDiscardWorkspace(
      std::string workspace_id,
      uint64_t expected_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSearchLibrary(
      std::string request_id,
      std::string query,
      uint32_t limit,
      uint64_t requested_at_epoch_ms,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSaveLibraryFact(
      std::string workspace_id,
      uint64_t expected_workspace_revision,
      std::string fact_id,
      uint64_t expected_library_revision,
      uint64_t expected_entry_revision,
      uint64_t approved_at_epoch_ms,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRemoveLibraryEntry(
      std::string entry_id,
      uint64_t expected_library_revision,
      uint64_t expected_entry_revision,
      uint64_t removed_at_epoch_ms,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRequestLibraryExport(
      std::string request_id,
      uint64_t expected_library_revision,
      std::optional<std::string> collection_id,
      core_api::mojom::WorkspaceExportFormat format,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSearchMemory(
      std::string request_id,
      std::string query,
      uint32_t limit,
      uint64_t requested_at_epoch_ms,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildUpsertMemory(
      std::optional<std::string> memory_id,
      std::string statement,
      core_api::mojom::MemoryScopeKind scope_kind,
      core_api::mojom::MemoryWorkspaceViewPtr scope_workspace,
      core_api::mojom::MemorySensitivity sensitivity,
      uint64_t expected_memory_revision,
      uint64_t expected_record_revision,
      uint64_t expires_at_epoch_ms,
      uint64_t approved_at_epoch_ms,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildDeleteMemory(
      std::string memory_id,
      uint64_t expected_memory_revision,
      uint64_t expected_record_revision,
      uint64_t deleted_at_epoch_ms,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRequestAsset(
      std::string asset_id,
      std::string asset_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildRemoveAsset(
      std::string asset_id,
      std::string asset_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSetAssetPolicy(
      core_api::mojom::AssetNetworkCostView network_cost,
      bool metered_permitted,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  std::optional<ProjectedCoreCommand> BuildSetAssistantConfiguration(
      uint64_t expected_revision,
      std::vector<core_api::mojom::AssistantAbilityView> disabled_abilities,
      core_api::mojom::PersonalityPresetView preset,
      uint32_t pace,
      uint32_t length,
      uint32_t check_in,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  // "Teach this site" and subsequent lifecycle changes share one deep
  // bounded body. Teaching carries only browser-observed semantic ordinals,
  // references and compiled-in tool names; it cannot carry executable code.
  std::optional<ProjectedCoreCommand> BuildMutateSiteSkill(
      core_api::mojom::SiteSkillMutationKind kind,
      std::string skill_id,
      uint32_t expected_version,
      std::string origin,
      std::vector<core_api::mojom::SiteSkillObservedClausePtr> clauses,
      std::vector<core_api::mojom::SiteSkillObservedStepPtr> steps,
      uint32_t admitted,
      bool enabled,
      uint64_t recorded_at_epoch_ms,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);

  // The provider seam. Each of these validates every field of the request
  // before it constructs anything, so a refused request has produced no
  // operation identity, spent no idempotency key and told the core nothing.
  ProviderCommandResult BuildSaveProviderCredential(
      std::string provider_id,
      core_api::mojom::ProviderAuthMethodView method,
      std::string credential_handle,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  ProviderCommandResult BuildForgetProviderCredential(
      std::string provider_id,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  ProviderCommandResult BuildSetProviderCredentialState(
      std::string provider_id,
      core_api::mojom::ProviderCredentialStateView state,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  ProviderCommandResult BuildStartProviderAuth(std::string provider_id,
                                               uint64_t service_generation,
                                               uint64_t now_monotonic_ms);
  ProviderCommandResult BuildCancelProviderAuth(std::string flow_id,
                                                uint64_t service_generation,
                                                uint64_t now_monotonic_ms);
  // The key probe (decision 0083). The handle is opaque here as everywhere:
  // durable when it names the provider itself, one-shot transient otherwise,
  // and this layer cannot tell them apart — the model broker resolves the
  // difference at dispatch.
  ProviderCommandResult BuildProbeProviderKey(std::string provider_id,
                                              std::string credential_handle,
                                              uint64_t service_generation,
                                              uint64_t now_monotonic_ms);
  // `models` is the roster a person declared on their own endpoint, and
  // `detected_server` what a probe recognised the endpoint as. Both are
  // whole-state: a save carrying an empty roster is a provider with no
  // declared models, not a save that leaves the previous roster standing.
  ProviderCommandResult BuildSaveCustomProvider(
      std::string provider_id,
      std::string display_name,
      std::string endpoint,
      core_api::mojom::ProviderWireApiView wire_api,
      std::optional<std::string> credential_handle,
      std::vector<core_api::mojom::CustomModelSpecViewPtr> models,
      std::optional<core_api::mojom::ServerKindView> detected_server,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  // The endpoint question asked before a provider exists to ask it about. The
  // endpoint rule it applies is the save's, so a probe cannot report reachable
  // an endpoint the save would then refuse.
  //
  // `provider_id` is the **draft** identity the verdict is filed under
  // (decision 0096 section 5), supplied by the surface and reused by the save
  // that follows, so that probing an address and then saving it produces one
  // roster row rather than two. It is held to the same identity rule as a
  // saved provider's, because it is the identity the save will create — a
  // probe that accepted a spelling the save refuses would file a verdict under
  // a row that can never exist. Nothing is stored under it here: the probe
  // writes no register row, which is what separates it from the save.
  ProviderCommandResult BuildProbeCustomEndpoint(
      std::string provider_id,
      std::string endpoint,
      core_api::mojom::ProviderWireApiView wire_api,
      std::optional<std::string> credential_handle,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  // One provider's standing model choice (decision 0093). Both fields absent
  // is the request to clear it, and is accepted.
  ProviderCommandResult BuildSetProviderModelPreference(
      std::string provider_id,
      std::optional<std::string> model_id,
      std::optional<core_api::mojom::ThinkingLevelView> thinking_level,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  ProviderCommandResult BuildRemoveCustomProvider(std::string provider_id,
                                                  uint64_t service_generation,
                                                  uint64_t now_monotonic_ms);

  // The composer's inline completion request. Not a provider request, so it
  // answers with a bare projected command; core_api_command_factory_composer.cc
  // holds the argument.
  std::optional<ProjectedCoreCommand> BuildRequestComposerCompletion(
      std::string request_id,
      std::string prefix,
      std::optional<std::string> suffix,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);

  // The withdrawal of that request (decision 0097 section 3). It names the
  // request to stop and carries nothing else — a surface that has stopped
  // wanting an answer has nothing further to say about it, and a body that
  // repeated the prefix would be sending the person's text a second time to
  // cancel having sent it once.
  //
  // It is a command in its own right rather than a newer request with an empty
  // prefix, because those are different facts: a newer request supersedes an
  // older one and asks for something, and this asks for nothing.
  std::optional<ProjectedCoreCommand> BuildCancelComposerCompletion(
      std::string request_id,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);

  core_api::mojom::CoreCommandPtr BuildRetryCore(uint64_t service_generation,
                                                 uint64_t now_monotonic_ms);

  // Exhaustive over the generated Core API enum. False means the browser lacks
  // the authority correlation or Core Service body required to project it.
  static bool IsSafeCommandKind(core_api::mojom::CoreCommandKind kind);

 private:
  core_api::mojom::OperationEnvelopePtr NewCoreOperation(
      uint64_t task_revision,
      uint64_t service_generation,
      uint64_t now_monotonic_ms);
  core_service::mojom::OperationEnvelopePtr ProjectOperation(
      const core_api::mojom::OperationEnvelope& operation) const;
  bool HasValidGeneratedBody(const core_api::mojom::CoreCommand& command) const;

  const std::string browser_profile_id_;
  const std::unique_ptr<CoreApiEntropySource> entropy_source_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_H_
