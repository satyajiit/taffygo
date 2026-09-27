// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_document_decisions.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {

struct FilteringDocumentDecisions::Cached {
  scoped_refptr<const SharedRuleset> ruleset;
  uint64_t posture_revision = 0;
  GURL document_url;
  url::Origin document_origin;
  FilteringDocumentDecision decision;
};

FilteringDocumentDecisions::FilteringDocumentDecisions(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<FilteringDocumentDecisions>(*web_contents) {}

FilteringDocumentDecisions::~FilteringDocumentDecisions() = default;

FilteringDocumentDecision FilteringDocumentDecisions::Resolve(
    FilteringRulesetService& service,
    scoped_refptr<const SharedRuleset> ruleset,
    const GURL& document_url,
    const url::Origin& document_origin) {
  const uint64_t posture_revision = service.posture_revision();
  if (cached_ && cached_->ruleset.get() == ruleset.get() &&
      cached_->posture_revision == posture_revision &&
      cached_->document_url == document_url &&
      cached_->document_origin == document_origin) {
    return cached_->decision;
  }

  FilteringDocumentDecision decision;
  decision.active = service.posture().ActiveForHost(document_url.host());
  if (decision.active &&
      ruleset->matcher().IsDocumentAllowlisted(document_url, document_origin)) {
    decision.active = false;
  }
  if (decision.active) {
    decision.disable_generic_rules = ruleset->matcher().IsGenericBlockDisabled(
        document_url, document_origin);
  }
  cached_ = std::make_unique<Cached>(Cached{
      .ruleset = std::move(ruleset),
      .posture_revision = posture_revision,
      .document_url = document_url,
      .document_origin = document_origin,
      .decision = decision,
  });
  ++evaluation_count_;
  return decision;
}

void FilteringDocumentDecisions::PrimaryPageChanged(content::Page& page) {
  cached_.reset();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(FilteringDocumentDecisions);

}  // namespace taffy::filtering
