// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager_factory.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/asset_delivery_configuration.h"
#include "taffy/browser/assets/profile_asset_plane.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/filtering_list_reader.h"
#include "taffy/browser/model/custom_provider_endpoint_store.h"
#include "taffy/browser/model/profile_entitlement_cache.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "taffy/browser/model/profile_provider_listing_fetcher.h"
#include "taffy/browser/profile_local_model_tool_launcher.h"
#include "taffy/browser/profile_media_tool_launcher.h"
#include "taffy/browser/profile_model_register.h"
#include "taffy/browser/profile_page_media_store.h"
#include "taffy/browser/profile_python_library.h"
#include "taffy/browser/profile_python_tool_launcher.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/profile_store/profile_store_reader.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {
namespace {

bool ProfileMayOwnLiveCore(Profile* profile) {
  if (!profile) {
    return false;
  }
  // Standalone native unit fixtures construct profiles without a Chrome
  // BrowserProcess. Product profiles always have Local State, where absence
  // of the registered application pref is an unavailable security boundary.
  if (!g_browser_process) {
    return true;
  }
  PrefService* local_state = g_browser_process->local_state();
  return BackupRestoreQuarantineForProfilePath(local_state,
                                               profile->GetPath()) ==
         BackupRestoreProfileQuarantineStatus::kNotQuarantined;
}

}  // namespace

// static
CoreServiceManager* CoreServiceManagerFactory::GetForProfile(Profile* profile) {
  if (!ProfileMayOwnLiveCore(profile)) {
    return nullptr;
  }
  return static_cast<CoreServiceManager*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
CoreServiceManager* CoreServiceManagerFactory::GetForProfileIfExists(
    Profile* profile) {
  if (!ProfileMayOwnLiveCore(profile)) {
    return nullptr;
  }
  return static_cast<CoreServiceManager*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

// static
bool CoreServiceManagerFactory::HasExistingInstanceForProfile(
    Profile* profile) {
  return profile && GetInstance()->GetServiceForBrowserContext(
                        profile, /*create=*/false) != nullptr;
}

// static
CoreServiceManager* CoreServiceManagerFactory::GetForBrowserContext(
    content::BrowserContext* browser_context) {
  Profile* profile = Profile::FromBrowserContext(browser_context);
  return profile ? GetForProfile(profile) : nullptr;
}

// static
CoreServiceManagerFactory* CoreServiceManagerFactory::GetInstance() {
  static base::NoDestructor<CoreServiceManagerFactory> instance;
  return instance.get();
}

CoreServiceManagerFactory::CoreServiceManagerFactory()
    : ProfileKeyedServiceFactory(
          "TaffyCoreServiceManager",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kOwnInstance)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {
  for (KeyedServiceBaseFactory* dependency :
       ProfileSavedDataBroker::GetFactoryDependencies()) {
    DependsOn(dependency);
  }
  for (KeyedServiceBaseFactory* dependency :
       ProfileStoreReader::GetFactoryDependencies()) {
    DependsOn(dependency);
  }
}

CoreServiceManagerFactory::~CoreServiceManagerFactory() = default;

std::unique_ptr<KeyedService>
CoreServiceManagerFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!ProfileMayOwnLiveCore(profile)) {
    return nullptr;
  }
  const bool ephemeral = profile->IsOffTheRecord();
  const base::FilePath database_path =
      ephemeral ? base::FilePath()
                : profile->GetPath()
                      .Append(FILE_PATH_LITERAL("TaffyCore"))
                      .Append(FILE_PATH_LITERAL("core.sqlite3"));

  auto storage = std::make_unique<CoreStorageBroker>(database_path, ephemeral);
  // Volatile exact-page pixels have one profile owner. The observation broker
  // can mint a one-use handle, while model transport can claim it only with
  // the same task/generation and PAGE_CONTENT disclosure. Neither the handle
  // nor the bytes are part of profile storage or CoreStatus.
  auto page_media_store = base::MakeRefCounted<ProfilePageMediaStore>(
      CreateLivePageMediaDocumentValidator(profile));
  auto observation = base::MakeRefCounted<CorePageObservationBroker>(
      profile, page_media_store);
  // One store, shared by delivery and every descriptor register. Product
  // assets are profile-scoped on disk but are not browsing data.
  const base::FilePath asset_store_root =
      profile->GetPath().Append(FILE_PATH_LITERAL("TaffyAssets"));

  // Every admitted media or Python tool gets a fresh utility process. Python
  // is present only in configurations that compile its source-built
  // interpreter. The local-model selector is always present, but its default
  // construction has no runtime adapter: missing registered bytes,
  // incompatible bytes and an absent SP-08-selected adapter remain three
  // typed refusals and no refusal spends a process.
  auto media = base::MakeRefCounted<ProfileMediaToolLauncher>();
  auto local_model = base::MakeRefCounted<ProfileLocalModelToolLauncher>();
#if defined(TAFFY_ENABLE_PYTHON_RUNTIME)
  auto python = base::MakeRefCounted<ProfilePythonToolLauncher>();
  auto python_library =
      base::MakeRefCounted<ProfilePythonLibrary>(asset_store_root);
  auto tools = std::make_unique<ProfileToolSupervisor>(
      1u,
      ProfileToolSupervisor::PythonPorts(python->GetStartPort(),
                                         python->GetCancelPort()),
      ProfileToolSupervisor::LocalModelPorts(local_model->GetStartPort(),
                                             local_model->GetCancelPort()),
      ProfileToolSupervisor::MediaPorts(media->GetStartPort(),
                                        media->GetCancelPort()));
#else
  scoped_refptr<ProfilePythonLibrary> python_library;
  auto tools = std::make_unique<ProfileToolSupervisor>(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts(local_model->GetStartPort(),
                                             local_model->GetCancelPort()),
      ProfileToolSupervisor::MediaPorts(media->GetStartPort(),
                                        media->GetCancelPort()));
#endif
  if (python_library) {
    tools->SetPythonLibraryPort(python_library->GetOpenPort());
  }
  // One store, two readers: the delivery plane writes this directory and the
  // model register reads it. The path is named once rather than at each of
  // them, because two literals that have to agree is exactly how a register
  // ends up opening exact revisions in an empty directory beside a full one.
  //
  // It is deliberately not partitioned by `IsOffTheRecord`. A private window
  // is still the product, and the product's own artifacts are not the person's
  // data: they are the same bytes for every device, fetched from an origin the
  // build pinned, recorded nowhere. Giving a private window a different set of
  // installed artifacts would be a behaviour difference with nothing behind
  // it.
  // The two resource ports. They are installed for every profile, including
  // this one. What they replace is the state in which a job that named a
  // resource could not have been refused for a stated reason, because there
  // was no browser side to refuse it.
  //
  // The register reads the asset store and never writes it: the delivery plane
  // is the only thing that puts first-party bytes on a device (decision 0060),
  // and a second writer of this directory would be a second answer to a
  // question that already has one.
  auto models = base::MakeRefCounted<ProfileModelRegister>(asset_store_root);
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  tools->SetResourcePorts(handles->GetResolvePort(), models->GetArtifactPort());
  auto tool_artifacts = std::make_unique<ProfileToolArtifactBroker>(
      tools.get(), handles,
      profile->GetPath().Append(FILE_PATH_LITERAL("TaffyToolOutputs")),
      ephemeral);
  auto saved_data = ProfileSavedDataBroker::Create(profile);
  auto profile_store = ProfileStoreReader::Create(profile);
  std::unique_ptr<ProfileAccountBroker> account;
  std::unique_ptr<ProfileProviderAuthBroker> provider_auth;
  if (!ephemeral) {
    account = std::make_unique<ProfileAccountBroker>(profile);
    // The provider sign-in's network half (decision 0081) travels with the
    // account plane: its Custom Tab, one-shot vault and sealed record store
    // all ride the same Android adapter the account broker owns.
    provider_auth = std::make_unique<ProfileProviderAuthBroker>(
        profile->GetURLLoaderFactory(), account.get());
  }
  // The delivery plane is installed for every profile, private ones included,
  // for the reason the store root above gives.
  auto assets = std::make_unique<ProfileAssetPlane>(
      profile->GetURLLoaderFactory(), asset_store_root,
      std::string(ConfiguredAssetOrigin()));
  // The provider listing fetcher (decision 0098). It was the authenticated
  // twin of an anonymous catalog fetcher that stood here and has gone with
  // the served catalog (decision 0200); what it does is unchanged.
  // It has one compiled provider/family route and receives secret material
  // only through the manager-owned resolver; the sandbox supplies neither a
  // path nor a credential.
  auto provider_listing = std::make_unique<ProfileProviderListingFetcher>(
      profile->GetURLLoaderFactory());
  // The filtering plane's lifecycle service (decision 0076). It reads the
  // filter-list pack through an injected reader over the delivery plane's
  // store rather than the plane itself, because the filtering component sits
  // below this host in the layer graph. Installed for every profile for the
  // same reason the plane is: the rules are the product's bytes, not the
  // person's data, and a private window is still the product.
  auto filtering = std::make_unique<filtering::FilteringRulesetService>(
      profile->GetPrefs(), MakeFilterListReader(assets.get()));
  // The installed-set observer refreshes readers that are not core state.
  // Model custody is refreshed by the complete typed CoreStateUpdate snapshot
  // emitted after delivery settles, which avoids rehashing every installed
  // model when an unrelated flag, filter or Python asset changes.
  assets->SetInstalledObserver(base::BindRepeating(
      [](ProfilePythonLibrary* python_library,
         base::WeakPtr<filtering::FilteringRulesetService> filtering,
         std::string_view asset_id) {
        if (python_library && asset_id == "python-stdlib") {
          python_library->Refresh();
        }
        if (filtering && asset_id == "easylist-base") {
          filtering->ReloadRuleset();
        }
      },
      base::Unretained(python_library.get()), filtering->GetWeakPtr()));
  // The browser-process half of a model call. Created for every profile,
  // private ones included: a person browsing privately still asks Taffy
  // things, and the broker holds nothing that outlives the profile.
  //
  // Its credential resolver is installed by the manager's constructor, not
  // here: the subscription-aware ProviderAccessResolver (decision 0081)
  // rides the account and provider-auth brokers, and the manager is where
  // those three lifetimes already live together. Private and off-the-record
  // profiles have no account broker and therefore no resolver — a handle on
  // those profiles stays unavailable.
  auto model_broker = std::make_unique<ProfileModelBroker>(
      profile->GetURLLoaderFactory(), TAFFY_MANAGED_WORKER_ORIGIN);
  // The register of decision 0096, and the only thing that may accept a model
  // request naming a person's own server. It is installed here rather than in
  // the manager's constructor because what it reads is the profile's own
  // preference file and nothing else — no account plane, no vault, no second
  // broker — so it exists on a private profile exactly as it does on any
  // other.
  model_broker->SetRegisteredEndpointLookup(
      CustomProviderEndpointLookup(profile->GetPrefs()));
  // The managed entitlement's browser custody (decision 0082). It travels
  // with the account plane — a mint is the signed-in session's act — so a
  // private profile gets none and the managed route stays refused there. The
  // access-token read rides Unretained because the manager owns both ends
  // and destroys the cache before the broker, by its member order.
  std::unique_ptr<ProfileEntitlementCache> entitlement_cache;
  if (account) {
    entitlement_cache = std::make_unique<ProfileEntitlementCache>(
        profile->GetURLLoaderFactory(), TAFFY_MANAGED_WORKER_ORIGIN,
        base::BindRepeating(&ProfileAccountBroker::ReadCanonicalAccessToken,
                            base::Unretained(account.get())));
  }
  CoreEffectBroker::Handlers handlers;
  handlers.load_bootstrap = base::BindRepeating(
      &CoreStorageBroker::LoadBootstrap, base::Unretained(storage.get()));
  handlers.commit_intent = base::BindRepeating(&CoreStorageBroker::CommitIntent,
                                               base::Unretained(storage.get()));
  handlers.commit_result = base::BindRepeating(&CoreStorageBroker::CommitResult,
                                               base::Unretained(storage.get()));
  handlers.storage = base::BindRepeating(&CoreStorageBroker::DispatchStorage,
                                         base::Unretained(storage.get()));
  handlers.observation = base::BindRepeating(
      &CorePageObservationBroker::Dispatch, base::RetainedRef(observation));
  handlers.model = base::BindRepeating(&ProfileModelBroker::Dispatch,
                                       base::Unretained(model_broker.get()));
  // The tool seam. Without this line every tool job reached the broker,
  // journalled its intent, and came back UNAVAILABLE through the path a
  // deliberate refusal takes — which is indistinguishable from a decision that
  // this product does not run tools. The supervisor above decides what is
  // actually runnable, and says so per runtime: media starts everywhere,
  // Python starts only in the interpreter-bearing Android graph, and the local
  // model remains unsupported until a runtime is selected.
  handlers.tool = base::BindRepeating(&ProfileToolSupervisor::Start,
                                      base::Unretained(tools.get()));
  // Three adapters own work a cancelled task must stop, and the broker's
  // handler is one callback. A model call is the expensive half: an
  // observation that outlives its task wastes a renderer round trip, while a
  // model call that does runs to completion and is billed against a task
  // nobody is waiting for. A tool job is the long half: its worker holds a
  // process, a memory budget and the descriptors the browser opened for it
  // until something kills it.
  handlers.cancel_task = base::BindRepeating(
      [](CorePageObservationBroker* observation, ProfileModelBroker* models,
         ProfileToolSupervisor* tool_jobs, std::string_view task_id,
         uint64_t generation) {
        observation->CancelTask(task_id, generation);
        models->CancelTask(task_id, generation);
        tool_jobs->CancelTask(task_id, generation);
      },
      base::RetainedRef(observation), base::Unretained(model_broker.get()),
      base::Unretained(tools.get()));
  if (account) {
    handlers.secure_store =
        base::BindRepeating(&ProfileAccountBroker::DispatchSecureStore,
                            base::Unretained(account.get()));
    handlers.auth_surface =
        base::BindRepeating(&ProfileAccountBroker::DispatchAuthSurface,
                            base::Unretained(account.get()));
    handlers.network =
        base::BindRepeating(&ProfileAccountBroker::DispatchNetwork,
                            base::Unretained(account.get()));
  }
  handlers.asset_delivery = base::BindRepeating(&ProfileAssetPlane::Dispatch,
                                                base::Unretained(assets.get()));
  handlers.cancel_generation = base::BindRepeating(
      [](CoreStorageBroker* storage, ProfileAccountBroker* account,
         CorePageObservationBroker* observation, ProfileModelBroker* models,
         uint64_t generation, std::vector<std::string> effect_ids) {
        storage->MarkEffectsLost(generation, std::move(effect_ids));
        if (account) {
          account->CancelGeneration(generation);
        }
        observation->CancelGeneration(generation);
        models->CancelGeneration(generation);
      },
      base::Unretained(storage.get()), base::Unretained(account.get()),
      base::RetainedRef(observation), base::Unretained(model_broker.get()));
  auto effects = std::make_unique<CoreEffectBroker>(std::move(handlers));
  return std::make_unique<CoreServiceManager>(
      profile, profile->GetPrefs(),
      CoreServiceManagerResources{
          .storage_broker = std::move(storage),
          .model_register = std::move(models),
          .tool_supervisor = std::move(tools),
          .tool_artifact_broker = std::move(tool_artifacts),
          .saved_data_broker = std::move(saved_data),
          .profile_store_reader = std::move(profile_store),
          .page_observation_broker = std::move(observation),
          .effect_broker = std::move(effects),
          .account_broker = std::move(account),
          .provider_auth_broker = std::move(provider_auth),
          .provider_listing_fetcher = std::move(provider_listing),
          .asset_plane = std::move(assets),
          .entitlement_cache = std::move(entitlement_cache),
          .filtering_service = std::move(filtering),
          .model_broker = std::move(model_broker),
      });
}

}  // namespace taffy
