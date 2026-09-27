// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/asset_delivery_configuration.h"

#include <string>
#include <string_view>

#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

// The longest an origin may be, which is the longest a host may be.
constexpr size_t kMaxOriginBytes = 253u;

// The longest a catalog-relative path may be. Generous for a path of four or
// five short segments, and far below anything a URL would refuse, so a value
// past it is a value that did not come from a catalog.
constexpr size_t kMaxPathBytes = 512u;

bool IsPermittedPathByte(char character) {
  return base::IsAsciiAlphaNumeric(character) || character == '.' ||
         character == '-' || character == '_' || character == '+';
}

}  // namespace

AssetDeliveryConfigurationStatus ValidateAssetDeliveryConfiguration(
    std::string_view origin) {
  if (origin.empty()) {
    return AssetDeliveryConfigurationStatus::kAbsent;
  }
  if (origin.size() > kMaxOriginBytes) {
    return AssetDeliveryConfigurationStatus::kMalformedOrigin;
  }
  const GURL parsed{origin};
  if (!parsed.is_valid() || !parsed.SchemeIs("https") || !parsed.has_host() ||
      parsed.has_username() || parsed.has_password() || parsed.has_query() ||
      parsed.has_ref()) {
    return AssetDeliveryConfigurationStatus::kMalformedOrigin;
  }
  // `GURL` normalises a bare origin's path to "/", so that and nothing else.
  if (parsed.path() != "/") {
    return AssetDeliveryConfigurationStatus::kMalformedOrigin;
  }
  return AssetDeliveryConfigurationStatus::kReady;
}

GURL AssetUrl(std::string_view origin, std::string_view catalog_path) {
  if (ValidateAssetDeliveryConfiguration(origin) !=
      AssetDeliveryConfigurationStatus::kReady) {
    return GURL();
  }
  if (catalog_path.empty() || catalog_path.size() > kMaxPathBytes) {
    return GURL();
  }
  if (catalog_path.front() == '/' || catalog_path.back() == '/') {
    return GURL();
  }
  for (const std::string_view segment :
       base::SplitStringPiece(catalog_path, "/", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_ALL)) {
    if (segment.empty() || segment == "." || segment == "..") {
      return GURL();
    }
    for (const char character : segment) {
      if (!IsPermittedPathByte(character)) {
        return GURL();
      }
    }
  }
  const GURL parsed{origin};
  const GURL resolved = parsed.Resolve(catalog_path);
  // Belt and braces. `Resolve` on a validated origin with a validated relative
  // path cannot leave the origin, and the assertion that it did not is one
  // comparison — cheaper than the argument that it could not.
  if (!resolved.is_valid() || resolved.DeprecatedGetOriginAsURL() !=
                                  parsed.DeprecatedGetOriginAsURL()) {
    return GURL();
  }
  return resolved;
}

std::string_view ConfiguredAssetOrigin() { return TAFFY_ASSET_ORIGIN; }

core_service::mojom::AssetPlatform CurrentAssetPlatform() {
#if BUILDFLAG(IS_ANDROID)
#if defined(ARCH_CPU_ARM64)
  return core_service::mojom::AssetPlatform::kAndroidArm64;
#else
  return core_service::mojom::AssetPlatform::kAndroidX64;
#endif
#elif BUILDFLAG(IS_MAC)
#if defined(ARCH_CPU_ARM64)
  return core_service::mojom::AssetPlatform::kMacosArm64;
#else
  return core_service::mojom::AssetPlatform::kMacosX64;
#endif
#elif BUILDFLAG(IS_WIN)
#if defined(ARCH_CPU_ARM64)
  return core_service::mojom::AssetPlatform::kWindowsArm64;
#else
  return core_service::mojom::AssetPlatform::kWindowsX64;
#endif
#else
  // Every other target - the Linux builder among them - has no column in the
  // catalog. Naming a real platform here would make a host build ask an origin
  // for bytes compiled for a machine it is not; `kUnsupported` is the answer
  // that says so, and no catalog row may publish for it.
  return core_service::mojom::AssetPlatform::kUnsupported;
#endif
}

}  // namespace taffy
