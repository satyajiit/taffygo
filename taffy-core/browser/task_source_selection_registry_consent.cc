// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

#include "base/containers/flat_set.h"
#include "base/logging.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_api/task_consent_shape.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "url/gurl.h"
#include "url/origin.h"

// The one start rule's browser half: the consent the person is shown is
// resolved from the hosts they named, the template's count rule and the
// route, and a request that does not fit is refused by name. Split from the
// registry's tab and window bookkeeping, which it reads and never writes.

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

bool IsCanonicalHostIntent(const std::string& host) {
  if (host.empty() || host.size() > service::kMaxNormalizedOriginBytes ||
      host.find('/') != std::string::npos ||
      host.find(':') != std::string::npos) {
    return false;
  }
  const GURL candidate("https://" + host + "/");
  return candidate.is_valid() && candidate.SchemeIsHTTPOrHTTPS() &&
         !candidate.host().empty() && candidate.host() == host;
}

bool IsNormalizedHttpTupleOrigin(const std::string& value) {
  const GURL parsed(value);
  const url::Origin origin = url::Origin::Create(parsed);
  return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
         !origin.opaque() && origin.Serialize() == value;
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

bool AdmitsStartTaskProviderRoute(api::TaskProviderRoute route) {
  switch (route) {
    case api::TaskProviderRoute::kNoModelRequired:
    case api::TaskProviderRoute::kDirectUserKey:
    case api::TaskProviderRoute::kManagedService:
      return true;
    case api::TaskProviderRoute::kNotConfigured:
      return false;
  }
  return false;
}

// Every refusal below answers through here, so what the person is told is
// written beside the clause that logged it (decision 0231). Every one of them
// used to reach the phone as "Taffy could not read this request", including
// the ones about tabs and windows, which no rewording of a request can fix.
std::nullopt_t RefuseStart(api::CoreApiSubmissionStatus* verdict,
                           api::CoreApiSubmissionStatus status) {
  if (verdict) {
    *verdict = status;
  }
  return std::nullopt;
}

}  // namespace

std::optional<service::TaskConsentPreviewPtr>
TaskSourceSelectionRegistry::ResolveConsentPreview(
    api::TaskTemplateId template_id,
    const api::TaskConsentPreview& intent,
    const std::optional<std::string>& skill_offer_id,
    api::CoreApiSubmissionStatus* verdict) {
  const std::optional<service::TaskProviderRoute> provider_route =
      ProjectProviderRoute(intent.provider_route);
  const size_t requested = intent.source_hosts.size();
  const bool is_errand = template_id == api::TaskTemplateId::kWebErrand;
  const bool model_route =
      intent.provider_route == api::TaskProviderRoute::kDirectUserKey ||
      intent.provider_route == api::TaskProviderRoute::kManagedService;
  const bool saved_replay =
      is_errand && skill_offer_id && !skill_offer_id->empty() &&
      skill_offer_id->size() <= api::kMaxIdentifierBytes &&
      intent.provider_route == api::TaskProviderRoute::kNoModelRequired;
  const bool count_admitted =
      is_errand
          ? (saved_replay ? requested == 1u : requested <= 1u)
          : requested <= kMaxSelectedTaskSources &&
                ((template_id == api::TaskTemplateId::kBuildSourceTable &&
                  requested == 1u) ||
                 (template_id == api::TaskTemplateId::kCompareProducts &&
                  requested >= 2u) ||
                 (template_id == api::TaskTemplateId::kSummarizeEvidence &&
                  requested >= 1u));
  const bool route_admitted =
      model_route || saved_replay ||
      (!is_errand && template_id == api::TaskTemplateId::kBuildSourceTable &&
       intent.provider_route == api::TaskProviderRoute::kNoModelRequired);
  const bool discovery_admitted =
      is_errand && !saved_replay
          ? intent.source_discovery_enabled && intent.new_source_cap >= 1u &&
                intent.new_source_cap <= kMaxErrandNewSourceCap
          : !intent.source_discovery_enabled && intent.new_source_cap == 0u;
  base::flat_set<std::string> requested_hosts(intent.source_hosts.begin(),
                                              intent.source_hosts.end());
  // Named, one clause per branch. This was one compounded `if` answering a
  // bare `std::nullopt`, and the phone reported every one of its ten reasons
  // as "This task no longer matches the selected page" — including the one
  // that was really "two windows are open". The label is compiled in; the
  // only number printed is a window count. A clause about the request says
  // the request could not be read; the first two say Taffy cannot work here,
  // and the last says which window is the trouble.
  const char* refusal = nullptr;
  api::CoreApiSubmissionStatus status =
      api::CoreApiSubmissionStatus::kInvalidRequest;
  if (!browser_context_) {
    refusal = "browser-context";
    status = api::CoreApiSubmissionStatus::kCoreUnavailable;
  } else if (browser_context_->IsOffTheRecord()) {
    refusal = "off-the-record";
    status = api::CoreApiSubmissionStatus::kCoreUnavailable;
  } else if (!provider_route) {
    refusal = "provider-route";
  } else if (!count_admitted) {
    refusal = "source-count";
  } else if (!route_admitted) {
    refusal = "route-for-template";
  } else if (!AdmitsStartTaskProviderRoute(intent.provider_route)) {
    refusal = "route-admission";
  } else if (requested_hosts.size() != requested) {
    refusal = "duplicate-hosts";
  } else if (std::any_of(intent.source_hosts.begin(), intent.source_hosts.end(),
                         [](const std::string& host) {
                           return !IsCanonicalHostIntent(host);
                         })) {
    refusal = "host-shape";
  } else if (!discovery_admitted) {
    refusal = "discovery";
  } else if (active_windows_.size() != 1u) {
    refusal = "active-windows";
    status = api::CoreApiSubmissionStatus::kWindowUnavailable;
  }
  if (refusal) {
    LOG(WARNING) << "[taffy_start_refused] at=consent/" << refusal
                 << " active_windows=" << active_windows_.size();
    return RefuseStart(verdict, status);
  }
  const auto window = windows_.find(*active_windows_.begin());
  if (window == windows_.end()) {
    LOG(WARNING) << "[taffy_start_refused] at=consent/window-unknown";
    return RefuseStart(verdict,
                       api::CoreApiSubmissionStatus::kWindowUnavailable);
  }
  if (is_errand && !window->second.browser_actions) {
    LOG(WARNING)
        << "[taffy_start_refused] at=consent/errand-no-browser-actions";
    return RefuseStart(verdict,
                       api::CoreApiSubmissionStatus::kWindowUnavailable);
  }

  struct Candidate {
    base::WeakPtr<content::WebContents> web_contents;
    std::string tab_id;
    std::string origin;
    std::optional<std::string> canonical_locator;
    std::string source_id;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(requested);
  for (const std::string& requested_host : intent.source_hosts) {
    std::optional<Candidate> match;
    // Why each tab was passed over, counted. A host a person is looking at
    // that resolves to no tab is the refusal here a person most often
    // provokes, and it used to reach them as "Taffy could not read this
    // request" — the sentence for a malformed command. It now answers
    // `kSourceNotOpen`, and a host two of their tabs show answers
    // `kSourceAmbiguous`. The counters are counters: no host, no address and
    // nothing from the page.
    // `unverified_restored` is the part of `not_user_owned` whose register
    // could not be read; the rest is what the tab switcher calls "opened by
    // Taffy" (decision 0232), so the two can be compared from one line.
    size_t seen = 0u;
    size_t not_user_owned = 0u;
    size_t unverified_restored = 0u;
    size_t not_this_profile = 0u;
    size_t no_observation = 0u;
    size_t other_host = 0u;
    for (const auto& [product_tab_id, tab] : window->second.tabs) {
      ++seen;
      if (tab.provenance != TaskSourceTabProvenance::kUserOwned) {
        ++not_user_owned;
        unverified_restored += size_t{
            tab.provenance == TaskSourceTabProvenance::kUnverifiedRestored};
        continue;
      }
      content::WebContents* web_contents = tab.web_contents.get();
      if (!web_contents ||
          web_contents->GetBrowserContext() != browser_context_ ||
          !IsRegisteredProductContents(web_contents)) {
        ++not_this_profile;
        continue;
      }
      TaffyPageIntelligenceHost* page_host =
          TaffyPageIntelligenceHost::FromWebContents(web_contents);
      std::optional<DirectObservationContext> live =
          page_host ? page_host->BuildDirectObservationContext() : std::nullopt;
      if (!live) {
        ++no_observation;
        continue;
      }
      if (live->host != requested_host || live->tab_id.empty() ||
          !IsNormalizedHttpTupleOrigin(live->origin)) {
        ++other_host;
        continue;
      }
      if (match.has_value()) {
        // Hosts are display intent, not tab authority. Two eligible tabs on
        // one requested host are ambiguous and never resolved by guessing at
        // the selected or most recent one.
        LOG(WARNING) << "[taffy_start_refused] at=consent/ambiguous-host";
        return RefuseStart(verdict,
                           api::CoreApiSubmissionStatus::kSourceAmbiguous);
      }
      match = Candidate{
          web_contents->GetWeakPtr(),
          live->tab_id,
          live->origin,
          is_errand ? std::nullopt : CanonicalSourceLocator(web_contents),
          {}};
    }
    if (!match.has_value()) {
      LOG(WARNING) << "[taffy_start_refused] at=consent/no-tab-for-host"
                   << " tabs=" << seen << " not_user_owned=" << not_user_owned
                   << " unverified_restored=" << unverified_restored
                   << " not_this_profile=" << not_this_profile
                   << " no_observation=" << no_observation
                   << " other_host=" << other_host;
      return RefuseStart(verdict, api::CoreApiSubmissionStatus::kSourceNotOpen);
    }
    candidates.push_back(std::move(*match));
  }

  PruneIssuedSources();
  size_t new_sources = 0u;
  for (Candidate& candidate : candidates) {
    for (const auto& [existing_id, source] : issued_sources_) {
      if (source.web_contents.get() == candidate.web_contents.get() &&
          source.tab_id == candidate.tab_id &&
          source.normalized_origin == candidate.origin &&
          source.canonical_locator == candidate.canonical_locator &&
          source.origin_scoped == is_errand) {
        candidate.source_id = existing_id;
        break;
      }
    }
    new_sources += size_t{candidate.source_id.empty()};
  }
  if (new_sources > issued_source_limit_ - std::min(issued_source_limit_,
                                                    issued_sources_.size())) {
    LOG(WARNING) << "[taffy_start_refused] at=consent/issued-source-limit"
                 << " issued=" << issued_sources_.size()
                 << " limit=" << issued_source_limit_;
    // The profile already holds as many issued pages as it will accept,
    // which is what backpressure means; nothing about the request is wrong.
    return RefuseStart(verdict, api::CoreApiSubmissionStatus::kBackpressure);
  }

  base::flat_set<std::string> pending_ids;
  for (Candidate& candidate : candidates) {
    if (!candidate.source_id.empty()) {
      continue;
    }
    const std::string uuid = base::Uuid::GenerateRandomV4().AsLowercaseString();
    std::string source_id;
    source_id.reserve(32u);
    std::copy_if(uuid.begin(), uuid.end(), std::back_inserter(source_id),
                 [](char character) { return character != '-'; });
    if (source_id.size() != 32u || issued_sources_.contains(source_id) ||
        !pending_ids.insert(source_id).second) {
      LOG(WARNING) << "[taffy_start_refused] at=consent/source-id";
      return RefuseStart(verdict,
                         api::CoreApiSubmissionStatus::kInvalidRequest);
    }
    candidate.source_id = std::move(source_id);
  }
  for (const Candidate& candidate : candidates) {
    if (issued_sources_.contains(candidate.source_id)) {
      continue;
    }
    issued_sources_.emplace(
        candidate.source_id,
        IssuedSource{candidate.source_id, candidate.tab_id, candidate.origin,
                     candidate.canonical_locator, candidate.web_contents,
                     is_errand});
  }

  auto preview = service::TaskConsentPreview::New();
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right) {
              return left.source_id < right.source_id;
            });
  for (const Candidate& candidate : candidates) {
    preview->sources.push_back(service::TaskConsentSource::New(
        candidate.source_id, candidate.tab_id, candidate.origin,
        candidate.canonical_locator));
  }
  preview->source_discovery_enabled = intent.source_discovery_enabled;
  preview->new_source_cap = intent.new_source_cap;
  preview->provider_route = *provider_route;
  return std::optional<service::TaskConsentPreviewPtr>(std::move(preview));
}

}  // namespace taffy
