// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager_test_support.h"

#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "content/public/browser/browser_context.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/assets/profile_asset_plane.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/model/profile_entitlement_cache.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "taffy/browser/model/profile_provider_listing_fetcher.h"
#include "taffy/browser/profile_model_register.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/profile_store/profile_store_reader.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy::test {

QuietManagerTail::QuietManagerTail() {
  // Every taffy profile preference, not only the filtering plane's: this pref
  // service is now also the manager's own, and a manager reading a preference
  // nobody registered would CHECK rather than find nothing.
  profile_preferences::RegisterProfilePreferences(prefs_.registry());
}

QuietManagerTail::~QuietManagerTail() = default;

std::unique_ptr<CoreServiceManager> QuietManagerTail::MakeManager(
    content::BrowserContext* context,
    std::unique_ptr<CoreStorageBroker> storage_broker,
    std::unique_ptr<ProfileToolSupervisor> tool_supervisor,
    scoped_refptr<CorePageObservationBroker> page_observation_broker,
    std::unique_ptr<CoreEffectBroker> effect_broker,
    std::unique_ptr<ProfileSavedDataBroker> saved_data_broker,
    std::unique_ptr<ProfileStoreReader> profile_store_reader) {
  auto account = std::make_unique<ProfileAccountBroker>(context);
  // No URL loader factory: this suite runs no vendor network leg, and a
  // broker with none refuses one rather than crashing.
  auto provider_auth =
      std::make_unique<ProfileProviderAuthBroker>(nullptr, account.get());
  // No asset directory and no delivery origin: a store with no root reads
  // nothing and writes nothing, and an empty origin refuses every fetch and
  // breaks nothing else.
  auto assets = std::make_unique<ProfileAssetPlane>(nullptr, base::FilePath(),
                                                    std::string());
  auto provider_listing =
      std::make_unique<ProfileProviderListingFetcher>(nullptr);
  // An absent filter list: rules compiled from nothing block nothing, which
  // is the honest posture for a suite about the manager's own sequencing.
  auto filtering = std::make_unique<filtering::FilteringRulesetService>(
      &prefs_,
      base::BindRepeating(
          [](base::OnceCallback<void(std::optional<std::string>)> reply) {
            std::move(reply).Run(std::nullopt);
          }));
  // No worker origin and no loader factory: a cache so built fails every
  // mint closed, which is the honest posture for a suite about the manager's
  // own sequencing — and the manager requires the cache exactly when the
  // account broker exists.
  auto entitlement_cache = std::make_unique<ProfileEntitlementCache>(
      nullptr, std::string(),
      base::BindRepeating(
          [](base::OnceCallback<void(std::optional<std::string>)> reply) {
            std::move(reply).Run(std::nullopt);
          }));
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  auto model_register = base::MakeRefCounted<ProfileModelRegister>(
      context->GetPath().Append(FILE_PATH_LITERAL("TaffyAssets")));
  tool_supervisor->SetResourcePorts(handles->GetResolvePort(),
                                    model_register->GetArtifactPort());
  auto tool_artifacts = std::make_unique<ProfileToolArtifactBroker>(
      tool_supervisor.get(), handles,
      context->GetPath().Append(FILE_PATH_LITERAL("TaffyToolOutputs")),
      context->IsOffTheRecord());
  return std::make_unique<CoreServiceManager>(
      context, &prefs_,
      CoreServiceManagerResources{
          .storage_broker = std::move(storage_broker),
          .model_register = std::move(model_register),
          .tool_supervisor = std::move(tool_supervisor),
          .tool_artifact_broker = std::move(tool_artifacts),
          .saved_data_broker = std::move(saved_data_broker),
          .profile_store_reader = std::move(profile_store_reader),
          .page_observation_broker = std::move(page_observation_broker),
          .effect_broker = std::move(effect_broker),
          .account_broker = std::move(account),
          .provider_auth_broker = std::move(provider_auth),
          .provider_listing_fetcher = std::move(provider_listing),
          .asset_plane = std::move(assets),
          .entitlement_cache = std::move(entitlement_cache),
          .filtering_service = std::move(filtering),
          .model_broker =
              std::make_unique<ProfileModelBroker>(nullptr, std::string()),
      });
}

}  // namespace taffy::test
