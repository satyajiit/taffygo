// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/task_consent_shape.h"

#include <string_view>

namespace taffy {
namespace {

namespace service = core_service::mojom;

std::optional<std::string_view> RouteId(service::TaskProviderRoute route) {
  switch (route) {
    case service::TaskProviderRoute::kDirectUserKey:
      return "direct_user_key";
    case service::TaskProviderRoute::kManagedService:
      return "managed_service";
    case service::TaskProviderRoute::kNoModelRequired:
      return "no_model_required";
    case service::TaskProviderRoute::kNotConfigured:
      return std::nullopt;
  }
  return std::nullopt;
}

bool IsModelRoute(service::TaskProviderRoute route) {
  return route == service::TaskProviderRoute::kDirectUserKey ||
         route == service::TaskProviderRoute::kManagedService;
}

bool IsPageScopedShape(const service::TaskConsentPreview& preview,
                       size_t minimum_sources,
                       bool permits_no_model) {
  const size_t source_count = preview.sources.size();
  const bool route_admitted =
      IsModelRoute(preview.provider_route) ||
      (permits_no_model &&
       preview.provider_route == service::TaskProviderRoute::kNoModelRequired);
  return route_admitted && source_count >= minimum_sources &&
         source_count <= kMaxSelectedTaskSources &&
         !preview.source_discovery_enabled && preview.new_source_cap == 0u;
}

bool IsInitialErrandShape(const service::TaskConsentPreview& preview) {
  return IsModelRoute(preview.provider_route) && preview.sources.size() <= 1u &&
         preview.source_discovery_enabled && preview.new_source_cap >= 1u &&
         preview.new_source_cap <= kMaxErrandNewSourceCap;
}

bool IsDurableErrandShape(const service::TaskConsentPreview& preview) {
  const size_t source_count = preview.sources.size();
  return IsModelRoute(preview.provider_route) &&
         preview.source_discovery_enabled &&
         source_count <= 1u + kMaxErrandNewSourceCap &&
         preview.new_source_cap <= kMaxErrandNewSourceCap &&
         source_count + preview.new_source_cap <=
             1u + kMaxErrandNewSourceCap;
}

}  // namespace

bool IsAdmittedInitialTaskConsentShape(
    service::TaskTemplateId template_id,
    const service::TaskConsentPreview& preview,
    const std::optional<std::string>& provider_route_id,
    const std::optional<std::string>& skill_version_id) {
  const std::optional<std::string_view> expected =
      RouteId(preview.provider_route);
  if (!expected || !provider_route_id || *provider_route_id != *expected) {
    return false;
  }

  switch (template_id) {
    case service::TaskTemplateId::kBuildSourceTable:
      return IsPageScopedShape(preview, 1u, true) &&
             preview.sources.size() == 1u;
    case service::TaskTemplateId::kCompareProducts:
      return IsPageScopedShape(preview, 2u, false);
    case service::TaskTemplateId::kSummarizeEvidence:
      return IsPageScopedShape(preview, 1u, false);
    case service::TaskTemplateId::kWebErrand:
      if (preview.provider_route == service::TaskProviderRoute::kNoModelRequired) {
        return skill_version_id && !skill_version_id->empty() &&
               skill_version_id->size() <= service::kMaxIdentifierBytes &&
               IsPageScopedShape(preview, 1u, true) &&
               preview.sources.size() == 1u;
      }
      return IsInitialErrandShape(preview);
  }
  return false;
}

bool IsAdmittedDurableTaskConsentShape(
    const service::TaskConsentPreview& preview) {
  if (IsDurableErrandShape(preview)) {
    return true;
  }
  if (preview.provider_route == service::TaskProviderRoute::kNoModelRequired) {
    return IsPageScopedShape(preview, 1u, true) &&
           preview.sources.size() == 1u;
  }
  return IsPageScopedShape(preview, 1u, false);
}

}  // namespace taffy
