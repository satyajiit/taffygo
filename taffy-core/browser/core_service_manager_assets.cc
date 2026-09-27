// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager.h"

#include <limits>
#include <string_view>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "taffy/browser/assets/asset_network_observer.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

std::string NewAssetNetworkId(std::string_view domain) {
  return std::string(domain) + "-" +
         base::Uuid::GenerateRandomV4().AsLowercaseString();
}

}  // namespace

void CoreServiceManager::OnAssetNetworkCost(service_mojom::AssetNetworkCost cost) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_) {
    return;
  }
  if (last_asset_network_cost_ == cost) {
    return;
  }
  last_asset_network_cost_ = cost;
  if (availability_ == Availability::kStopped ||
      availability_ == Availability::kShuttingDown ||
      availability_ == Availability::kCircuitOpen) {
    return;
  }
  Submit(MakeSetAssetDeliveryPolicy(cost), base::DoNothing());
}

void CoreServiceManager::ReportAssetNetwork() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!asset_network_observer_) {
    return;
  }
  last_asset_network_cost_.reset();
  OnAssetNetworkCost(asset_network_observer_->current());
}

service_mojom::CoreServiceCommandPtr CoreServiceManager::MakeSetAssetDeliveryPolicy(
    service_mojom::AssetNetworkCost cost) const {
  const uint64_t now = NowMonotonicMillis();
  auto command = service_mojom::CoreServiceCommand::New();
  const uint64_t deadline =
      now > std::numeric_limits<uint64_t>::max() - 30'000
          ? std::numeric_limits<uint64_t>::max()
          : now + 30'000;
  command->operation = service_mojom::OperationEnvelope::New(
      NewAssetNetworkId("operation"), service_generation_, 0u, deadline,
      NewAssetNetworkId("idempotency"));
  command->kind = service_mojom::CoreServiceCommandKind::kSetAssetDeliveryPolicy;
  command->set_asset_delivery_policy = service_mojom::SetAssetDeliveryPolicyCommand::New(
      cost, asset_metered_permitted_);
  return command;
}

}  // namespace taffy
