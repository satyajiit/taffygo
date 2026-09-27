// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/core_service_impl.h"

#include <utility>

#include "base/functional/bind.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace mojom = core_service::mojom;

// The poll-and-deliver plane: the managed entitlement (decision 0082). The
// browser asks whether anything is due, carries out at most one fetch, and
// hands the outcome back, and the ordered core alone decides what an outcome
// means. The one bit each delivery answers with is bookkeeping for the caller;
// everything a surface draws rides the state that publishes beside it.
//
// This file held two planes run on the same protocol. The other was the served
// catalog, and decision 0200 removed it along with the host that served it.

void CoreServiceImpl::PlanEntitlementRefresh(
    mojom::EntitlementFetchReason reason,
    PlanEntitlementRefreshCallback callback) {
  if (!ready_ || shutdown_started_) {
    std::move(callback).Run(nullptr);
    return;
  }
  // Planning changes no published state, so the envelope travels straight
  // back on the reply; there is no batch to publish.
  core_.AsyncCall(&RustCore::PlanEntitlementRefresh)
      .WithArgs(reason)
      .Then(std::move(callback));
}

void CoreServiceImpl::DeliverEntitlementFetchResult(
    mojom::EffectResultPtr result,
    DeliverEntitlementFetchResultCallback callback) {
  if (!ready_ || shutdown_started_ || !result || !result->operation ||
      result->operation->service_generation != generation_) {
    std::move(callback).Run(false);
    return;
  }
  core_.AsyncCall(&RustCore::DeliverEntitlementFetchResult)
      .WithArgs(std::move(result))
      .Then(base::BindOnce(&CoreServiceImpl::OnEntitlementFetchDelivered,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CoreServiceImpl::OnEntitlementFetchDelivered(
    DeliverEntitlementFetchResultCallback callback,
    CoreEntitlementDeliveryBatch delivered) {
  PublishBatch(std::move(delivered.batch));
  std::move(callback).Run(delivered.installed);
}

} // namespace taffy
