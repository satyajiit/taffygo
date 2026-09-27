// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_PROFILE_CORE_API_FACADE_H_
#define TAFFY_BROWSER_CORE_API_PROFILE_CORE_API_FACADE_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/assets/asset_pack.h"
#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

namespace taffy {

// One profile-lifetime browser facade. Android supplies only bounded UI
// intent; operation identity, reducer revision, approval digest, task seed,
// and service generation are resolved in the browser process.
class ProfileCoreApiFacade final : public core_api::mojom::TaffyProfileCoreApi,
                                   public CoreServiceManager::Observer {
 public:
  explicit ProfileCoreApiFacade(CoreServiceManager* manager);
  ProfileCoreApiFacade(const ProfileCoreApiFacade&) = delete;
  ProfileCoreApiFacade& operator=(const ProfileCoreApiFacade&) = delete;
  ~ProfileCoreApiFacade() override;

 private:
  using SubmissionStatus = core_api::mojom::CoreApiSubmissionStatus;
  using SubmissionCallback = base::OnceCallback<void(SubmissionStatus)>;

  // core_api::mojom::TaffyProfileCoreApi:
  void Observe(mojo::PendingRemote<core_api::mojom::TaffyProfileCoreApiObserver>
                   observer) override;
  void StartTask(const std::string& goal,
                 core_api::mojom::TaskTemplateId template_id,
                 const std::optional<std::string>& workspace_id,
                 core_api::mojom::TaskConsentPreviewPtr consent_preview,
                 const std::optional<std::string>& skill_offer_id,
                 StartTaskCallback callback) override;
  void CancelTask(const std::string& task_id,
                  CancelTaskCallback callback) override;
  void PauseTask(const std::string& task_id,
                 PauseTaskCallback callback) override;
  void ResumeTask(const std::string& task_id,
                  ResumeTaskCallback callback) override;
  void TakeOver(const std::string& task_id, TakeOverCallback callback) override;
  void SetAssistantConfiguration(
      uint64_t expected_revision,
      const std::vector<core_api::mojom::AssistantAbilityView>&
          disabled_abilities,
      core_api::mojom::PersonalityPresetView preset,
      uint32_t pace,
      uint32_t length,
      uint32_t check_in,
      SetAssistantConfigurationCallback callback) override;
  void FindSavedFlows(const std::string& request_id, const std::string& goal,
                      FindSavedFlowsCallback callback) override;
  void GetSavedFlowReview(const std::string& request_id, const std::string& skill_id,
                          uint32_t expected_version, GetSavedFlowReviewCallback callback) override;
  void OpenSavedFlowStart(const std::string& request_id, const std::string& skill_id,
                          uint32_t expected_version, OpenSavedFlowStartCallback callback) override;
  void QuerySavedFlowReviews(const std::string& request_id,
      core_service::mojom::SavedFlowQueryKind kind, const std::string& goal,
      const std::string& skill_id, uint32_t expected_version,
      base::OnceCallback<void(core_api::mojom::SavedFlowQueryResultPtr)> callback);
  void MutateSiteSkill(
      core_api::mojom::SiteSkillMutationKind kind,
      const std::string& skill_id,
      uint32_t expected_version,
      const std::string& origin,
      std::vector<core_api::mojom::SiteSkillObservedClausePtr> clauses,
      std::vector<core_api::mojom::SiteSkillObservedStepPtr> steps,
      uint32_t admitted,
      bool enabled,
      MutateSiteSkillCallback callback) override;
  void ApproveAction(const std::string& task_id,
                     const std::string& action_id,
                     ApproveActionCallback callback) override;
  void RetryCore(uint64_t observed_generation,
                 RetryCoreCallback callback) override;
  void StartAuth(core_api::mojom::AuthProvider provider,
                 StartAuthCallback callback) override;
  void RequestEmailLink(const std::string& email,
                        RequestEmailLinkCallback callback) override;
  void SignOut(SignOutCallback callback) override;
  void DeliverCredentialResult(
      const std::string& flow_id,
      core_api::mojom::AuthProvider provider,
      core_api::mojom::AuthCredentialStatus credential_status,
      const std::optional<std::string>& credential_handle,
      DeliverCredentialResultCallback callback) override;
  void DeliverPermissionResult(
      const std::string& request_id,
      core_api::mojom::PlatformPermission permission,
      core_api::mojom::PermissionDecision decision,
      DeliverPermissionResultCallback callback) override;
  void CorrectWorkspaceFact(const std::string& workspace_id,
                            uint64_t expected_revision,
                            const std::string& fact_id,
                            const std::string& value,
                            CorrectWorkspaceFactCallback callback) override;
  void ExcludeWorkspaceSource(const std::string& workspace_id,
                              uint64_t expected_revision,
                              const std::string& source_id,
                              ExcludeWorkspaceSourceCallback callback) override;
  void RequestWorkspaceExport(const std::string& request_id,
                              const std::string& workspace_id,
                              uint64_t expected_revision,
                              core_api::mojom::WorkspaceExportFormat format,
                              RequestWorkspaceExportCallback callback) override;
  void AcceptTaskArtifact(const std::string& task_id,
                          const std::string& artifact_id,
                          AcceptTaskArtifactCallback callback) override;
  void RequestTaskArtifactExport(
      const std::string& request_id,
      const std::string& task_id,
      const std::string& artifact_id,
      core_api::mojom::TaskArtifactKind kind,
      RequestTaskArtifactExportCallback callback) override;
  void SaveWorkspace(const std::string& workspace_id,
                     uint64_t expected_revision,
                     SaveWorkspaceCallback callback) override;
  void RenameWorkspace(const std::string& workspace_id,
                       uint64_t expected_revision,
                       const std::string& display_name,
                       RenameWorkspaceCallback callback) override;
  void DeleteWorkspace(const std::string& workspace_id,
                       uint64_t expected_revision,
                       const std::string& confirmation_token,
                       DeleteWorkspaceCallback callback) override;
  void DiscardWorkspace(const std::string& workspace_id,
                        uint64_t expected_revision,
                        DiscardWorkspaceCallback callback) override;
  void SearchLibrary(const std::string& request_id,
                     const std::string& query,
                     uint32_t limit,
                     SearchLibraryCallback callback) override;
  void SaveLibraryFact(const std::string& workspace_id,
                       uint64_t expected_workspace_revision,
                       const std::string& fact_id,
                       uint64_t expected_library_revision,
                       uint64_t expected_entry_revision,
                       SaveLibraryFactCallback callback) override;
  void RemoveLibraryEntry(const std::string& entry_id,
                          uint64_t expected_library_revision,
                          uint64_t expected_entry_revision,
                          RemoveLibraryEntryCallback callback) override;
  void RequestLibraryExport(const std::string& request_id,
                            uint64_t expected_library_revision,
                            const std::optional<std::string>& collection_id,
                            core_api::mojom::WorkspaceExportFormat format,
                            RequestLibraryExportCallback callback) override;
  void StartLibraryRefresh(const std::string& preview_id,
                           const std::string& collection_id,
                           uint64_t expected_library_revision,
                           uint64_t expected_workspace_revision,
                           uint32_t source_count,
                           StartLibraryRefreshCallback callback) override;
  void SearchMemory(const std::string& request_id,
                    const std::string& query,
                    uint32_t limit,
                    SearchMemoryCallback callback) override;
  void UpsertMemory(const std::optional<std::string>& memory_id,
                    const std::string& statement,
                    core_api::mojom::MemoryScopeKind scope_kind,
                    core_api::mojom::MemoryWorkspaceViewPtr scope_workspace,
                    core_api::mojom::MemorySensitivity sensitivity,
                    uint64_t expected_memory_revision,
                    uint64_t expected_record_revision,
                    uint64_t expires_at_epoch_ms,
                    UpsertMemoryCallback callback) override;
  void DeleteMemory(const std::string& memory_id,
                    uint64_t expected_memory_revision,
                    uint64_t expected_record_revision,
                    DeleteMemoryCallback callback) override;
  void UpsertSavedDetail(uint64_t expected_revision,
                         const std::optional<std::string>& detail_id,
                         const std::string& given_name,
                         const std::string& family_name,
                         const std::string& email,
                         const std::string& phone,
                         const std::string& address,
                         const std::string& postcode,
                         const std::string& country,
                         UpsertSavedDetailCallback callback) override;
  void DeleteSavedDetail(const std::string& detail_id,
                         uint64_t expected_revision,
                         DeleteSavedDetailCallback callback) override;
  void DeleteSavedSignIn(const std::string& sign_in_id,
                         uint64_t expected_revision,
                         DeleteSavedSignInCallback callback) override;
  void RequestAsset(const std::string& asset_id,
                    const std::string& asset_revision,
                    RequestAssetCallback callback) override;
  void RemoveAsset(const std::string& asset_id,
                   const std::string& asset_revision,
                   RemoveAssetCallback callback) override;
  void SetAssetPolicy(core_api::mojom::AssetNetworkCostView network_cost,
                      bool metered_permitted,
                      SetAssetPolicyCallback callback) override;
  void ReadPartMember(const std::string& asset_id,
                      const std::string& member_path,
                      ReadPartMemberCallback callback) override;
  // The provider seam, answered in profile_core_api_facade_provider.cc. Every
  // one of these returns a status and nothing else: no response field on this
  // interface can carry a credential, a fragment of one, or its length.
  void SaveProviderCredential(
      const std::string& provider_id,
      core_api::mojom::ProviderAuthMethodView auth_method,
      const std::string& credential_handle,
      SaveProviderCredentialCallback callback) override;
  void ForgetProviderCredential(
      const std::string& provider_id,
      ForgetProviderCredentialCallback callback) override;
  void SetProviderCredentialState(
      const std::string& provider_id,
      core_api::mojom::ProviderCredentialStateView state,
      SetProviderCredentialStateCallback callback) override;
  void ProbeProviderKey(const std::string& provider_id,
                        const std::string& credential_handle,
                        ProbeProviderKeyCallback callback) override;
  void StartProviderAuth(const std::string& provider_id,
                         StartProviderAuthCallback callback) override;
  void StartProviderAuthFlow(
      const std::string& provider_id,
      StartProviderAuthFlowCallback callback) override;
  void CancelProviderAuth(const std::string& flow_id,
                          CancelProviderAuthCallback callback) override;
  void SaveCustomProvider(
      const std::string& provider_id,
      const std::string& display_name,
      const std::string& endpoint,
      core_api::mojom::ProviderWireApiView wire_api,
      const std::optional<std::string>& credential_handle,
      std::vector<core_api::mojom::CustomModelSpecViewPtr> models,
      core_api::mojom::DetectedServerViewPtr detected_server,
      SaveCustomProviderCallback callback) override;
  void RemoveCustomProvider(const std::string& provider_id,
                            RemoveCustomProviderCallback callback) override;
  // The standing model choice (decision 0093). It touches the browser's own
  // preference file before it forwards anything; the argument is in
  // taffy/browser/model/provider_model_preference_store.h.
  void SetProviderModelPreference(
      const std::string& provider_id,
      const std::optional<std::string>& model_id,
      core_api::mojom::ThinkingPreferenceViewPtr thinking,
      SetProviderModelPreferenceCallback callback) override;
  void ProbeCustomEndpoint(const std::string& endpoint,
                           core_api::mojom::ProviderWireApiView wire_api,
                           const std::optional<std::string>& credential_handle,
                           const std::string& provider_id,
                           ProbeCustomEndpointCallback callback) override;
  // The composer seam, answered in profile_core_api_facade_composer.cc.
  void RequestComposerCompletion(
      const std::string& request_id,
      const std::string& prefix,
      const std::optional<std::string>& suffix,
      RequestComposerCompletionCallback callback) override;
  void CancelComposerCompletion(
      const std::string& request_id,
      CancelComposerCompletionCallback callback) override;
  void CompleteHandover(const std::string& task_id,
                        CompleteHandoverCallback callback) override;
  void SupplyUserInput(const std::string& task_id,
                       const std::string& answer,
                       SupplyUserInputCallback callback) override;
  void FollowUp(const std::string& task_id,
                const std::string& question,
                FollowUpCallback callback) override;

  // CoreServiceManager::Observer:
  void OnCoreAvailabilityChanged(
      CoreServiceManager::Availability availability) override;
  void OnCoreState(const core_service::mojom::CoreStateUpdate& state) override;
  void OnCorePermissionRequest(
      const std::string& request_id,
      core_api::mojom::PlatformPermission permission) override;
  void OnAssetProgress(const std::string& asset_id,
                       const std::string& asset_revision,
                       uint64_t written_bytes,
                       uint64_t total_bytes) override;
  void OnComposerCompletion(const std::string& request_id,
                            const std::optional<std::string>& text) override;
  void OnTaskAnswerDelta(const std::string& task_id,
                         const std::string& call_id,
                         uint32_t sequence,
                         const std::optional<std::string>& text,
                         bool terminal,
                         bool complete) override;
  bool OnTaskArtifactExport(const std::string& task_id,
                            const std::string& artifact_id,
                            core_service::mojom::TaskArtifactKind kind,
                            const std::vector<uint8_t>& content) override;

  void OnPreparedStartTask(
      std::string goal,
      core_api::mojom::TaskTemplateId template_id,
      std::optional<std::string> workspace_id,
      core_api::mojom::TaskConsentPreviewPtr consent_preview,
      std::optional<std::string> skill_offer_id,
      StartTaskCallback callback,
      bool ready);
  void SubmitProjected(std::optional<ProjectedCoreCommand> projected,
                       SubmissionCallback callback);
  // One exit for every provider handler, refused or submitted, so a handler
  // can neither answer twice nor fail to answer.
  void SubmitProviderResult(ProviderCommandResult result,
                            SubmissionCallback callback);
  void StartProviderAuthImpl(const std::string& provider_id,
                             StartProviderAuthFlowCallback callback);
  void OnPartMemberRead(ReadPartMemberCallback callback,
                        PackMemberResult result);
  void OnSubmitted(SubmissionCallback callback,
                   core_service::mojom::AdmissionPtr admission);
  static SubmissionStatus ProjectAdmission(
      core_service::mojom::AdmissionStatus status);
  void OnTaskArtifactExportSubmitted(
      std::string request_id,
      RequestTaskArtifactExportCallback callback,
      core_service::mojom::AdmissionPtr admission);
  CoreApiCommandFactory NewFactory() const;
  SubmissionStatus UnavailableOrStaleRevision() const;

  raw_ptr<CoreServiceManager> manager_;
  uint64_t saved_flow_query_ticket_ = 0u;
  mojo::Remote<core_api::mojom::TaffyProfileCoreApiObserver> observer_;
  bool observing_ = false;

  struct PendingTaskArtifactExport {
    std::string task_id;
    std::string artifact_id;
    core_api::mojom::TaskArtifactKind kind;
    base::TimeTicks expires_at;
  };
  base::flat_map<std::string, PendingTaskArtifactExport>
      pending_task_artifact_exports_;

  base::WeakPtrFactory<ProfileCoreApiFacade> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_PROFILE_CORE_API_FACADE_H_
