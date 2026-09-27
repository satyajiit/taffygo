// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <vector>

#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_task_browser_actions.h"
#include "taffy/browser/task_source_selection_registry.h"

namespace taffy {

std::vector<TaskStoreEntry> TaskSourceSelectionRegistry::ListPersonTabs()
    const {
  std::vector<TaskStoreEntry> entries;
  if (!browser_context_ || browser_context_->IsOffTheRecord()) {
    return entries;
  }
  for (const auto& [window_token, window] : windows_) {
    for (const auto& [product_tab_id, tab] : window.tabs) {
      content::WebContents* web_contents = tab.web_contents.get();
      if (!web_contents ||
          tab.provenance != TaskSourceTabProvenance::kUserOwned ||
          BrowserOwnedProvenance(web_contents) !=
              TaskSourceTabProvenance::kUserOwned ||
          web_contents->GetBrowserContext()->IsOffTheRecord()) {
        continue;
      }
      const GURL& url = web_contents->GetLastCommittedURL();
      if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
        continue;
      }
      entries.push_back(TaskStoreEntry{
          .title = web_contents->GetTitle(),
          .url = url,
          .when = web_contents->GetLastActiveTime(),
      });
    }
  }
  return entries;
}

// The browser-actions reading of the same list: nothing from a private
// profile, and nothing while no task browser is registered.
std::vector<TaskStoreEntry> CoreTaskBrowserActions::ListPersonTabs() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    return {};
  }
  return task_source_selections_->ListPersonTabs();
}

}  // namespace taffy
