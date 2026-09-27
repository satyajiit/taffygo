// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_TAB_COUNTERS_H_
#define TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_TAB_COUNTERS_H_

#include <cstdint>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace taffy::filtering {

class FilteringRulesetService;

// One tab's blocked-request count, and the coalescing that keeps it honest
// without turning a busy page into a publication storm (decision 0076): the
// first block after a navigation commit publishes immediately, then at most
// one publication per half second carrying the accumulated count — the number
// may jump, never lie — with a forced flush available for the moments a
// surface opens.
class FilteringTabCounters
    : public content::WebContentsObserver,
      public content::WebContentsUserData<FilteringTabCounters> {
 public:
  // Runs on the UI thread each time the published count changes.
  using CountChangedCallback = base::RepeatingClosure;

  ~FilteringTabCounters() override;

  // One blocked request on the committed page. UI thread; also feeds the
  // profile's lifetime total through the service.
  void NoteBlocked();

  // What a surface shows now: everything counted so far this page, whether or
  // not the coalescer has announced it yet. Reads never wait on the timer.
  uint32_t blocked_count() const { return blocked_count_; }

  // Publishes immediately whatever the coalescer is holding. For the moments
  // the number is about to be looked at: tab activation, the site sheet.
  void FlushNow();

  void SetCountChangedCallback(CountChangedCallback callback);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<FilteringTabCounters>;
  FilteringTabCounters(content::WebContents* web_contents,
                       base::WeakPtr<FilteringRulesetService> service);

  void Publish();

  const base::WeakPtr<FilteringRulesetService> service_;
  CountChangedCallback count_changed_;
  uint32_t blocked_count_ = 0;
  uint32_t published_count_ = 0;
  base::OneShotTimer coalesce_timer_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_TAB_COUNTERS_H_
