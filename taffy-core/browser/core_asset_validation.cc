// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_asset_validation.h"

#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

// The path a fetch names, as a sequence of segments and nothing else.
//
// This is a subset of what `AssetUrl` accepts, and it is checked twice on
// purpose: here so a malformed proposal is refused before an adapter is
// entered, and there so that the URL a request is actually built from was
// resolved rather than assumed. Neither check trusts the other's verdict.
bool IsRelativePath(const std::string& value, size_t maximum) {
  if (value.empty() || value.size() > maximum || value.front() == '/' ||
      value.back() == '/') {
    return false;
  }
  size_t segment_length = 0;
  for (char character : value) {
    if (character == '/') {
      if (segment_length == 0) {
        return false;  // An empty segment: `a//b`, which resolves elsewhere.
      }
      segment_length = 0;
      continue;
    }
    const bool permitted = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') ||
                           character == '.' || character == '-' ||
                           character == '_';
    if (!permitted) {
      return false;
    }
    segment_length++;
  }
  return segment_length != 0;
}

bool IsValidFetch(const mojom::AssetFetchRequest& fetch,
                  size_t max_identifier_bytes) {
  return IsIdentifier(fetch.asset_id, max_identifier_bytes) &&
         IsIdentifier(fetch.asset_revision, max_identifier_bytes) &&
         IsRelativePath(fetch.origin_path, mojom::kMaxAssetPathBytes) &&
         fetch.total_bytes > 0u &&
         fetch.total_bytes <= mojom::kMaxAssetTransferBytes &&
         fetch.offset_bytes < fetch.total_bytes;
}

bool IsValidRemove(const mojom::AssetRemoveRequest& remove,
                   size_t max_identifier_bytes) {
  return IsIdentifier(remove.asset_id, max_identifier_bytes) &&
         IsIdentifier(remove.asset_revision, max_identifier_bytes);
}

// Whether a report names the asset the effect asked about.
bool Answers(const std::string& asset_id,
             const std::string& asset_revision,
             const std::string& expected_id,
             const std::string& expected_revision) {
  return asset_id == expected_id && asset_revision == expected_revision;
}

}  // namespace

bool IsValidCoreAssetDelivery(const mojom::AssetDeliveryEffect& effect,
                              size_t max_identifier_bytes) {
  switch (effect.operation_kind) {
    case mojom::AssetDeliveryOperation::kFetchAsset:
      return effect.fetch && !effect.remove &&
             IsValidFetch(*effect.fetch, max_identifier_bytes);
    case mojom::AssetDeliveryOperation::kRemoveAsset:
      return effect.remove && !effect.fetch &&
             IsValidRemove(*effect.remove, max_identifier_bytes);
  }
  return false;
}

bool IsValidCoreAssetDeliveryResult(
    const mojom::AssetDeliveryEffectResult& result,
    const mojom::AssetDeliveryEffect& effect) {
  if (result.operation_kind != effect.operation_kind) {
    return false;
  }
  switch (effect.operation_kind) {
    case mojom::AssetDeliveryOperation::kFetchAsset:
      // `observed_bytes` is what the adapter measured on disk and
      // `written_bytes` is what it wrote; neither may exceed the length the
      // catalog named, because a longer file is not the artifact.
      return result.transfer && !result.removal && effect.fetch &&
             Answers(result.transfer->asset_id, result.transfer->asset_revision,
                     effect.fetch->asset_id, effect.fetch->asset_revision) &&
             result.transfer->written_bytes <= effect.fetch->total_bytes &&
             result.transfer->observed_bytes <= effect.fetch->total_bytes;
    case mojom::AssetDeliveryOperation::kRemoveAsset:
      return result.removal && !result.transfer && effect.remove &&
             Answers(result.removal->asset_id, result.removal->asset_revision,
                     effect.remove->asset_id, effect.remove->asset_revision);
  }
  return false;
}

}  // namespace taffy
