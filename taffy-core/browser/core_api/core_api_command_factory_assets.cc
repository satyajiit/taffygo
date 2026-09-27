// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <optional>
#include <utility>

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

// An identity a surface may name.
//
// Bounded here as well as in the isolated core, because a string this long is
// refused before it is put in a message rather than after it has crossed a
// process boundary. The core refuses it too; neither check is the other's
// excuse.
bool IsIdentifier(const std::string &value) {
  return !value.empty() && value.size() <= api::kMaxIdentifierBytes;
}

std::optional<service::AssetNetworkCost>
ProjectNetworkCost(api::AssetNetworkCostView cost) {
  switch (cost) {
  case api::AssetNetworkCostView::kOffline:
    return service::AssetNetworkCost::kOffline;
  case api::AssetNetworkCostView::kMetered:
    return service::AssetNetworkCost::kMetered;
  case api::AssetNetworkCostView::kUnmetered:
    return service::AssetNetworkCost::kUnmetered;
  }
  return std::nullopt;
}

} // namespace

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRequestAsset(std::string asset_id,
                                         std::string asset_revision,
                                         uint64_t service_generation,
                                         uint64_t now_monotonic_ms) {
  if (!IsIdentifier(asset_id) || !IsIdentifier(asset_revision)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRequestAsset;
  core_command->request_asset =
      api::RequestAssetBody::New(asset_id, asset_revision);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kRequestAsset;
  service_command->request_asset = service::RequestAssetCommand::New(
      std::move(asset_id), std::move(asset_revision));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRemoveAsset(std::string asset_id,
                                        std::string asset_revision,
                                        uint64_t service_generation,
                                        uint64_t now_monotonic_ms) {
  if (!IsIdentifier(asset_id) || !IsIdentifier(asset_revision)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRemoveAsset;
  core_command->remove_asset =
      api::RemoveAssetBody::New(asset_id, asset_revision);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kRemoveAsset;
  service_command->remove_asset = service::RemoveAssetCommand::New(
      std::move(asset_id), std::move(asset_revision));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildSetAssetPolicy(
    api::AssetNetworkCostView network_cost, bool metered_permitted,
    uint64_t service_generation, uint64_t now_monotonic_ms) {
  const std::optional<service::AssetNetworkCost> service_cost =
      ProjectNetworkCost(network_cost);
  if (!service_cost) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSetAssetPolicy;
  core_command->set_asset_policy =
      api::SetAssetPolicyBody::New(network_cost, metered_permitted);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kSetAssetDeliveryPolicy;
  service_command->set_asset_delivery_policy =
      service::SetAssetDeliveryPolicyCommand::New(*service_cost,
                                                  metered_permitted);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

} // namespace taffy
