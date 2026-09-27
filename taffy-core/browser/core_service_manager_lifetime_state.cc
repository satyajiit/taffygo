// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager_lifetime_state.h"

#include <utility>

#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/assets/asset_network_observer.h"
#include "taffy/browser/assets/profile_asset_plane.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/browser/model/profile_entitlement_cache.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "taffy/browser/model/profile_provider_listing_fetcher.h"
#include "taffy/browser/profile_model_register.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_access_resolver.h"
#include "taffy/browser/profile_store/profile_store_reader.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {

CoreServiceManagerLifetimeState::CoreServiceManagerLifetimeState(
    CoreServiceManager* owner,
    core_service::mojom::CoreHost* core_host,
    content::BrowserContext* browser_context,
    PrefService* profile_prefs,
    bool private_profile,
    CoreServiceManagerResources resources,
    std::string browser_session_id)
    : browser_context_(browser_context),
      profile_prefs_(profile_prefs),
      private_profile_(private_profile),
      storage_broker_(std::move(resources.storage_broker)),
      model_register_(std::move(resources.model_register)),
      tool_supervisor_(std::move(resources.tool_supervisor)),
      tool_artifact_broker_(std::move(resources.tool_artifact_broker)),
      saved_data_broker_(std::move(resources.saved_data_broker)),
      profile_store_reader_(std::move(resources.profile_store_reader)),
      page_observation_broker_(std::move(resources.page_observation_broker)),
      account_broker_(std::move(resources.account_broker)),
      provider_auth_broker_(std::move(resources.provider_auth_broker)),
      provider_listing_fetcher_(std::move(resources.provider_listing_fetcher)),
      asset_plane_(std::move(resources.asset_plane)),
      entitlement_cache_(std::move(resources.entitlement_cache)),
      filtering_service_(std::move(resources.filtering_service)),
      model_broker_(std::move(resources.model_broker)),
      effect_broker_(std::move(resources.effect_broker)),
      host_receiver_(core_host),
      backup_protocol_(std::make_unique<CoreBackupProtocol>(owner)),
      site_skill_offers_(private_profile),
      browser_session_id_(std::move(browser_session_id)),
      weak_factory_(owner) {}

CoreServiceManagerLifetimeState::~CoreServiceManagerLifetimeState() = default;

void CoreServiceManagerLifetimeState::RevokePreapprovedFormActionsForTask(
    const std::string& task_id) {
  for (auto it = preapproved_form_actions_.begin();
       it != preapproved_form_actions_.end();) {
    if (it->first.first == task_id) {
      it = preapproved_form_actions_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace taffy
