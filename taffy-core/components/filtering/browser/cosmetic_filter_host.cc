// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/cosmetic_filter_host.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "content/public/browser/render_frame_host.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/filtering/core/cosmetic_resources.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {
namespace {

constexpr size_t kMaxTokens = 200;
constexpr size_t kMaxTokenBytes = 256;

mojom::CosmeticResourcesPtr DisabledResources() {
  auto resources = mojom::CosmeticResources::New();
  resources->enabled = false;
  resources->generichide = false;
  return resources;
}

std::vector<std::string> CopyCappedTokens(
    const std::vector<std::string>& tokens,
    size_t* remaining) {
  std::vector<std::string> kept;
  kept.reserve(std::min(*remaining, tokens.size()));
  for (const std::string& token : tokens) {
    if (*remaining == 0) {
      break;
    }
    --*remaining;
    if (token.empty() || token.size() > kMaxTokenBytes) {
      continue;
    }
    kept.push_back(token);
  }
  return kept;
}

}  // namespace

DOCUMENT_USER_DATA_KEY_IMPL(CosmeticFilterHost);

// static
void CosmeticFilterHost::Bind(
    content::RenderFrameHost* rfh,
    base::WeakPtr<FilteringRulesetService> service,
    mojo::PendingAssociatedReceiver<mojom::CosmeticFilterHost> receiver) {
  if (!rfh) {
    return;
  }
  if (CosmeticFilterHost* existing = GetForCurrentDocument(rfh)) {
    existing->service_ = std::move(service);
    existing->receiver_.reset();
    existing->receiver_.Bind(std::move(receiver));
    existing->enabled_ = false;
    existing->generichide_ = false;
    existing->exceptions_.clear();
    return;
  }
  CreateForCurrentDocument(rfh, std::move(service), std::move(receiver));
}

CosmeticFilterHost::CosmeticFilterHost(
    content::RenderFrameHost* rfh,
    base::WeakPtr<FilteringRulesetService> service,
    mojo::PendingAssociatedReceiver<mojom::CosmeticFilterHost> receiver)
    : content::DocumentUserData<CosmeticFilterHost>(rfh),
      service_(std::move(service)) {
  receiver_.Bind(std::move(receiver));
}

CosmeticFilterHost::~CosmeticFilterHost() = default;

void CosmeticFilterHost::GetUrlCosmeticResources(
    GetUrlCosmeticResourcesCallback callback) {
  std::vector<std::string> exceptions;
  mojom::CosmeticResourcesPtr resources = EvaluateUrlResources(
      service_.get(), render_frame_host().GetLastCommittedURL(),
      render_frame_host().GetLastCommittedOrigin(), &exceptions);
  enabled_ = resources->enabled;
  generichide_ = resources->generichide;
  exceptions_ = std::move(exceptions);
  std::move(callback).Run(std::move(resources));
}

void CosmeticFilterHost::HiddenClassIdSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    HiddenClassIdSelectorsCallback callback) {
  std::move(callback).Run(EvaluateHiddenClassIdSelectors(
      service_.get(), enabled_, generichide_, exceptions_, classes, ids));
}

// static
mojom::CosmeticResourcesPtr CosmeticFilterHost::EvaluateUrlResources(
    FilteringRulesetService* service,
    const GURL& document_url,
    const url::Origin& document_origin,
    std::vector<std::string>* exceptions_out) {
  if (exceptions_out) {
    exceptions_out->clear();
  }
  if (!service || !service->ruleset()) {
    return DisabledResources();
  }
  if (!document_url.SchemeIsHTTPOrHTTPS()) {
    return DisabledResources();
  }
  if (!service->posture().ActiveForHost(document_url.host())) {
    return DisabledResources();
  }
  const FilterRulesetMatcher& matcher = service->ruleset()->matcher();
  if (matcher.IsDocumentAllowlisted(document_url, document_origin)) {
    return DisabledResources();
  }
  CosmeticResources parsed = matcher.UrlCosmeticResources(document_url);
  if (exceptions_out) {
    *exceptions_out = parsed.exceptions;
  }
  auto resources = mojom::CosmeticResources::New();
  resources->hide_selectors = std::move(parsed.hide_selectors);
  resources->generichide = parsed.generichide;
  resources->enabled = true;
  return resources;
}

// static
std::vector<std::string> CosmeticFilterHost::EvaluateHiddenClassIdSelectors(
    FilteringRulesetService* service,
    bool enabled,
    bool generichide,
    const std::vector<std::string>& exceptions,
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids) {
  if (!enabled || generichide || !service || !service->ruleset()) {
    return {};
  }
  // Clamp while copying. The renderer is a hostile process edge: copying or
  // scanning its whole message made work proportional to attacker-controlled
  // input. Invalid values spend the shared budget too, so an invalid prefix
  // cannot move the bound from memory to CPU.
  size_t remaining = kMaxTokens;
  std::vector<std::string> capped_classes =
      CopyCappedTokens(classes, &remaining);
  std::vector<std::string> capped_ids = CopyCappedTokens(ids, &remaining);
  return service->ruleset()->matcher().HiddenClassIdSelectors(
      capped_classes, capped_ids, exceptions);
}

}  // namespace taffy::filtering
