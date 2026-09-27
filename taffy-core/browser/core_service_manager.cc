// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/assets/asset_network_observer.h"
#include "taffy/browser/assets/profile_asset_plane.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/browser/model/profile_entitlement_cache.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "taffy/browser/model/profile_provider_listing_fetcher.h"
#include "taffy/browser/policy_response_validation.h"
#include "taffy/browser/profile_model_register.h"
#include "taffy/browser/profile_store/profile_store_reader.h"
#include "taffy/browser/profile_page_media_store.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_access_resolver.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

std::string NewBrowserSessionId() {
  const base::Uuid id = base::Uuid::GenerateRandomV4();
  CHECK(id.is_valid());
  return "browser-session-" + id.AsLowercaseString();
}

}  // namespace

CoreServiceManager::CoreServiceManager(content::BrowserContext* browser_context,
                                       PrefService* profile_prefs,
                                       CoreServiceManagerResources resources)
    : CoreTaskBrowserActions(browser_context),
      CoreServiceManagerLifetimeState(this,
                                      this,
                                      browser_context,
                                      profile_prefs,
                                      browser_context->IsOffTheRecord(),
                                      std::move(resources),
                                      NewBrowserSessionId()) {
  CHECK(browser_context_);
  CHECK(tool_supervisor_);
  CHECK(tool_artifact_broker_);
  CHECK(page_observation_broker_);
  CHECK_EQ(private_profile_, !account_broker_);
  // The provider auth broker travels with the account broker: both exist
  // exactly when the profile has an account plane, because the sign-in's
  // Custom Tab, vault and sealed store all ride that plane's adapter.
  CHECK_EQ(!account_broker_, !provider_auth_broker_);
  if (provider_auth_broker_) {
    provider_auth_broker_->SetFlowTerminalCallback(
        base::BindRepeating(&CoreServiceManager::OnProviderFlowTerminated,
                            weak_factory_.GetWeakPtr()));
    // The subscription-aware answer to the model broker's credential
    // question (decision 0081). Installed here rather than in the factory so
    // the resolver exists exactly when the two brokers it rides do, and dies
    // between the model broker (the only holder of this callback, destroyed
    // first by member order) and the brokers it points at.
    if (model_broker_) {
      provider_access_resolver_ = std::make_unique<ProviderAccessResolver>(
          account_broker_.get(), provider_auth_broker_.get());
      model_broker_->SetCredentialResolver(base::BindRepeating(
          &ProviderAccessResolver::Resolve,
          base::Unretained(provider_access_resolver_.get())));
    }
  }
  // The probe's one-shot spender (decision 0083). A pasted draft's transient
  // lives in the same platform vault the account plane owns, and this is the
  // only seam that spends one. Unretained holds for the resolver's reason:
  // the model broker — the only holder of this callback — is destroyed
  // before the account broker by member order.
  if (account_broker_ && model_broker_) {
    model_broker_->SetTransientCredentialConsumer(
        base::BindRepeating(&ProfileAccountBroker::ConsumeTransientCredential,
                            base::Unretained(account_broker_.get())));
  }
  // The entitlement cache travels with the account plane, for the account
  // plane's reason: a mint is the signed-in session's act. Its token seam is
  // installed here so it exists exactly when the cache and the broker both do
  // and dies between them, by the member order the header argues.
  CHECK_EQ(!account_broker_, !entitlement_cache_);
  if (entitlement_cache_ && model_broker_) {
    model_broker_->SetEntitlementTokenProvider(
        base::BindRepeating(&ProfileEntitlementCache::AcquireToken,
                            base::Unretained(entitlement_cache_.get())));
    model_broker_->SetQuotaRefusedCallback(base::BindRepeating(
        [](CoreServiceManager* manager) {
          manager->PokeEntitlementRefresh(
              service_mojom::EntitlementFetchReason::kQuotaRefused);
        },
        base::Unretained(this)));
  }
  if (model_broker_) {
    model_broker_->SetModelStreamChunkDispatcher(
        base::BindRepeating(&CoreServiceManager::DeliverModelStreamChunk,
                            weak_factory_.GetWeakPtr()));
    model_broker_->SetPageMediaStore(
        page_observation_broker_->page_media_store());
  }
  if (account_broker_) {
    account_broker_->SetTokenValidator(
        base::BindRepeating(&CoreServiceManager::ValidateAccountTokenResponse,
                            base::Unretained(this)));
    account_broker_->SetFlowTerminalCallback(
        base::BindRepeating(&CoreServiceManager::OnAccountFlowTerminated,
                            weak_factory_.GetWeakPtr()));
  }
  InitializeSavedDataBroker();
  CHECK(asset_plane_);
  CHECK(provider_listing_fetcher_);
  CHECK(model_register_);
  CHECK(filtering_service_);
  asset_network_observer_ =
      std::make_unique<AssetNetworkObserver>(base::BindRepeating(
          &CoreServiceManager::OnAssetNetworkCost, base::Unretained(this)));
  asset_plane_->SetProgressObserver(base::BindRepeating(
      [](CoreServiceManager* manager,
         const ProfileAssetPlane::LiveProgress& progress) {
        manager->OnAssetProgress(progress.asset_id, progress.asset_revision,
                                 progress.written_bytes, progress.total_bytes);
      },
      base::Unretained(this)));
  CHECK(effect_broker_);
  effect_broker_->SetPendingChangedCallback(base::BindRepeating(
      &CoreServiceManager::RefreshIdleTeardown, weak_factory_.GetWeakPtr()));
  site_skill_offers_.SetPendingChangedCallback(base::BindRepeating(
      &CoreServiceManager::RefreshIdleTeardown, weak_factory_.GetWeakPtr()));
  live_task_source_validator_ = base::BindRepeating(
      &CoreServiceManager::IssuedTaskSourceLiveness, base::Unretained(this));
  // The browser-side owner of an open field-value request (decision 0088).
  // Unretained holds for the reason the resolvers above give: this manager
  // owns the coordinator and destroys it, and the coordinator is declared
  // after every member whose address it is handed. It is deliberately not
  // added to `observers_` — that list is read as "is any Taffy surface
  // watching this profile", and a permanent entry would answer yes forever.
  field_value_requests_ =
      FieldValueRequestCoordinator::ForProfile(this, browser_context_);
  service_.AddObserver(this);
}

void CoreServiceManager::OnAssetProgress(const std::string& asset_id,
                                         const std::string& asset_revision,
                                         uint64_t written_bytes,
                                         uint64_t total_bytes) {
  for (Observer& observer : observers_) {
    observer.OnAssetProgress(asset_id, asset_revision, written_bytes,
                             total_bytes);
  }
}

CoreServiceManager::~CoreServiceManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Shutdown();
  service_.RemoveObserver(this);
}

TaskJournalSink* CoreServiceManager::task_journal_sink() {
  return storage_broker_.get();
}

filtering::FilteringRulesetService* CoreServiceManager::filtering_service() {
  return filtering_service_.get();
}

ValueReferenceVault* CoreServiceManager::value_references() {
  return &value_references_;
}

void CoreServiceManager::RetryExplicitly() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_) {
    return;
  }
  recovery_policy_.RetryExplicitly();
  SetAvailability(Availability::kStopped);
  EnsureStarted();
}

void CoreServiceManager::TearDownForIdle() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!service_.remote().is_bound() || !IsQuiescent()) {
    RefreshIdleTeardown();
    return;
  }
  teardown_reason_ = TeardownReason::kIdle;
  session_.reset();
  host_receiver_.reset();
  service_.remote().reset();
  HandleDisconnect(/*unexpected=*/false);
}

bool CoreServiceManager::IsQuiescent() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Only a running service can be released, and only from rest: a launch in
  // flight is work, and every other availability either has no process to
  // release or is already on its way down.
  if (availability_ != Availability::kReady || launch_in_progress_ ||
      shutdown_started_ || !service_.remote().is_bound()) {
    return false;
  }
  // Nothing is watching. The Core API facade observes only while a Taffy
  // surface holds it, so an empty list means no UI would see the relaunch.
  if (!observers_.empty() || HasRegisteredTaskSourceWindows()) {
    return false;
  }
  // No request is outstanding towards the service.
  if (!pending_admissions_.empty() || !pending_policy_evaluations_.empty() ||
      backup_protocol_->has_live_work() ||
      !pending_page_inspections_.empty() || !pending_task_surfaces_.empty() ||
      !pending_page_exports_.empty() || site_skill_offers_.has_pending() ||
      !pending_core_api_preparations_.empty() || !open_handovers_.empty() ||
      !pending_host_task_effect_ids_.empty() ||
      !pending_live_effect_ids_.empty() || active_task_answer_count_ != 0u ||
      entitlement_fetch_in_flight_) {
    return false;
  }
  if ((account_broker_ && account_broker_->has_pending_auth_flow()) ||
      (provider_auth_broker_ && provider_auth_broker_->has_active_flows()) ||
      (field_value_requests_ &&
       field_value_requests_->open_request_count() != 0u)) {
    return false;
  }
  // No effect is outstanding back towards the browser. The core blocks on
  // each one, so an empty broker is the service telling us it is waiting.
  return !effect_broker_->HasPendingEffects() &&
         !provider_listing_fetcher_->has_fetch() &&
         !tool_supervisor_->HasActiveJobs();
}

void CoreServiceManager::RefreshIdleTeardown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  idle_teardown_timer_.Stop();
  if (!IsQuiescent()) {
    return;
  }
  idle_teardown_timer_.Start(
      FROM_HERE, kIdleTeardownDelay,
      base::BindOnce(&CoreServiceManager::OnIdleTeardownDeadline,
                     weak_factory_.GetWeakPtr()));
}

void CoreServiceManager::OnIdleTeardownDeadline() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsQuiescent()) {
    return;
  }
  TearDownForIdle();
}

void CoreServiceManager::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
  RefreshIdleTeardown();
  observer->OnCoreAvailabilityChanged(availability_);
  if (const service_mojom::CoreStateUpdate* state = state_cache_.latest()) {
    observer->OnCoreState(*state);
  }
}

void CoreServiceManager::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
  RefreshIdleTeardown();
}

size_t CoreServiceManager::pending_admission_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pending_admissions_.size();
}

std::optional<uint64_t> CoreServiceManager::FindTaskRevision(
    const std::string& task_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_bindings_.service_generation() != service_generation_) {
    return std::nullopt;
  }
  return state_bindings_.FindTaskRevision(task_id);
}

std::optional<TaskControlLookup> CoreServiceManager::FindTaskControl(
    const std::string& task_id,
    service_mojom::TaskControlKind kind) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_bindings_.service_generation() != service_generation_) {
    return std::nullopt;
  }
  return state_bindings_.FindTaskControl(task_id, kind);
}

std::optional<PendingApprovalLookup> CoreServiceManager::FindPendingApproval(
    const std::string& task_id,
    const std::string& action_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_bindings_.service_generation() != service_generation_) {
    return std::nullopt;
  }
  return state_bindings_.FindPendingApproval(task_id, action_id);
}

std::optional<PendingPermissionLookup>
CoreServiceManager::FindPendingPermission(const std::string& request_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_bindings_.service_generation() != service_generation_) {
    return std::nullopt;
  }
  return state_bindings_.FindPendingPermission(request_id);
}

std::optional<TerminalTaskLookup> CoreServiceManager::FindTerminalTask(
    const std::string& task_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_bindings_.service_generation() != service_generation_) {
    return std::nullopt;
  }
  return state_bindings_.FindTerminalTask(task_id);
}

void CoreServiceManager::PrepareForCoreApi(
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || recovery_policy_.circuit_open()) {
    std::move(callback).Run(false);
    return;
  }
  if (availability_ == Availability::kReady && session_.is_bound()) {
    std::move(callback).Run(true);
    return;
  }
  pending_core_api_preparations_.push_back(std::move(callback));
  RefreshIdleTeardown();
  EnsureStarted();
}

void CoreServiceManager::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_) {
    return;
  }
  shutdown_started_ = true;
  launch_timer_.Stop();
  entitlement_poke_timer_.Stop();
  if (provider_auth_broker_) {
    provider_auth_broker_->CancelAll();
  }
  // Browser-owned live network work is not part of the Core effect journal.
  // Stop it explicitly at profile shutdown instead of relying on member
  // destruction after a late token or URL-loader callback has already run.
  provider_listing_fetcher_->CancelGeneration(service_generation_);
  if (entitlement_cache_) {
    entitlement_cache_->CancelPendingMint();
  }
  pending_live_effect_ids_.clear();
  entitlement_fetch_in_flight_ = false;
  SetAvailability(Availability::kShuttingDown);
  teardown_reason_ = TeardownReason::kProfileShutdown;
  ResolvePendingAdmissionsUnavailable();
  ResolvePendingPolicyUnavailable();
  ResolvePendingPageInspectionsUnavailable();
  ResolvePendingPageExportsUnavailable();
  ResolvePendingTaskSurfacesUnavailable();
  ResolveDeferredTaskDiscoveryBootstrapsUnavailable();
  ResolvePendingCoreApiPreparations(false);
  AbandonAllOpenHandovers();
  tool_artifact_broker_->Shutdown();
  model_register_->Withdraw();
  tool_supervisor_->Shutdown();
  effect_broker_->OnGenerationDisconnected(service_generation_);
  actor_leases_.RevokeGeneration(service_generation_);
  capabilities_.RevokeGeneration(service_generation_);
  value_references_.RevokeGeneration(service_generation_);
  if (field_value_requests_) {
    field_value_requests_->CloseAllRequests();
  }
  preapproved_form_actions_.clear();
  accepted_approvals_.Reset();
  state_bindings_.Reset();
  state_cache_.Reset();
  task_answer_sequences_.clear();
  completed_task_answer_order_.clear();
  active_task_answer_count_ = 0;
  session_.reset();
  backup_protocol_->OnServiceDisconnected();
  host_receiver_.reset();

  if (!service_.remote().is_bound()) {
    return;
  }

  service_.remote()->PrepareForShutdown(base::BindOnce(
      [](base::WeakPtr<CoreServiceManager> manager, bool) {
        if (!manager) {
          return;
        }
        manager->service_.remote().reset();
        manager->HandleDisconnect(/*unexpected=*/false);
      },
      weak_factory_.GetWeakPtr()));
}

}  // namespace taffy
