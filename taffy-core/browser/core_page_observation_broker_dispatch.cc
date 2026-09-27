// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/profile_page_media_store.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

namespace core_mojom = core_service::mojom;

CorePageObservationBroker::CorePageObservationBroker(
    content::BrowserContext* browser_context)
    : CorePageObservationBroker(
          browser_context,
          base::MakeRefCounted<ProfilePageMediaStore>(
              CreateLivePageMediaDocumentValidator(browser_context)),
          base::BindRepeating(
              &CoreServiceManagerFactory::GetForBrowserContext)) {}

CorePageObservationBroker::CorePageObservationBroker(
    content::BrowserContext* browser_context,
    scoped_refptr<ProfilePageMediaStore> page_media_store)
    : CorePageObservationBroker(
          browser_context,
          std::move(page_media_store),
          base::BindRepeating(
              &CoreServiceManagerFactory::GetForBrowserContext)) {}

CorePageObservationBroker::CorePageObservationBroker(
    content::BrowserContext* browser_context,
    ManagerLookup manager_lookup)
    : CorePageObservationBroker(
          browser_context,
          base::MakeRefCounted<ProfilePageMediaStore>(
              CreateLivePageMediaDocumentValidator(browser_context)),
          std::move(manager_lookup)) {}

CorePageObservationBroker::CorePageObservationBroker(
    content::BrowserContext* browser_context,
    scoped_refptr<ProfilePageMediaStore> page_media_store,
    ManagerLookup manager_lookup)
    : browser_context_(browser_context),
      page_media_store_(std::move(page_media_store)),
      manager_lookup_(std::move(manager_lookup)) {
  CHECK(browser_context_);
  CHECK(page_media_store_);
  CHECK(manager_lookup_);
}

CorePageObservationBroker::~CorePageObservationBroker() = default;

CoreServiceManager* CorePageObservationBroker::LookupManager() const {
  return manager_lookup_.Run(browser_context_);
}

scoped_refptr<ProfilePageMediaStore>
CorePageObservationBroker::page_media_store() const {
  return page_media_store_;
}

core_api::mojom::PageInspectorSnapshotViewPtr
CorePageObservationBroker::TakeDirectProjection(const std::string& effect_id) {
  auto it = direct_projections_.find(effect_id);
  if (it == direct_projections_.end()) {
    return nullptr;
  }
  core_api::mojom::PageInspectorSnapshotViewPtr projection =
      std::move(it->second);
  direct_projections_.erase(it);
  return projection;
}

void CorePageObservationBroker::Dispatch(core_mojom::EffectEnvelopePtr effect,
                                         CompletionCallback callback) {
  DispatchInternal(std::move(effect), std::move(callback), true);
}

void CorePageObservationBroker::DispatchForExport(
    core_mojom::EffectEnvelopePtr effect,
    CompletionCallback callback) {
  DispatchInternal(std::move(effect), std::move(callback), false);
}

void CorePageObservationBroker::DispatchInternal(
    core_mojom::EffectEnvelopePtr effect,
    CompletionCallback callback,
    bool create_direct_projection) {
  if (!effect || !effect->operation || !effect->page_observation ||
      effect->kind != core_mojom::EffectKind::kPageObservation ||
      effect->page_observation->scope !=
          core_mojom::ObservationScope::kCurrentDocument ||
      effect->page_observation->max_bytes == 0u ||
      effect->page_observation->max_bytes > core_mojom::kMaxEffectBytes) {
    if (effect && effect->operation) {
      std::move(callback).Run(MakeUnavailable(*effect));
    } else {
      std::move(callback).Run(nullptr);
    }
    return;
  }

  CoreServiceManager* manager = LookupManager();
  if (!manager) {
    std::move(callback).Run(MakeUnavailable(*effect));
    return;
  }

  const std::string effect_id = effect->effect_id;
  const core_mojom::PageObservationEffect& requested =
      *effect->page_observation;
  auto [insert_entry, inserted] = pending_observations_.emplace(
      effect_id, PendingObservation{
                     requested.task_id, effect->operation->service_generation,
                     requested.tab_id, RequestId{}, std::nullopt});
  if (!inserted) {
    std::move(callback).Run(MakeUnavailable(*effect));
    return;
  }

  core_mojom::EffectEnvelopePtr retained = effect.Clone();
  const RequestId request_id = ObserveTabForCore(
      browser_context_, *effect->page_observation, *manager->actor_leases(),
      *manager->capabilities(), &insert_entry->second.authorized_target,
      base::BindOnce(&CorePageObservationBroker::OnObservation,
                     weak_factory_.GetWeakPtr(), std::move(retained),
                     std::move(callback), create_direct_projection));
  auto pending = pending_observations_.find(effect_id);
  if (pending != pending_observations_.end()) {
    if (!request_id.is_valid()) {
      pending_observations_.erase(pending);
      return;
    }
    pending->second.request_id = request_id;
  }
}

}  // namespace taffy
