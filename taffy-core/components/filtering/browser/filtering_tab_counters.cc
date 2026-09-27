// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_tab_counters.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "url/gurl.h"

namespace taffy::filtering {

namespace {

// At most one publication per tab per this interval, after the immediate
// first one. Numbers may jump, never lie.
constexpr base::TimeDelta kCoalesceInterval = base::Milliseconds(500);

}  // namespace

FilteringTabCounters::FilteringTabCounters(
    content::WebContents* web_contents,
    base::WeakPtr<FilteringRulesetService> service)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<FilteringTabCounters>(*web_contents),
      service_(std::move(service)) {}

FilteringTabCounters::~FilteringTabCounters() = default;

void FilteringTabCounters::NoteBlocked() {
  ++blocked_count_;
  if (service_) {
    service_->NoteBlocked();
    content::WebContents* contents = web_contents();
    // A private tab's block reaches neither number a person can read later.
    // It is excluded from the week window here, and its lifetime increment
    // lands in the private profile's own preference overlay, which is never
    // written to disk and is discarded with the profile (decision 0128).
    if (contents && !contents->GetBrowserContext()->IsOffTheRecord()) {
      service_->NoteWeekBlocked(contents->GetLastCommittedURL().host());
    }
  }
  if (coalesce_timer_.IsRunning()) {
    return;
  }
  if (published_count_ == 0 && blocked_count_ == 1) {
    // The first block after a commit is the moment a shield appears; a person
    // should not wait half a second to learn their page is being cleaned.
    Publish();
  }
  coalesce_timer_.Start(FROM_HERE, kCoalesceInterval,
                        base::BindOnce(&FilteringTabCounters::Publish,
                                       base::Unretained(this)));
}

void FilteringTabCounters::FlushNow() {
  coalesce_timer_.Stop();
  Publish();
}

void FilteringTabCounters::SetCountChangedCallback(
    CountChangedCallback callback) {
  count_changed_ = std::move(callback);
}

void FilteringTabCounters::PrimaryPageChanged(content::Page& page) {
  // A new page is a new count. Published immediately, so a surface never
  // shows the old page's number over the new page.
  blocked_count_ = 0;
  coalesce_timer_.Stop();
  Publish();
}

void FilteringTabCounters::Publish() {
  if (published_count_ == blocked_count_ && blocked_count_ != 0) {
    return;
  }
  published_count_ = blocked_count_;
  if (count_changed_) {
    count_changed_.Run();
  }
  // The profile-wide signal, so a projection holding the profile refreshes
  // without subscribing to every tab.
  if (service_) {
    service_->NoteTabCountPublished();
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(FilteringTabCounters);

}  // namespace taffy::filtering
