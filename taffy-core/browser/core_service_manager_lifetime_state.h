// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_MANAGER_LIFETIME_STATE_H_
#define TAFFY_BROWSER_CORE_SERVICE_MANAGER_LIFETIME_STATE_H_

#include <stdint.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/observed_service_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_service_manager_types.h"
#include "taffy/browser/core_service_recovery_policy.h"
#include "taffy/browser/core_site_skill_offer_broker.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "taffy/browser/core_state_cache.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/browser/task_download_ownership_registry.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/components/security/browser/value_reference_vault.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

class PrefService;

namespace content {
class BrowserContext;
}

namespace taffy {

class AssetNetworkObserver;
class CoreEffectBroker;
class CorePageObservationBroker;
class CoreServiceManager;
class CoreDeferredTaskSurfaceOwner;
class CoreBackupProtocol;
class CoreStorageBroker;
class FieldValueRequestCoordinator;
class ProfileAccountBroker;
class ProfileAssetPlane;
class ProfileEntitlementCache;
class ProfileModelBroker;
class ProfileModelRegister;
class ProfileProviderListingFetcher;
class ProfileProviderAuthBroker;
class ProfileSavedDataBroker;
class ProfileStoreReader;
class ProfileToolSupervisor;
class ProfileToolArtifactBroker;
class ProviderAccessResolver;

namespace filtering {
class FilteringRulesetService;
}

// The profile-owned services whose lifetimes the manager orders. Bundling
// them keeps construction named and prevents every caller from depending on a
// long positional parameter list.
struct CoreServiceManagerResources {
  std::unique_ptr<CoreStorageBroker> storage_broker;
  scoped_refptr<ProfileModelRegister> model_register;
  std::unique_ptr<ProfileToolSupervisor> tool_supervisor;
  std::unique_ptr<ProfileToolArtifactBroker> tool_artifact_broker;
  std::unique_ptr<ProfileSavedDataBroker> saved_data_broker;
  std::unique_ptr<ProfileStoreReader> profile_store_reader;
  scoped_refptr<CorePageObservationBroker> page_observation_broker;
  std::unique_ptr<CoreEffectBroker> effect_broker;
  std::unique_ptr<ProfileAccountBroker> account_broker;
  std::unique_ptr<ProfileProviderAuthBroker> provider_auth_broker;
  std::unique_ptr<ProfileProviderListingFetcher> provider_listing_fetcher;
  std::unique_ptr<ProfileAssetPlane> asset_plane;
  std::unique_ptr<ProfileEntitlementCache> entitlement_cache;
  std::unique_ptr<filtering::FilteringRulesetService> filtering_service;
  std::unique_ptr<ProfileModelBroker> model_broker;
};

// Lifetime-ordered storage for CoreServiceManager. Keeping the state in one
// base preserves unqualified access from the manager's responsibility-sharded
// implementation files while making the lifetime contract independently
// readable. It is the last base, so it is destroyed before every service
// interface base just as the manager's former direct members were.
class CoreServiceManagerLifetimeState {
 protected:
  friend class CoreDeferredTaskSurfaceOwner;
  friend class CoreBackupProtocol;
  using TeardownReason = CoreServiceTeardownReason;
  using PendingAdmission = CorePendingAdmission;
  using PendingPolicyEvaluation = CorePendingPolicyEvaluation;
  using PendingPageInspection = CorePendingPageInspection;
  using PendingPageExport = CorePendingPageExport;
  using DeferredTaskSurface = CoreDeferredTaskSurface;

  struct PendingTaskSurface {
    uint64_t state_sequence = 0;
    core_service::mojom::TaskEffectBindingPtr binding;
    core_service::mojom::CoreHost::ExecuteTaskEffectCallback callback;
  };

  // One zero-source errand's discovery bootstrap, authorized and waiting for
  // a window to be active. It holds the effect's own callback, so the
  // effect stays pending in `pending_host_task_effect_ids_` and idle
  // teardown cannot release the generation from under it.
  struct DeferredDiscoveryBootstrap {
    core_service::mojom::TaskEffectBindingPtr effect;
    core_service::mojom::CoreHost::ExecuteTaskEffectCallback callback;
  };

  using TaskAnswerKey = std::pair<std::string, std::string>;
  struct TaskAnswerSequenceState {
    uint32_t next_sequence = 0;
    bool terminal = false;
  };

  // The browser-private second half of one exact form confirmation. The
  // person's bytes are in ValueReferenceVault; this record contains only the
  // executable metadata that a later AskPolicy must repeat exactly.
  using FormApprovalKey = std::pair<std::string, std::string>;
  CoreServiceManagerLifetimeState(CoreServiceManager* owner,
                                  core_service::mojom::CoreHost* core_host,
                                  content::BrowserContext* browser_context,
                                  PrefService* profile_prefs,
                                  bool private_profile,
                                  CoreServiceManagerResources resources,
                                  std::string browser_session_id);
  ~CoreServiceManagerLifetimeState();

  void RevokePreapprovedFormActionsForTask(const std::string& task_id);

  const raw_ptr<content::BrowserContext> browser_context_;
  const raw_ptr<PrefService> profile_prefs_;
  const bool private_profile_;
  ActorLeaseRegistry actor_leases_;
  CapabilityLedger capabilities_;
  // This one owns a deadline timer. Reverse destruction must first take down
  // every broker that can still mint into it.
  ValueReferenceVault value_references_;
  // Handlers bind these adapters unretained, so reverse destruction must
  // destroy the effect broker before them.
  std::unique_ptr<CoreStorageBroker> storage_broker_;
  // The supervisor holds a port reference; declaration order keeps the
  // register alive until that port is destroyed.
  scoped_refptr<ProfileModelRegister> model_register_;
  std::unique_ptr<ProfileToolSupervisor> tool_supervisor_;
  // Minting and retained output call into the supervisor, so reverse
  // destruction releases browser custody before that supervisor.
  std::unique_ptr<ProfileToolArtifactBroker> tool_artifact_broker_;
  std::unique_ptr<ProfileSavedDataBroker> saved_data_broker_;
  // Reads History and Bookmarks for an attached-store read (decision 0133);
  // null in a binary with no Chrome profile composition and for a private
  // profile, where every such read answers unavailable.
  std::unique_ptr<ProfileStoreReader> profile_store_reader_;
  scoped_refptr<CorePageObservationBroker> page_observation_broker_;
  std::unique_ptr<ProfileAccountBroker> account_broker_;
  std::unique_ptr<ProfileProviderAuthBroker> provider_auth_broker_;
  // The model broker owns the callback into this resolver and is destroyed
  // first. The resolver in turn points at the two preceding brokers.
  std::unique_ptr<ProviderAccessResolver> provider_access_resolver_;
  // This fetcher asks through the resolver above, so reverse destruction must
  // release it first. It stores no credential after a request ends.
  std::unique_ptr<ProfileProviderListingFetcher> provider_listing_fetcher_;
  std::unique_ptr<ProfileAssetPlane> asset_plane_;
  // The model broker owns callbacks into this cache, while this cache owns a
  // callback into the account broker. Member order encodes that lifetime.
  std::unique_ptr<ProfileEntitlementCache> entitlement_cache_;
  // The service reads the plane's asset store, so reverse destruction takes
  // the service down before the filtering service and asset plane.
  std::unique_ptr<filtering::FilteringRulesetService> filtering_service_;
  std::unique_ptr<ProfileModelBroker> model_broker_;
  std::unique_ptr<CoreEffectBroker> effect_broker_;
  content::ObservedServiceRemote<core_service::mojom::TaffyCoreService>
      service_;
  mojo::Remote<core_service::mojom::CoreSession> session_;
  mojo::Receiver<core_service::mojom::CoreHost> host_receiver_;
  std::unique_ptr<CoreBackupProtocol> backup_protocol_;
  // A Mojo pipe can report its disconnect before Android reports the utility
  // process terminal. Keep the launched process tied to the generation it
  // served so that late OS notification cannot tear down its successor.
  std::optional<std::pair<content::ServiceProcessId, uint64_t>>
      observed_service_process_;

  CoreServiceRecoveryPolicy recovery_policy_;
  base::OneShotTimer launch_timer_;
  base::TimeTicks launch_not_before_;
  base::flat_map<std::string, PendingAdmission> pending_admissions_;
  base::flat_map<std::string, PendingPolicyEvaluation>
      pending_policy_evaluations_;
  std::vector<PendingPageInspection> pending_page_inspections_;
  // Export callbacks are correlated only by their attempt-unique operation
  // identity. The reverse index makes a surface request identity exclusive
  // only while that attempt is live; once terminal, the same logical request
  // may be retried without giving a late callback authority over the retry.
  base::flat_map<std::string, PendingPageExport> pending_page_exports_;
  base::flat_map<std::string, std::string> active_page_export_requests_;
  // Every asynchronous CoreHost task-effect reply, including browser actions
  // that deliberately bypass the durable effect broker.
  base::flat_set<std::string> pending_host_task_effect_ids_;
  // Live, non-journalled provider listing and endpoint-probe questions. Their
  // exact identities keep idle teardown from revoking work that deliberately
  // has no durable replay owner.
  base::flat_set<std::string> pending_live_effect_ids_;
  CoreSiteSkillOfferBroker site_skill_offers_;
  base::flat_map<std::string, PendingTaskSurface> pending_task_surfaces_;
  base::flat_map<std::string, DeferredTaskSurface> deferred_task_surfaces_;
  std::vector<DeferredDiscoveryBootstrap> deferred_discovery_bootstraps_;
  base::OneShotTimer deferred_task_surface_deadline_timer_;
  std::vector<base::OnceCallback<void(bool)>> pending_core_api_preparations_;
  base::flat_set<std::string> emitted_permission_requests_;
  base::flat_set<std::string> emitted_field_value_requests_;
  base::flat_map<FormApprovalKey, BrowserFormActionPreapproval>
      preapproved_form_actions_;
  base::flat_set<std::string> completed_task_settlements_;
  // Anti-splicing state for streamed task answers. Completed keys remain in a
  // bounded FIFO so a replayed sequence-zero terminal cannot draw twice.
  base::flat_map<TaskAnswerKey, TaskAnswerSequenceState> task_answer_sequences_;
  std::deque<TaskAnswerKey> completed_task_answer_order_;
  size_t active_task_answer_count_ = 0;
  base::ObserverList<CoreServiceObserver> observers_;
  // Destroyed before every registry it can mint into and while the observer
  // list remains whole.
  std::unique_ptr<FieldValueRequestCoordinator> field_value_requests_;

  uint64_t service_generation_ = 1;
  std::string browser_profile_id_;
  const std::string browser_session_id_;
  base::RepeatingCallback<IssuedSourceLiveness(
      const core_service::mojom::TaskConsentSource&)>
      live_task_source_validator_;
  CoreStateCache state_cache_;
  core_service::mojom::ReplaceSavedDataSnapshotCommandPtr
      latest_saved_data_snapshot_;
  CoreStateBindingRegistry state_bindings_;
  AcceptedApprovalLedger accepted_approvals_;
  TaskDownloadOwnershipRegistry task_download_ownership_;
  using OpenHandover = CoreOpenHandover;
  base::flat_map<std::string, OpenHandover> open_handovers_;
  uint64_t late_reply_count_ = 0;
  CoreServiceAvailability availability_ = CoreServiceAvailability::kStopped;
  TeardownReason teardown_reason_ = TeardownReason::kNone;
  base::OneShotTimer idle_teardown_timer_;
  bool launch_in_progress_ = false;
  bool disconnect_handled_ = true;
  bool shutdown_started_ = false;

  // Destroyed while the manager is intact because its callback is unretained.
  std::unique_ptr<AssetNetworkObserver> asset_network_observer_;
  std::optional<core_service::mojom::AssetNetworkCost> last_asset_network_cost_;
  bool asset_metered_permitted_ = false;
  base::RepeatingTimer entitlement_poke_timer_;
  bool entitlement_fetch_in_flight_ = false;

  base::WeakPtrFactory<CoreServiceManager> weak_factory_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_MANAGER_LIFETIME_STATE_H_
