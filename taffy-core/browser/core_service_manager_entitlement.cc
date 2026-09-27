// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager.h"

#include <utility>

#include "base/functional/bind.h"
#include "taffy/browser/model/profile_entitlement_cache.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

// The managed entitlement's one browser cycle (decision 0082): poke, mint,
// deliver. Every decision in the cycle is the core's — whether a fetch is
// warranted for the reason given, and what the delivered summary means. This
// file moves a summary; the token the same mint produced never appears in it,
// because it lives in the entitlement cache and nothing here has a field to
// carry it.
void CoreServiceManager::PokeEntitlementRefresh(
    service_mojom::EntitlementFetchReason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || availability_ != Availability::kReady ||
      !session_.is_bound() || entitlement_fetch_in_flight_ ||
      !entitlement_cache_) {
    return;
  }
  entitlement_fetch_in_flight_ = true;
  RefreshIdleTeardown();
  session_->PlanEntitlementRefresh(
      reason,
      base::BindOnce(&CoreServiceManager::OnEntitlementRefreshPlanned,
                     weak_factory_.GetWeakPtr(), service_generation_));
}

void CoreServiceManager::OnEntitlementRefreshPlanned(
    uint64_t generation, service_mojom::EffectEnvelopePtr effect) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_ ||
      !entitlement_cache_) {
    ++late_reply_count_;
    entitlement_fetch_in_flight_ = false;
    RefreshIdleTeardown();
    return;
  }
  if (!effect) {
    // Not warranted, or the core already has one in flight. Nothing to do
    // until the next poke.
    entitlement_fetch_in_flight_ = false;
    RefreshIdleTeardown();
    return;
  }
  // The envelope stays here so the delivery can answer under the identity
  // the core planned; the cache is asked only for the summary.
  entitlement_cache_->FetchSummary(
      base::BindOnce(&CoreServiceManager::OnEntitlementMinted,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(effect)));
}

void CoreServiceManager::OnEntitlementMinted(
    uint64_t generation, service_mojom::EffectEnvelopePtr effect,
    service_mojom::EntitlementSummaryResultPtr summary) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || shutdown_started_ ||
      availability_ != Availability::kReady || !session_.is_bound() ||
      !effect || !effect->operation) {
    ++late_reply_count_;
    entitlement_fetch_in_flight_ = false;
    RefreshIdleTeardown();
    return;
  }
  auto result = service_mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->kind = service_mojom::EffectKind::kNetworkRequest;
  result->network = service_mojom::NetworkEffectResult::New();
  result->network->operation_kind =
      service_mojom::AccountNetworkOperation::kFetchEntitlement;
  if (summary) {
    // A definitive answer, a definitive absence included: the summary is the
    // whole of it, and COMPLETED says the worker was heard.
    result->status = service_mojom::EffectStatus::kCompleted;
    result->network->entitlement_summary = std::move(summary);
  } else {
    // The worker was not definitively heard. The core keeps its last state,
    // and a later poke asks again.
    result->status = service_mojom::EffectStatus::kUnavailable;
  }
  session_->DeliverEntitlementFetchResult(
      std::move(result),
      base::BindOnce(&CoreServiceManager::OnEntitlementDeliveryAcknowledged,
                     weak_factory_.GetWeakPtr(), generation));
}

void CoreServiceManager::OnEntitlementDeliveryAcknowledged(uint64_t generation,
                                                           bool installed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  entitlement_fetch_in_flight_ = false;
  RefreshIdleTeardown();
  if (generation != service_generation_ || shutdown_started_) {
    ++late_reply_count_;
    return;
  }
  // `installed` is bookkeeping alone: everything a surface draws rode the
  // published status the delivery produced. Nothing here persists — the
  // summary is process state on both sides by design.
  (void)installed;
}

} // namespace taffy
