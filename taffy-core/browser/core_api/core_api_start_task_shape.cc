// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Whether a start-task command's inputs are well formed, separated from the
// composition that reads them. The two answer different questions and the
// composing file reached its line cap holding both.

#include "taffy/browser/core_api/core_api_start_task_shape.h"

#include <algorithm>
#include <set>
#include <string_view>
#include <utility>

#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace start_task_shape {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

}  // namespace

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= api::kMaxIdentifierBytes;
}

bool IsOptionalIdentifier(const std::optional<std::string>& value) {
  return !value || IsIdentifier(*value);
}

// Whether the allowlist this task was asked for is a list somebody could have
// composed on purpose. It is the same question the browser's submission gate
// asks (`IsWellFormedToolAllowlist` in accepted_approval_ledger.cc) and, like
// that one, it names no tools: the vocabulary lives in the sandboxed core,
// which refuses a name this build's milestone has not reached, and the
// compiled-in action-class join refuses the effect the name would produce.
//
// Empty is the clause that matters, and it is why this cannot be left to the
// gate downstream alone: reducer guard evaluation reads an empty allowlist as
// everything the milestone has reached, so a factory that let `{}` through
// would be composing a wider task than any explicit list could ask for.
bool IsWellFormedToolAllowlist(const std::vector<std::string>& tools) {
  if (tools.empty() || tools.size() > service::kMaxToolAllowlistEntries) {
    return false;
  }
  std::set<std::string_view> seen;
  for (const std::string& tool : tools) {
    // Bounded by the Core Service identifier limit and not the Core API one:
    // the allowlist is a Core Service field, the two contracts are free to
    // move apart, and a command this factory composes has to survive the
    // submission gate that reads it with those limits.
    if (tool.empty() || tool.size() > service::kMaxIdentifierBytes ||
        !seen.insert(tool).second ||
        std::any_of(tool.begin(), tool.end(), [](char character) {
          return static_cast<unsigned char>(character) < 0x20u;
        })) {
      return false;
    }
  }
  return true;
}

std::optional<service::TaskTemplateId> ProjectTemplate(
    api::TaskTemplateId template_id) {
  switch (template_id) {
    case api::TaskTemplateId::kCompareProducts:
      return service::TaskTemplateId::kCompareProducts;
    case api::TaskTemplateId::kSummarizeEvidence:
      return service::TaskTemplateId::kSummarizeEvidence;
    case api::TaskTemplateId::kBuildSourceTable:
      return service::TaskTemplateId::kBuildSourceTable;
    case api::TaskTemplateId::kWebErrand:
      return service::TaskTemplateId::kWebErrand;
  }
  return std::nullopt;
}

std::optional<service::TaskProviderRoute> ProjectProviderRoute(
    api::TaskProviderRoute route) {
  switch (route) {
    case api::TaskProviderRoute::kDirectUserKey:
      return service::TaskProviderRoute::kDirectUserKey;
    case api::TaskProviderRoute::kManagedService:
      return service::TaskProviderRoute::kManagedService;
    case api::TaskProviderRoute::kNoModelRequired:
      return service::TaskProviderRoute::kNoModelRequired;
    case api::TaskProviderRoute::kNotConfigured:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::string> ProviderRouteId(service::TaskProviderRoute route) {
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

bool AreWellFormedResolvedSources(
    const std::vector<service::TaskConsentSourcePtr>& sources) {
  std::string_view previous_id;
  std::set<std::string_view> tabs;
  std::set<std::pair<std::string_view, std::string_view>> documents;
  for (const auto& source : sources) {
    const GURL parsed = source ? GURL(source->normalized_origin) : GURL();
    const url::Origin origin = url::Origin::Create(parsed);
    if (!source || !IsIdentifier(source->source_id) ||
        !IsIdentifier(source->tab_id) ||
        source->normalized_origin.size() > service::kMaxNormalizedOriginBytes ||
        !parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS() || origin.opaque() ||
        origin.Serialize() != source->normalized_origin ||
        (!previous_id.empty() && previous_id >= source->source_id) ||
        !tabs.insert(source->tab_id).second ||
        !documents.insert({source->tab_id, source->normalized_origin}).second) {
      return false;
    }
    previous_id = source->source_id;
  }
  return true;
}

bool IsCanonicalSourceHost(const std::string& host) {
  if (host.empty() || host.size() > service::kMaxNormalizedOriginBytes ||
      host.find('/') != std::string::npos ||
      host.find(':') != std::string::npos) {
    return false;
  }
  const GURL candidate("https://" + host + "/");
  return candidate.is_valid() && !candidate.host().empty() &&
         candidate.host() == host;
}

bool HasExactResolvedHosts(const api::TaskConsentPreview& intent,
                           const service::TaskConsentPreview& resolved) {
  if (intent.source_hosts.size() != resolved.sources.size()) {
    return false;
  }
  std::set<std::string> requested_hosts;
  std::set<std::string> resolved_hosts;
  for (const std::string& host : intent.source_hosts) {
    if (!IsCanonicalSourceHost(host) || !requested_hosts.insert(host).second) {
      return false;
    }
  }
  for (const auto& source : resolved.sources) {
    if (!source ||
        !resolved_hosts
             .insert(std::string(GURL(source->normalized_origin).host()))
             .second) {
      return false;
    }
  }
  return requested_hosts == resolved_hosts;
}

}  // namespace start_task_shape
}  // namespace taffy
