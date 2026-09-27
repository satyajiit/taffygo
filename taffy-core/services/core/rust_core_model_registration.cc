// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_model_registration.h"

#include <optional>
#include <set>
#include <string>
#include <utility>

namespace taffy::core_service_internal {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

std::optional<mojom::AssetKind> ModelAssetKindFromWire(uint8_t value) {
  switch (value) {
    case 2:
      return mojom::AssetKind::kModelWeights;
    case 3:
      return mojom::AssetKind::kModelTokenizer;
    default:
      return std::nullopt;
  }
}

std::optional<mojom::ToolModelArtifactKind> ModelFormatFromWire(
    uint8_t value) {
  switch (value) {
    case 0:
      return mojom::ToolModelArtifactKind::kLitertTflite;
    case 1:
      return mojom::ToolModelArtifactKind::kOnnxRuntime;
    case 2:
      return mojom::ToolModelArtifactKind::kGguf;
    default:
      return std::nullopt;
  }
}

bool IsValidIdentity(const rust::String& value, uint64_t limit) {
  return !value.empty() && value.size() <= limit;
}

}  // namespace

bool PopulateModelArtifactRegistrations(
    const rust::Vec<bridge::BridgeModelArtifactRegistration>& artifacts,
    mojom::CoreStateUpdate* state) {
  if (!state || artifacts.size() > mojom::kMaxRegisteredModelArtifacts) {
    return false;
  }
  std::set<std::pair<std::string, std::string>> identities;
  for (const bridge::BridgeModelArtifactRegistration& artifact : artifacts) {
    const std::optional<mojom::AssetKind> asset_kind =
        ModelAssetKindFromWire(artifact.asset_kind);
    const std::optional<mojom::ToolModelArtifactKind> format =
        ModelFormatFromWire(artifact.format);
    if (!asset_kind || !format ||
        !IsValidIdentity(artifact.asset_id, mojom::kMaxToolModelIdBytes) ||
        !IsValidIdentity(artifact.asset_revision,
                         mojom::kMaxToolModelRevisionBytes) ||
        artifact.byte_length == 0u ||
        artifact.byte_length > mojom::kMaxToolModelArtifactBytes ||
        (*asset_kind == mojom::AssetKind::kModelTokenizer &&
         artifact.adapter) ||
        !identities
             .emplace(std::string(artifact.asset_id),
                      std::string(artifact.asset_revision))
             .second) {
      return false;
    }
    auto projected = mojom::ModelArtifactRegistration::New();
    projected->asset_id = std::string(artifact.asset_id);
    projected->asset_revision = std::string(artifact.asset_revision);
    projected->asset_kind = *asset_kind;
    projected->format = *format;
    projected->adapter = artifact.adapter;
    projected->byte_length = artifact.byte_length;
    projected->digest.assign(artifact.digest.begin(), artifact.digest.end());
    state->model_artifacts.push_back(std::move(projected));
  }
  return true;
}

}  // namespace taffy::core_service_internal
