// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_DOCUMENT_DECISIONS_H_
#define TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_DOCUMENT_DECISIONS_H_

#include <stddef.h>

#include <memory>

#include "base/memory/ref_counted.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class GURL;

namespace url {
class Origin;
}

namespace taffy::filtering {

class FilteringRulesetService;
class SharedRuleset;

// The two document-level answers a subresource request needs before its own
// URL can be judged. They are stable for one ruleset/posture revision.
struct FilteringDocumentDecision {
  bool active = false;
  bool disable_generic_rules = false;
};

// Caches the document-level half of filtering on the tab that owns it.
//
// A page can issue hundreds of subresource requests. Its document allowlist
// and generic-block posture do not change between those requests, so running
// both ad-block-engine queries for every one is redundant. A committed page,
// posture revision, or immutable ruleset change invalidates the answer.
class FilteringDocumentDecisions
    : public content::WebContentsObserver,
      public content::WebContentsUserData<FilteringDocumentDecisions> {
 public:
  ~FilteringDocumentDecisions() override;

  FilteringDocumentDecision Resolve(FilteringRulesetService& service,
                                    scoped_refptr<const SharedRuleset> ruleset,
                                    const GURL& document_url,
                                    const url::Origin& document_origin);

  size_t evaluation_count_for_testing() const { return evaluation_count_; }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<FilteringDocumentDecisions>;
  explicit FilteringDocumentDecisions(content::WebContents* web_contents);

  struct Cached;
  std::unique_ptr<Cached> cached_;
  size_t evaluation_count_ = 0;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_DOCUMENT_DECISIONS_H_
