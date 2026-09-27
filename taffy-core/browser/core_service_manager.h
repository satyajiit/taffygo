// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_MANAGER_H_
#define TAFFY_BROWSER_CORE_SERVICE_MANAGER_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/observed_service_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/account/profile_platform_adapter.mojom-forward.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_service_manager_lifetime_state.h"
#include "taffy/browser/core_service_manager_types.h"
#include "taffy/browser/core_task_browser_actions.h"
#include "taffy/browser/field_values/field_value_surface.mojom-forward.h"
#include "taffy/browser/task_download_manager_adapter.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/components/security/browser/value_reference_vault.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

class GURL;
class PrefService;

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace taffy {

class CoreStorageBroker;
struct TaskPolicyDocumentContext;
class CorePageObservationBroker;
class TaskJournalSink;
class ProfileToolArtifactBroker;
class ProfileAccountBroker;
class ProfileProviderAuthBroker;
class ProfileEntitlementCache;
class ProviderAccessResolver;
class ProfileAssetPlane;
class ProfileModelBroker;
class FieldValueRequestCoordinator;
class CoreSavedDataActions;
class CoreBackupProtocol;
class ProfileBackupCoordinator;

namespace filtering {
class FilteringRulesetService;
}

// One lazy isolated core-service connection for one browser context
// (including an independent instance for every off-the-record one).
class CoreServiceManager final
    : public KeyedService,
      public CoreTaskBrowserActions,
      public core_service::mojom::CoreHost,
      public content::ObservedServiceRemote<
          core_service::mojom::TaffyCoreService>::Observer,
      private CoreServiceManagerLifetimeState {
 public:
  using Availability = CoreServiceAvailability;
  using Observer = CoreServiceObserver;

  // `profile_prefs` is the profile's own preference file, injected rather than
  // looked up for the reason the filtering service's is: this class is
  // constructed over a plain content::BrowserContext in its own suites, and a
  // lookup that assumed a Profile would crash them. It may be null there, and
  // every reader below treats null as "no stored choice".
  CoreServiceManager(content::BrowserContext* browser_context,
                     PrefService* profile_prefs,
                     CoreServiceManagerResources resources);
  CoreServiceManager(const CoreServiceManager&) = delete;
  CoreServiceManager& operator=(const CoreServiceManager&) = delete;
  ~CoreServiceManager() override;

  // Lazily launches the utility process and submits once initialization has
  // restored the browser-owned checkpoint and journal tail.
  void Submit(core_service::mojom::CoreServiceCommandPtr command,
              CoreServiceSubmitCallback callback);

  // Evaluates complete browser-owned lease, scope, and approval facts in the
  // canonical Rust policy engine. The returned grant is already registered in
  // this profile's browser ledger before it can be observed by the caller.
  void EvaluatePolicy(core_service::mojom::PolicyEvaluationRequestPtr request,
                      CoreServicePolicyEvaluationCallback callback);
  CoreBackupProtocol& backup_protocol() { return *backup_protocol_; }

  core_api::mojom::PageInspectorDocumentsViewPtr GetPageInspectorDocuments(
      content::WebContents* web_contents) const;
  void ObservePageForInspector(content::WebContents* web_contents,
                               CoreServicePageInspectorCallback callback);
  void ExportPageSnapshot(content::WebContents* web_contents,
                          const std::string& request_id,
                          const std::string& document_id,
                          core_api::mojom::PageSnapshotExportFormat format,
                          CoreServicePageExportCallback callback);
  bool CancelPageSnapshotExport(const std::string& request_id);
  CoreSiteSkillOfferBroker& site_skill_offers() { return site_skill_offers_; }

  // Local cancellation claims any not-yet-admitted request before the
  // best-effort service message is sent.
  void Cancel(core_service::mojom::OperationEnvelopePtr operation);

  // The explicit action required after the restart circuit opens.
  void RetryExplicitly();

  // Starts the lazy profile service before a UI intent has a service command.
  // This is needed for StartTask because the browser profile identity is
  // learned from the storage-owned bootstrap, never supplied by Android.
  void PrepareForCoreApi(base::OnceCallback<void(bool)> callback);

  // Deliberate idle teardown is not a crash and does not consume a restart.
  void TearDownForIdle();

  // How long the profile must stay quiescent before its utility process is
  // released. Quiescence transitions arm or stop one deadline; there is no
  // periodic browser wake-up. Releasing costs a process spawn and a journal
  // replay on the next command, but loses no work: the service is relaunched
  // lazily and recovers from its own journal, which is the property
  // //taffy/test/recovery already proves.
  static constexpr base::TimeDelta kIdleTeardownDelay = base::Seconds(30);

  bool is_quiescent_for_testing() const { return IsQuiescent(); }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  uint64_t service_generation() const { return service_generation_; }

  // The profile's delivery plane, for the one caller that reads bytes it
  // already owns rather than submitting an intent. Never null: the constructor
  // CHECKs it, because a profile with no plane could not answer what is on its
  // own disk.
  ProfileAssetPlane& asset_plane() { return *asset_plane_; }

  // The profile's filtering plane (decision 0076). Never null for the same
  // reason the asset plane is not: a profile with no filtering service could
  // not answer its own posture.
  filtering::FilteringRulesetService* filtering_service();
  const std::string& browser_profile_id() const { return browser_profile_id_; }
  // The profile's preference file, for the one browser-owned choice the core
  // is told rather than asked about (decision 0093). Null on a profile that
  // has none, which every reader treats as "no stored choice".
  PrefService* profile_prefs() { return profile_prefs_; }
  ActorLeaseRegistry* actor_leases() { return &actor_leases_; }
  CapabilityLedger* capabilities() { return &capabilities_; }
  // The profile's only holder of bytes destined for a form field (decision
  // 0063). It sits beside the two authority registries because it shares
  // their domain exactly: one profile generation, one task, dropped with
  // either.
  ValueReferenceVault* value_references();

  // The narrow profile-storage view used by each tab's action dispatcher.
  TaskJournalSink* task_journal_sink();
  void QuerySavedFlows(core_service::mojom::SavedFlowQueryKind kind,
                       const std::string& goal, const std::string& skill_id,
                       uint32_t expected_version,
                       base::OnceCallback<void(core_service::mojom::SavedFlowQueryResultPtr)> callback);
  Availability availability() const { return availability_; }
  size_t pending_admission_count_for_testing() const;
  uint64_t late_reply_count_for_testing() const { return late_reply_count_; }
  // Counts a reply or an effect that arrived for a generation, session or
  // effect this manager no longer holds, and says so once in the log. Thirty
  // sites used to increment the counter bare, and the counter was read only
  // by tests — so a start whose reply came back late left no trace at all.
  void NoteLateReply(const char* where);
  std::optional<uint64_t> FindTaskRevision(const std::string& task_id) const;
  // Current accepted source membership for the task workspace; no ownership
  // or action authority is created by reading this display fact.
  bool IsTaskSourceTabForDisplay(const std::string& task_id,
                                 content::WebContents* web_contents) const;
  // Manual file opening only: exact current-session attribution and a safe
  // completed profile download for a currently published terminal task.
  bool CanOpenTaskDownloadForPerson(const std::string& task_id,
                                    const std::string& download_id) const;
  std::optional<TaskControlLookup> FindTaskControl(
      const std::string& task_id,
      core_service::mojom::TaskControlKind kind) const;
  std::optional<PendingApprovalLookup> FindPendingApproval(
      const std::string& task_id,
      const std::string& action_id) const;
  std::optional<PendingPermissionLookup> FindPendingPermission(
      const std::string& request_id) const;
  std::optional<TerminalTaskLookup> FindTerminalTask(
      const std::string& task_id) const;

  const std::string& browser_session_id() const { return browser_session_id_; }

  // Closes the open handover window for `task_id` and returns the facts a
  // CompleteHandover command needs. The Core API never sees lease identities
  // or the input count; the facade copies them into the Core Service body.
  std::optional<HandoverCompletionFacts> CloseHandoverForCompletion(
      const std::string& task_id);

  // Tells the core how many values the person supplied for one field-value
  // request, and nothing else about any of them (decision 0088). Public
  // because the field-value coordinator is what calls it, after every
  // successful mint has already happened: the count is a report of what the
  // vault holds, never a promise about what it is about to hold.
  void SubmitSuppliedFieldValues(
      const std::string& task_id,
      const std::string& request_id,
      uint32_t supplied,
      core_service::mojom::FieldValueAskOutcome outcome,
      const std::vector<std::string>& field_node_ids);

  // Hands the browser-owned field-value surface to one platform client. The
  // pipe carries a person's bytes only into this browser process; it is not a
  // Core API or Core Service pipe (decisions 0063 and 0088).
  void BindFieldValueSurface(
      mojo::PendingReceiver<
          browser::field_values::mojom::TaffyFieldValueSurface> receiver);

  void BindPlatformAdapter(
      mojo::PendingRemote<browser::account::mojom::TaffyProfilePlatformAdapter>
          adapter);
  bool DeliverAuthCallback(std::string raw_uri);

  // Starts the vendor sign-in the core just admitted (decision 0081). The
  // facade calls this on an accepted START_PROVIDER_AUTH admission, with the
  // browser-minted identities the command factory stamped.
  void StartProviderAuthFlow(const std::string& provider_id,
                             const std::string& flow_id,
                             const std::string& redirect_binding_id);

  // Best-effort vendor-side revocation on sign-out (decision 0081). Android
  // resolves the sealed record before deleting it and hands the rotation
  // credential here; the forget proceeds whatever the vendor answers.
  void RevokeProviderCredential(const std::string& provider_id,
                                std::string token);

  // Offers one navigation to the running sign-ins (decision 0095 section 2).
  // True means a flow was waiting for exactly this address, the code and
  // state have been taken from it, and the navigation must be cancelled so
  // that nothing connects and no page ever sees the code.
  bool ClaimProviderRedirectNavigation(const GURL& url);
  bool HasInterceptableProviderRedirect() const;

  // The manual-code fallback's browser half. A person who was shown a code by
  // the vendor enters it on the product's own chrome, and it arrives here.
  // True means a live flow accepted it.
  bool SubmitProviderAuthCode(const std::string& flow_id, std::string code);

  // Ends one running sign-in because the person said so. True means there was
  // one to end; the core's pending marker clears through the same terminal a
  // dismissed authorization files.
  bool CancelProviderAuthFlow(const std::string& flow_id);

  // KeyedService:
  void Shutdown() override;

 private:
  friend class CoreServiceManagerIdleTestPeer;
  friend class CoreServiceManagerPageExportTestPeer;
  friend class CoreServiceManagerStorageTestPeer;
  friend class CoreServiceManagerTaskEffectTestPeer;
  friend class CoreServiceManagerCallModelTestPeer;
  friend class CoreBackupProtocol;
  friend class CoreDeferredTaskSurfaceOwner;
  friend class CoreSavedDataActions;
  friend class FieldValueRequestCoordinator;
  friend class ProfileBackupCoordinator;

  void EnsureStarted();
  void LoadBootstrapAndLaunch();
  void LoadCommittedBootstrap(uint64_t generation);
  void FailBootstrapLaunch(uint64_t generation);
  // Continues `OnBootstrapLoaded` once the asset store has been walked. The
  // scan is the only part of a bootstrap that touches a filesystem this class
  // owns, so it is the only part that has to wait for a reply.
  void OnAssetsScanned(uint64_t generation,
                       core_service::mojom::CoreBootstrapPtr bootstrap,
                       std::vector<core_service::mojom::AssetOnDiskPtr> assets);
  void LaunchWithBootstrap(uint64_t generation,
                           core_service::mojom::CoreBootstrapPtr bootstrap);
  void OnBootstrapLoaded(uint64_t generation,
                         core_service::mojom::CoreBootstrapPtr bootstrap);
  void OnInitialized(uint64_t generation,
                     core_service::mojom::CoreBootstrapResultPtr result);
  void InitializeSavedDataBroker();
  void OnSavedDataSnapshot(
      core_service::mojom::ReplaceSavedDataSnapshotCommandPtr snapshot);
  void ReplaySavedDataSnapshot();
  void DispatchQueuedCommands();
  void OnAdmission(uint64_t generation,
                   std::string operation_id,
                   core_service::mojom::AdmissionPtr admission);
  void OnMojoDisconnect(uint64_t generation);
  void HandleDisconnect(bool unexpected);
  void AdvanceGeneration();
  void ResolvePendingAdmissionsUnavailable();
  void ResolvePendingPolicyUnavailable();
  void ResolvePendingPageInspectionsUnavailable();
  void ResolvePendingPageExportsUnavailable();
  void ResolvePendingTaskSurfacesUnavailable();
  void ResolvePendingCoreApiPreparations(bool ready);
  void OnAccountVaultInspected(
      uint64_t generation,
      bool has_pending_session_mutation,
      core_service::mojom::AccountSessionHandlePtr committed_session,
      bool inspection_succeeded,
      std::optional<std::string> vault_handle,
      uint64_t vault_rotation);
  void OnAccountVaultCleared(uint64_t generation, bool cleared);
  void OnAccountSqlReconciled(uint64_t generation, bool reconciled);
  void OnAccountFlowTerminated(
      uint64_t generation,
      core_service::mojom::AuthCallbackCommandPtr receipt);
  // The provider sign-in twin: wraps one browser flow terminal into a
  // PROVIDER_AUTH_CALLBACK command and answers whether the core accepted it
  // (core_service_manager_providerauth.cc).
  void OnProviderFlowTerminated(
      uint64_t callback_generation,
      core_service::mojom::ProviderAuthCallbackCommandPtr terminal,
      base::OnceCallback<void(bool)> submitted);
  // Fans one running transfer's byte count out to the observers.
  //
  // Four scalars rather than the plane's own struct, so this header keeps its
  // forward declaration: the manager's callers have no business seeing what a
  // transfer looks like from the inside.
  void OnAssetProgress(const std::string& asset_id,
                       const std::string& asset_revision,
                       uint64_t written_bytes,
                       uint64_t total_bytes);
  // The link just changed, or the core just became ready. Tells the plane
  // what the connection costs so a required artifact can start.
  void OnAssetNetworkCost(core_service::mojom::AssetNetworkCost cost);
  void ReportAssetNetwork();
  // The managed entitlement's browser legs (decision 0082). The browser
  // pokes and mints; every decision is the core's, and the token the mint
  // produces stays in this process — what the core receives is the summary.
  void PokeEntitlementRefresh(
      core_service::mojom::EntitlementFetchReason reason);
  void OnEntitlementRefreshPlanned(
      uint64_t generation,
      core_service::mojom::EffectEnvelopePtr effect);
  void OnEntitlementMinted(
      uint64_t generation,
      core_service::mojom::EffectEnvelopePtr effect,
      core_service::mojom::EntitlementSummaryResultPtr summary);
  void OnEntitlementDeliveryAcknowledged(uint64_t generation, bool installed);
  // The same cadence-bounding role as the catalog's; the core's own gates
  // decide, per reason.
  static constexpr base::TimeDelta kEntitlementPokeInterval = base::Hours(6);
  core_service::mojom::CoreServiceCommandPtr MakeSetAssetDeliveryPolicy(
      core_service::mojom::AssetNetworkCost cost) const;
  void ValidateAccountTokenResponse(
      core_service::mojom::AccountTokenValidationRequestPtr request,
      base::OnceCallback<
          void(core_service::mojom::AccountTokenValidationResultPtr)> callback);
  core_service::mojom::TaskEffectCompletionStatus ResolveTaskSurfaceStatus(
      const core_service::mojom::TaskEffectBinding& binding,
      uint64_t state_sequence);
  void QueueTaskSurface(core_service::mojom::TaskEffectBindingPtr binding,
                        ExecuteTaskEffectCallback callback);
  // Hands one open field-value ask to this profile's coordinator. Declared
  // here and defined in core_service_manager_field_values.cc so the surface
  // arm can reach it without naming the coordinator's own header, which lives
  // one target further out than the arm does.
  void OpenFieldValueRequest(
      const std::string& request_id,
      const std::string& task_id,
      const std::string& tab_id,
      const std::string& node_id,
      const std::vector<std::string>& companion_node_ids);
  void OnFieldValueRequestClosed(const std::string& request_id);
  void CompleteRegisteredTaskSettlements();
  void CompleteTaskSettlementAfterEffects(
      core_service::mojom::TaskSettlementBindingPtr binding);
  void RevokeRegisteredTerminalTasks();
  void QueuePendingPageInspectorPolicies();
  void BeginPageInspectorPolicyEvaluation(
      content::WebContents* web_contents,
      CoreServicePageInspectorCallback callback);
  // `refusal`, when given, receives the code a task-context scope was refused
  // under, so the caller can settle the proposal with it instead of reporting
  // one unreadable-record status for every shape.
  bool ValidatePolicyRequestForCurrentGeneration(
      const core_service::mojom::PolicyEvaluationRequest& request,
      std::optional<core_service::mojom::TaskActionResultCode>* refusal =
          nullptr) const;
  const core_service::mojom::PolicyEvaluationRequest*
  FindPendingPolicyRequestForGrant(
      const core_service::mojom::MintedCapabilityGrant& grant) const;
  void OnPolicyEvaluated(uint64_t generation,
                         std::string operation_id,
                         core_service::mojom::PolicyEvaluationResultPtr result);
  void OnDirectPolicyEvaluated(
      CoreDirectObservationRequest request,
      core_service::mojom::PolicyEvaluationResultPtr result);
  void OnDirectObservationCompleted(
      CoreDirectObservationRequest request,
      std::string effect_id,
      core_service::mojom::EffectResultPtr result);
  void BeginPageExportPolicyEvaluation(const std::string& operation_id);
  void OnPageExportCorePrepared(std::string operation_id, bool ready);
  void OnPageExportPolicyEvaluated(
      std::string operation_id,
      ActorLeaseId lease_id,
      core_service::mojom::PolicyEvaluationResultPtr result);
  void OnPageExportObservationCompleted(
      std::string operation_id,
      ActorLeaseId lease_id,
      core_service::mojom::EffectResultPtr result);
  void OnPageSnapshotExported(
      std::string operation_id,
      core_service::mojom::PageSnapshotExportResultPtr result);
  void FinishPageExport(const std::string& operation_id,
                        core_api::mojom::PageSnapshotExportResultPtr result);
  void SetAvailability(Availability availability);

  // The isolated core is single-sequenced and owns no threads, timers, or
  // asynchronous work of its own: it acts only when a message arrives, and it
  // is given no clock. Quiescence is therefore a fact the browser can settle
  // by itself, without asking the service, and this predicate is the whole of
  // it — nothing watching, and no message outstanding in either direction.
  bool IsQuiescent() const;
  void RefreshIdleTeardown();
  void OnIdleTeardownDeadline();
  void OnLiveEffectCompleted(uint64_t generation,
                             std::string effect_id,
                             core_service::mojom::EffectResultPtr result);
  core_service::mojom::AdmissionPtr MakeAdmission(
      std::string operation_id,
      core_service::mojom::AdmissionStatus status) const;
  bool ValidateCommand(
      const core_service::mojom::CoreServiceCommand& command) const;
  bool IsTaskBrowserAvailable() const override;
  void OnTaskSourceWindowCountChanged() override;
  void OnTaskSourceWindowActivated() override;

  void OnEffectCompleted(uint64_t generation,
                         core_service::mojom::EffectResultPtr result);

  // Tells a fresh core generation everything this profile holds about
  // providers: each one the person defined themselves, then every credential
  // this browser has announced, then any credential whose recorded state is
  // not usable, then every standing model choice (decisions 0117 and 0093,
  // under decision 0080's rule that the browser file is the authority and the
  // core is told at each generation rather than asked to remember across one).
  // The core's provider plane holds none of it across a generation and nothing
  // else refills it, so without this a restart leaves every provider
  // unreachable while its row still reads connected.
  //
  // One command per row, built by the same factory the surface's own request
  // goes through, so there is exactly one construction path. The order is
  // load-bearing: a person's own provider carries its credential on its own
  // write and must exist before a credential command could name it; a
  // credential replayed as usable over a record this browser knows needs a
  // sign-in would answer a person with a failing request instead of the
  // sign-in they can act on; and a model choice names a provider, so it is
  // last.
  void ReplayProviderSetup();

  // The composer push, fanned out to whatever surface is attached. Answers
  // with whether one was: an undelivered completion is a fact the core has to
  // be able to tell from a delivered one.
  bool DeliverComposerCompletion(const std::string& request_id,
                                 const std::optional<std::string>& text);
  void DeliverModelStreamChunk(core_service::mojom::ModelStreamChunkPtr chunk,
                               CoreModelStreamChunkCallback callback);

  // core_service::mojom::CoreHost:
  void RegisterCapability(core_service::mojom::MintedCapabilityGrantPtr grant,
                          RegisterCapabilityCallback callback) override;
  void RegisterPendingApprovals(
      core_service::mojom::CoreStateBrowserBindingsPtr bindings,
      RegisterPendingApprovalsCallback callback) override;
  void EvaluateTaskPolicy(core_service::mojom::TaskPolicyEffectPtr effect,
                          EvaluateTaskPolicyCallback callback) override;
  void ExecuteTaskEffect(core_service::mojom::TaskEffectBindingPtr effect,
                         ExecuteTaskEffectCallback callback) override;
  void EmitEffect(core_service::mojom::EffectEnvelopePtr effect) override;
  void PublishTaskAnswerEvents(
      std::vector<core_service::mojom::TaskAnswerEventPtr> events,
      PublishTaskAnswerEventsCallback callback) override;
  void PublishState(core_service::mojom::CoreStateUpdatePtr update) override;

  // A clause that refused a proposal: the code the task records, and the
  // clause's name for the log line.
  using TaskPolicyDenial =
      std::pair<core_service::mojom::TaskActionResultCode, const char*>;
  std::optional<TaskPolicyDenial> DenyTaskFormPolicy(
      const core_service::mojom::TaskPolicyEffect& effect,
      const TaskPolicyDocumentContext* live,
      uint64_t now,
      uint64_t now_utc);
  void OnTaskPolicyEvaluated(
      ActorLeaseId lease_id,
      uint32_t expected_policy_version,
      bool answered_by_asking,
      EvaluateTaskPolicyCallback callback,
      core_service::mojom::PolicyEvaluationResultPtr result);
  void OnTaskObservationCompleted(
      core_service::mojom::TaskEffectBindingPtr binding,
      ExecuteTaskEffectCallback callback,
      core_service::mojom::EffectResultPtr result);
  void OnTaskModelCompleted(core_service::mojom::TaskEffectBindingPtr binding,
                            ExecuteTaskEffectCallback callback,
                            core_service::mojom::EffectResultPtr result);
  void ExecuteTaskToolJob(core_service::mojom::TaskEffectBindingPtr effect,
                          ExecuteTaskEffectCallback callback);
  void OnTaskToolJobPrepared(
      core_service::mojom::TaskEffectBindingPtr original,
      ExecuteTaskEffectCallback callback,
      core_service::mojom::TaskEffectBindingPtr prepared);
  void OnTaskToolJobCompleted(core_service::mojom::TaskEffectBindingPtr binding,
                              ExecuteTaskEffectCallback callback,
                              core_service::mojom::EffectResultPtr result);
  void OnTaskToolOutputRetained(
      core_service::mojom::TaskEffectBindingPtr binding,
      core_service::mojom::MediaProbeResultPtr media_probe,
      ExecuteTaskEffectCallback callback,
      core_service::mojom::TaskToolOutputReceiptPtr receipt);
  void ExecuteTaskDiscoveryBootstrap(
      core_service::mojom::TaskEffectBindingPtr effect,
      ExecuteTaskEffectCallback callback);
  void OnTaskDiscoveryTabPrepared(
      uint64_t generation,
      std::string browser_session_id,
      core_service::mojom::TaskEffectBindingPtr effect,
      ExecuteTaskEffectCallback callback,
      std::optional<std::string> tab_id);
  // A bootstrap that arrived before any window was active waits for the
  // first activation; a lost generation or shutdown answers it unavailable.
  void DrainDeferredTaskDiscoveryBootstraps();
  void ResolveDeferredTaskDiscoveryBootstrapsUnavailable();
  void ExecuteTaskPageAction(core_service::mojom::TaskEffectBindingPtr effect,
                             ExecuteTaskEffectCallback callback);
  void ExecuteTaskTabAction(core_service::mojom::TaskEffectBindingPtr effect,
                            ExecuteTaskEffectCallback callback);
  void ExecuteTaskTabRelease(core_service::mojom::TaskEffectBindingPtr effect,
                             ExecuteTaskEffectCallback callback);
  void OnTaskTabIntentRecorded(core_service::mojom::TaskEffectBindingPtr effect,
                               ExecuteTaskEffectCallback callback,
                               CapabilityReference capability_reference,
                               bool committed);
  void ExecuteTaskDownloadAction(
      core_service::mojom::TaskEffectBindingPtr effect,
      ExecuteTaskEffectCallback callback);
  // A read of a store the start attached (decision 0133). It spends its
  // grant, reads through the profile store reader or the tab registry, and
  // answers the bounded rows; it journals no dispatch, because a local read
  // has no consequence in the world to reconcile after a crash.
  void ExecuteTaskStoreAction(core_service::mojom::TaskEffectBindingPtr effect,
                              ExecuteTaskEffectCallback callback);
  void OnTaskStoreEntriesRead(core_service::mojom::TaskEffectBindingPtr effect,
                              ExecuteTaskEffectCallback callback,
                              CapabilityReference capability_reference,
                              uint64_t generation,
                              std::vector<TaskStoreEntry> entries);
  void FinishTaskStoreAction(core_service::mojom::TaskEffectBindingPtr effect,
                             ExecuteTaskEffectCallback callback,
                             CapabilityReference capability_reference,
                             const std::vector<TaskStoreEntry>& entries);
  void OnTaskDownloadIntentRecorded(
      core_service::mojom::TaskEffectBindingPtr effect,
      ExecuteTaskEffectCallback callback,
      CapabilityReference capability_reference,
      ActorLeaseId actor_lease_id,
      bool committed);
  void OnTaskDownloadStarted(
      core_service::mojom::TaskEffectBindingPtr effect,
      ExecuteTaskEffectCallback callback,
      CapabilityReference capability_reference,
      std::optional<TaskDownloadManagerSnapshot> snapshot);
  void FinishTaskDownloadAction(
      core_service::mojom::TaskEffectBindingPtr effect,
      ExecuteTaskEffectCallback callback,
      CapabilityReference capability_reference,
      ActionResultCode code,
      core_service::mojom::TaskDownloadActionResultPtr result,
      bool intent_committed);
  void ExecuteTaskNavigate(core_service::mojom::TaskEffectBindingPtr effect,
                           ExecuteTaskEffectCallback callback);
  void ExecuteTaskHandover(core_service::mojom::TaskEffectBindingPtr effect,
                           ExecuteTaskEffectCallback callback);
  void OnHandoverExpired(std::string task_id);
  void AbandonOpenHandover(const std::string& task_id);
  void AbandonAllOpenHandovers();
  core_service::mojom::CoreServiceCommandPtr MakeExpireHandover(
      const std::string& task_id,
      const std::string& handover_id,
      uint64_t task_revision) const;
  void OnTaskActionCompleted(core_service::mojom::TaskEffectBindingPtr binding,
                             ExecuteTaskEffectCallback callback,
                             std::optional<ActionResult> result);

  // ObservedServiceRemote::Observer:
  void OnServiceLaunched(const content::ServiceProcessInfo& info) override;
  void OnServiceTerminatedNormally(
      const content::ServiceProcessInfo& info) override;
  void OnServiceCrashed(const content::ServiceProcessInfo& info) override;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_MANAGER_H_
