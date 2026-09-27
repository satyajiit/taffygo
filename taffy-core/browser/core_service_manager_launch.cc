// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/rand_util.h"
#include "base/time/time.h"
#include "content/public/browser/service_process_host.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/account_plane_configuration.h"
#include "taffy/browser/asset_delivery_configuration.h"
#include "taffy/browser/assets/profile_asset_plane.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

bool AddBootstrapBytes(size_t amount, size_t* total) {
  if (*total > std::numeric_limits<size_t>::max() - amount) {
    return false;
  }
  *total += amount;
  return true;
}

size_t BootstrapByteSize(const service_mojom::CoreBootstrap& bootstrap) {
  size_t total = 0;
  if (!AddBootstrapBytes(bootstrap.core_journal_schema_checksum.size(),
                         &total) ||
      !AddBootstrapBytes(bootstrap.browser_profile_id.size(), &total) ||
      !AddBootstrapBytes(bootstrap.generation_capability_entropy.size(),
                         &total)) {
    return std::numeric_limits<size_t>::max();
  }
  if (bootstrap.account_session &&
      (!AddBootstrapBytes(bootstrap.account_session->session_handle.size(),
                          &total) ||
       !AddBootstrapBytes(bootstrap.account_session->account_subject.size(),
                          &total))) {
    return std::numeric_limits<size_t>::max();
  }
  for (const auto& task : bootstrap.tasks) {
    if (!task || !AddBootstrapBytes(task->task_id.size(), &total) ||
        !AddBootstrapBytes(task->task_id_seed.size(), &total)) {
      return std::numeric_limits<size_t>::max();
    }
    for (const auto& batch : task->batches) {
      if (!batch || !AddBootstrapBytes(batch->effect_id.size(), &total) ||
          !AddBootstrapBytes(batch->transaction_batch.size(), &total)) {
        return std::numeric_limits<size_t>::max();
      }
    }
  }
  return total;
}

bool HasUsableEntropy(const std::vector<uint8_t>& entropy) {
  return entropy.size() == 32u &&
         std::adjacent_find(entropy.begin(), entropy.end(),
                            std::not_equal_to<>()) != entropy.end();
}

}  // namespace

void CoreServiceManager::EnsureStarted() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || launch_in_progress_ ||
      service_.remote().is_bound() ||
      (pending_admissions_.empty() && pending_policy_evaluations_.empty() &&
       pending_page_inspections_.empty() &&
       pending_core_api_preparations_.empty())) {
    return;
  }
  if (recovery_policy_.circuit_open()) {
    SetAvailability(Availability::kCircuitOpen);
    ResolvePendingAdmissionsUnavailable();
    ResolvePendingPolicyUnavailable();
    ResolvePendingPageInspectionsUnavailable();
    ResolvePendingPageExportsUnavailable();
    ResolvePendingCoreApiPreparations(false);
    return;
  }

  const base::TimeTicks now = base::TimeTicks::Now();
  if (!launch_not_before_.is_null() && now < launch_not_before_) {
    if (!launch_timer_.IsRunning()) {
      launch_timer_.Start(FROM_HERE, launch_not_before_ - now, this,
                          &CoreServiceManager::EnsureStarted);
    }
    return;
  }
  LoadBootstrapAndLaunch();
}

void CoreServiceManager::LoadBootstrapAndLaunch() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  launch_in_progress_ = true;
  SetAvailability(Availability::kStarting);
  const uint64_t generation = service_generation_;
  tool_supervisor_->SetActiveGeneration(generation);
  tool_artifact_broker_->SetActiveGeneration(generation);
  effect_broker_->SetActiveGeneration(generation);
  // Both, because reconciliation is a conversation between them: the durable
  // record of the last session lives in storage and the vault that holds the
  // session itself is the account broker's. Either one absent means there is
  // nothing to reconcile, and the launch takes the path below instead.
  //
  // The storage half of that condition was missing once, and only the device
  // could see it, because this block used to be compiled out on a host: a
  // manager built without a storage broker — which three of this directory's
  // tests do, deliberately, and which the constructor accepts — was well
  // defined on the host and called a method on nothing here. It crashed the
  // device suite at base/threading/sequence_bound.h's DCHECK, one test in,
  // having passed on the host minutes earlier. The block is no longer
  // compiled out anywhere, which is what makes the host suite able to see the
  // next one of these.
  if (account_broker_ && storage_broker_) {
    storage_broker_->LoadAccountReconciliationState(base::BindOnce(
        [](base::WeakPtr<CoreServiceManager> manager, uint64_t generation,
           std::optional<CoreStorageBroker::AccountReconciliationState> state) {
          if (!manager) {
            return;
          }
          if (generation != manager->service_generation_ ||
              manager->shutdown_started_ || !manager->launch_in_progress_) {
            ++manager->late_reply_count_;
            return;
          }
          if (!state) {
            manager->FailBootstrapLaunch(generation);
            return;
          }
          const bool pending = state->has_pending_session_mutation;
          auto committed = std::move(state->committed_session);
          if (!manager->account_broker_) {
            manager->FailBootstrapLaunch(generation);
            return;
          }
          manager->account_broker_->InspectCanonicalSession(base::BindOnce(
              &CoreServiceManager::OnAccountVaultInspected, manager, generation,
              pending, std::move(committed)));
        },
        weak_factory_.GetWeakPtr(), generation));
    return;
  }
  LoadCommittedBootstrap(generation);
}

void CoreServiceManager::LoadCommittedBootstrap(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_ ||
      !launch_in_progress_) {
    ++late_reply_count_;
    return;
  }
  effect_broker_->LoadBootstrap(
      generation, private_profile_,
      base::BindOnce(&CoreServiceManager::OnBootstrapLoaded,
                     weak_factory_.GetWeakPtr(), generation));
}

void CoreServiceManager::FailBootstrapLaunch(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_) {
    ++late_reply_count_;
    return;
  }
  launch_in_progress_ = false;
  SetAvailability(Availability::kUnavailable);
  ResolvePendingAdmissionsUnavailable();
  ResolvePendingPolicyUnavailable();
  ResolvePendingPageInspectionsUnavailable();
  ResolvePendingPageExportsUnavailable();
  ResolvePendingCoreApiPreparations(false);
}

void CoreServiceManager::OnAccountVaultInspected(
    uint64_t generation,
    bool has_pending_session_mutation,
    service_mojom::AccountSessionHandlePtr committed_session,
    bool inspection_succeeded,
    std::optional<std::string> vault_handle,
    uint64_t vault_rotation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_ ||
      !launch_in_progress_) {
    ++late_reply_count_;
    return;
  }
  if (!account_broker_) {
    FailBootstrapLaunch(generation);
    return;
  }
  if (!inspection_succeeded) {
    // A malformed platform record is not allowed to poison every subsequent
    // launch. Clear the profile-owned vault before atomically reconciling the
    // durable browser journal and receipt.
    account_broker_->ClearCanonicalSession(
        base::BindOnce(&CoreServiceManager::OnAccountVaultCleared,
                       weak_factory_.GetWeakPtr(), generation));
    return;
  }
  const bool exact_match =
      !has_pending_session_mutation &&
      ((!committed_session && !vault_handle) ||
       (committed_session && vault_handle &&
        committed_session->session_handle == *vault_handle &&
        committed_session->rotation == vault_rotation));
  if (exact_match) {
    LoadCommittedBootstrap(generation);
    return;
  }
  account_broker_->ClearCanonicalSession(
      base::BindOnce(&CoreServiceManager::OnAccountVaultCleared,
                     weak_factory_.GetWeakPtr(), generation));
}

void CoreServiceManager::OnAccountVaultCleared(uint64_t generation,
                                               bool cleared) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_ ||
      !launch_in_progress_) {
    ++late_reply_count_;
    return;
  }
  if (!cleared) {
    FailBootstrapLaunch(generation);
    return;
  }
  storage_broker_->FinishAccountReconciliation(
      base::BindOnce(&CoreServiceManager::OnAccountSqlReconciled,
                     weak_factory_.GetWeakPtr(), generation));
}

void CoreServiceManager::OnAccountSqlReconciled(uint64_t generation,
                                                bool reconciled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_ ||
      !launch_in_progress_) {
    ++late_reply_count_;
    return;
  }
  if (!reconciled) {
    FailBootstrapLaunch(generation);
    return;
  }
  LoadCommittedBootstrap(generation);
}

void CoreServiceManager::OnBootstrapLoaded(
    uint64_t generation,
    service_mojom::CoreBootstrapPtr bootstrap) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_) {
    ++late_reply_count_;
    return;
  }
  if (!bootstrap || bootstrap->browser_profile_id.empty() ||
      bootstrap->browser_profile_id.size() >
          service_mojom::kMaxIdentifierBytes ||
      BootstrapByteSize(*bootstrap) >
          service_mojom::kMaxQueuedBytesPerProfile) {
    FailBootstrapLaunch(generation);
    return;
  }

  bootstrap->service_generation = generation;
  bootstrap->private_profile = private_profile_;
  bootstrap->browser_session_id = browser_session_id_;
  bootstrap->available_account_methods.clear();
  if (!private_profile_ &&
      ValidateAccountPlaneConfiguration(TAFFY_ACCOUNT_API_ORIGIN,
                                        TAFFY_ACCOUNT_PUBLISHABLE_KEY) ==
          AccountPlaneConfigurationStatus::kReady) {
    // Google is reported available only when this build can actually run the
    // flow. The broker refuses `kRequestNativeCredential` outright when the
    // server client id is empty, so a method reported available there would be
    // a button that opens nothing.
    //
    // This used to push Google unconditionally, with a comment saying that
    // hiding it "left the sign-in page with no way in". That was true while the
    // surface ignored this list and drew every row regardless; it no longer is.
    // Absent from this list now means `NOT_CONFIGURED` on screen SCR-701 — a
    // row that is drawn, disabled, and says why — which is what OD-087 asks of
    // a method the account plane cannot serve.
#if BUILDFLAG(IS_ANDROID)
    if (ValidateGoogleServerClientConfiguration(
            TAFFY_ACCOUNT_GOOGLE_SERVER_CLIENT_ID) ==
        GoogleServerClientConfigurationStatus::kReady) {
      bootstrap->available_account_methods.push_back(
          service_mojom::AccountAuthMethod::kGoogle);
    }
#endif
    // The other three have no equivalent gate and must not grow one. Whether
    // the account plane has an email sender, a GitHub app or a Facebook app
    // switched on is that service's configuration, not this build's: it can
    // change without a release, and a flag here would be a second copy of it
    // that goes stale silently and offers or withholds a method on the strength
    // of a stale copy. Google is different only because its native flow needs a
    // server client id compiled into *this* binary, which is a fact the build
    // does own.
    //
    // The consequence is deliberate and is the reason the failure path had to
    // exist first: a provider that is off on the account plane is offered, the
    // attempt reaches it, and the refusal comes back as a failure the screen
    // states. Before this change that refusal was discarded and the screen
    // returned to signed-out, which is how a provider nobody had switched on
    // read as a button that did nothing.
    bootstrap->available_account_methods.push_back(
        service_mojom::AccountAuthMethod::kEmailLink);
    bootstrap->available_account_methods.push_back(
        service_mojom::AccountAuthMethod::kGithub);
    bootstrap->available_account_methods.push_back(
        service_mojom::AccountAuthMethod::kFacebook);
  }
  browser_profile_id_ = bootstrap->browser_profile_id;
  actor_leases_.BeginGeneration(browser_profile_id_, generation);
  capabilities_.BeginGeneration(browser_profile_id_, generation);
  value_references_.BeginGeneration(browser_profile_id_, generation);
  bootstrap->generation_capability_entropy.resize(32u);
  base::RandBytes(bootstrap->generation_capability_entropy);
  if (!HasUsableEntropy(bootstrap->generation_capability_entropy)) {
    FailBootstrapLaunch(generation);
    return;
  }
  // The asset store is walked before the core is told anything, because the
  // disk is what decides whether a transfer can resume and a record the core
  // kept would have survived a crash the file did not. The same call installs
  // what the package carries first (decision 0202), so a first run reports the
  // required artifacts as present rather than asking the core to fetch them
  // from an origin that is empty.
  asset_plane_->SeedBundledPartsAndScan(
      base::BindOnce(&CoreServiceManager::OnAssetsScanned,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(bootstrap)));
}

void CoreServiceManager::OnAssetsScanned(
    uint64_t generation,
    service_mojom::CoreBootstrapPtr bootstrap,
    std::vector<service_mojom::AssetOnDiskPtr> assets) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_ || !bootstrap) {
    ++late_reply_count_;
    return;
  }
  bootstrap->asset_platform = CurrentAssetPlatform();
  bootstrap->assets = std::move(assets);
  // The bootstrap is complete here. There used to be one more read between
  // this point and the launch — the served catalog's cache file — and it is
  // gone with the catalog (decision 0200), so the core boots on its compiled
  // baseline and the person's own layer, which is now the whole catalog.
  LaunchWithBootstrap(generation, std::move(bootstrap));
}

void CoreServiceManager::LaunchWithBootstrap(
    uint64_t generation,
    service_mojom::CoreBootstrapPtr bootstrap) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  disconnect_handled_ = false;
  teardown_reason_ = TeardownReason::kNone;
  service_.remote().reset();
  content::ServiceProcessHost::Launch(service_,
                                      content::ServiceProcessHost::Options()
                                          .WithDisplayName("Taffy core service")
                                          .Pass());
  service_.remote().set_disconnect_handler(
      base::BindOnce(&CoreServiceManager::OnMojoDisconnect,
                     weak_factory_.GetWeakPtr(), generation));

  host_receiver_.reset();
  service_.remote()->Initialize(
      std::move(bootstrap), host_receiver_.BindNewPipeAndPassRemote(),
      base::BindOnce(&CoreServiceManager::OnInitialized,
                     weak_factory_.GetWeakPtr(), generation));
  QueuePendingPageInspectorPolicies();
}

void CoreServiceManager::OnInitialized(
    uint64_t generation,
    service_mojom::CoreBootstrapResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || disconnect_handled_) {
    ++late_reply_count_;
    return;
  }

  if (!result ||
      result->status != service_mojom::InitializationStatus::kReady ||
      result->accepted_generation != generation) {
    launch_in_progress_ = false;
    teardown_reason_ = TeardownReason::kInitializationRefused;
    ResolvePendingAdmissionsUnavailable();
    ResolvePendingPolicyUnavailable();
    ResolvePendingPageInspectionsUnavailable();
    ResolvePendingPageExportsUnavailable();
    ResolvePendingCoreApiPreparations(false);
    SetAvailability(Availability::kUnavailable);
    session_.reset();
    host_receiver_.reset();
    service_.remote().reset();
    HandleDisconnect(/*unexpected=*/false);
    return;
  }

  session_.reset();
  service_.remote()->OpenSession(
      private_profile_ ? "private-profile" : "regular-profile",
      session_.BindNewPipeAndPassReceiver());
  session_.set_disconnect_handler(
      base::BindOnce(&CoreServiceManager::OnMojoDisconnect,
                     weak_factory_.GetWeakPtr(), generation));
  launch_in_progress_ = false;
  SetAvailability(Availability::kReady);
  ReplaySavedDataSnapshot();
  ResolvePendingCoreApiPreparations(true);
  CompleteRegisteredTaskSettlements();
  QueuePendingPageInspectorPolicies();
  DispatchQueuedCommands();
  // What the person set up, told to a core that has just started and holds
  // none of it (decisions 0117 and 0093). It is replayed before the first
  // surface can ask for anything, so a model call planned in this generation
  // is planned against the providers and the choice rather than against an
  // empty plane — and before the catalog poke below, because a refresh that
  // replaced the catalog while the plane held no credential would plan every
  // route as unreachable and publish that as the answer.
  ReplayProviderSetup();
  // Bootstrap planned against the core's offline default. The real link,
  // reported now, re-plans required artifacts so they start on Wi-Fi
  // without a person opening Downloads.
  ReportAssetNetwork();
  // The entitlement cadence starts the same way (decision 0082). The
  // bootstrap poke is immediate on the core's side — there is nothing held —
  // and every later tick is gated by the core's own freshness interval.
  if (entitlement_cache_) {
    entitlement_poke_timer_.Start(
        FROM_HERE, kEntitlementPokeInterval,
        base::BindRepeating(&CoreServiceManager::PokeEntitlementRefresh,
                            base::Unretained(this),
                            service_mojom::EntitlementFetchReason::kCadence));
    PokeEntitlementRefresh(service_mojom::EntitlementFetchReason::kBootstrap);
  }
}

}  // namespace taffy
