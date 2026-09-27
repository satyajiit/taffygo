// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_ASSET_NETWORK_OBSERVER_H_
#define TAFFY_BROWSER_ASSETS_ASSET_NETWORK_OBSERVER_H_

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "net/base/network_change_notifier.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Watches Chromium's notifier and reports the delivery plane's three costs.
//
// Notifications can arrive on any thread. The callback always runs on the
// sequence this object was constructed on, which is the sequence the
// CoreServiceManager lives on.
class AssetNetworkObserver
    : public net::NetworkChangeNotifier::NetworkChangeObserver,
      public net::NetworkChangeNotifier::ConnectionCostObserver {
 public:
  using Callback =
      base::RepeatingCallback<void(core_service::mojom::AssetNetworkCost)>;

  explicit AssetNetworkObserver(Callback callback);
  AssetNetworkObserver(const AssetNetworkObserver&) = delete;
  AssetNetworkObserver& operator=(const AssetNetworkObserver&) = delete;
  ~AssetNetworkObserver() override;

  // The cost right now, from the notifier, not from the last callback.
  core_service::mojom::AssetNetworkCost current() const;

  // Posts one reading. Used when the core becomes ready, so a required
  // artifact is planned against the real link rather than the offline
  // default the core boots with.
  void Report();

 private:
  void OnNetworkChanged(
      net::NetworkChangeNotifier::ConnectionType type) override;
  void OnConnectionCostChanged(
      net::NetworkChangeNotifier::ConnectionCost cost) override;
  void RunCallback(core_service::mojom::AssetNetworkCost cost);

  const Callback callback_;
  const scoped_refptr<base::SequencedTaskRunner> runner_;
  base::WeakPtrFactory<AssetNetworkObserver> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_ASSET_NETWORK_OBSERVER_H_
