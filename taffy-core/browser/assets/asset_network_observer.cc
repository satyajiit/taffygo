// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_network_observer.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "taffy/browser/assets/asset_network_cost.h"

namespace taffy {

AssetNetworkObserver::AssetNetworkObserver(Callback callback)
    : callback_(std::move(callback)),
      runner_(base::SequencedTaskRunner::GetCurrentDefault()) {
  DCHECK(callback_);
  DCHECK(runner_);
  net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
  net::NetworkChangeNotifier::AddConnectionCostObserver(this);
}

AssetNetworkObserver::~AssetNetworkObserver() {
  net::NetworkChangeNotifier::RemoveConnectionCostObserver(this);
  net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
}

core_service::mojom::AssetNetworkCost AssetNetworkObserver::current() const {
  return MapAssetNetworkCost(net::NetworkChangeNotifier::GetConnectionType(),
                             net::NetworkChangeNotifier::GetConnectionCost());
}

void AssetNetworkObserver::Report() {
  if (runner_->RunsTasksInCurrentSequence()) {
    RunCallback(current());
    return;
  }
  runner_->PostTask(FROM_HERE,
                    base::BindOnce(&AssetNetworkObserver::RunCallback,
                                   weak_factory_.GetWeakPtr(), current()));
}

void AssetNetworkObserver::OnNetworkChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  const auto cost = MapAssetNetworkCost(
      type, net::NetworkChangeNotifier::GetConnectionCost());
  runner_->PostTask(FROM_HERE,
                    base::BindOnce(&AssetNetworkObserver::RunCallback,
                                   weak_factory_.GetWeakPtr(), cost));
}

void AssetNetworkObserver::OnConnectionCostChanged(
    net::NetworkChangeNotifier::ConnectionCost connection_cost) {
  const auto cost = MapAssetNetworkCost(
      net::NetworkChangeNotifier::GetConnectionType(), connection_cost);
  runner_->PostTask(FROM_HERE,
                    base::BindOnce(&AssetNetworkObserver::RunCallback,
                                   weak_factory_.GetWeakPtr(), cost));
}

void AssetNetworkObserver::RunCallback(
    core_service::mojom::AssetNetworkCost cost) {
  DCHECK(runner_->RunsTasksInCurrentSequence());
  callback_.Run(cost);
}

}  // namespace taffy
