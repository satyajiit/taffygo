// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/profile_core_api_facade.h"

#include <optional>
#include <utility>

#include "base/time/time.h"
#include "taffy/browser/assets/profile_asset_plane.h"

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

} // namespace

void ProfileCoreApiFacade::RequestAsset(const std::string &asset_id,
                                        const std::string &asset_revision,
                                        RequestAssetCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildRequestAsset(
                      asset_id, asset_revision,
                      manager_ ? manager_->service_generation() : 0u,
                      NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::RemoveAsset(const std::string &asset_id,
                                       const std::string &asset_revision,
                                       RemoveAssetCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildRemoveAsset(
                      asset_id, asset_revision,
                      manager_ ? manager_->service_generation() : 0u,
                      NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::SetAssetPolicy(
    core_api::mojom::AssetNetworkCostView network_cost, bool metered_permitted,
    SetAssetPolicyCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildSetAssetPolicy(
                      network_cost, metered_permitted,
                      manager_ ? manager_->service_generation() : 0u,
                      NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::ReadPartMember(const std::string &asset_id,
                                          const std::string &member_path,
                                          ReadPartMemberCallback callback) {
  namespace api = core_api::mojom;
  // Not a command, so nothing here mints an operation, no idempotency key is
  // spent and the isolated core is not told. The browser is being asked about
  // a file in the profile's own asset store, and it either has it or does not.
  if (!manager_) {
    std::move(callback).Run(api::PartMemberStatus::kNotInstalled, std::nullopt);
    return;
  }
  if (asset_id.empty() || asset_id.size() > api::kMaxIdentifierBytes ||
      member_path.empty() ||
      member_path.size() > api::kMaxPartMemberPathBytes) {
    std::move(callback).Run(api::PartMemberStatus::kInvalidRequest,
                            std::nullopt);
    return;
  }
  manager_->asset_plane().ReadMember(
      asset_id, member_path, api::kMaxPartMemberPathBytes,
      api::kMaxPartMemberBytes,
      base::BindOnce(&ProfileCoreApiFacade::OnPartMemberRead,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void ProfileCoreApiFacade::OnPartMemberRead(ReadPartMemberCallback callback,
                                            PackMemberResult result) {
  namespace api = core_api::mojom;
  switch (result.verdict) {
  case PackMemberVerdict::kOk:
    std::move(callback).Run(api::PartMemberStatus::kOk,
                            std::move(result.bytes));
    return;
  case PackMemberVerdict::kNotInstalled:
    std::move(callback).Run(api::PartMemberStatus::kNotInstalled, std::nullopt);
    return;
  case PackMemberVerdict::kNotFound:
    std::move(callback).Run(api::PartMemberStatus::kNotFound, std::nullopt);
    return;
  case PackMemberVerdict::kTooLarge:
    std::move(callback).Run(api::PartMemberStatus::kTooLarge, std::nullopt);
    return;
  case PackMemberVerdict::kUnreadable:
    std::move(callback).Run(api::PartMemberStatus::kUnreadable, std::nullopt);
    return;
  }
}

void ProfileCoreApiFacade::OnAssetProgress(const std::string &asset_id,
                                           const std::string &asset_revision,
                                           uint64_t written_bytes,
                                           uint64_t total_bytes) {
  // Straight through, with no state kept here. The figure is only worth
  // anything while it is fresh, so an observer that is not connected yet has
  // missed nothing that matters: the next snapshot carries where the transfer
  // actually got to.
  if (observer_) {
    observer_->OnAssetProgress(asset_id, asset_revision, written_bytes,
                               total_bytes);
  }
}

} // namespace taffy
