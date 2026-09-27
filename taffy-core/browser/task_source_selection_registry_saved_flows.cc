// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_task_browser_actions.h"
#include "taffy/browser/task_source_selection_registry.h"

namespace taffy {
content::WebContents* TaskSourceSelectionRegistry::SelectedUserOwnedTab()
    const {
  if (browser_context_->IsOffTheRecord() || active_windows_.size() != 1u) {
    return nullptr;
  }
  const auto window = windows_.find(*active_windows_.begin());
  if (window == windows_.end() || !window->second.selected_product_tab_id) {
    return nullptr;
  }
  const auto tab =
      window->second.tabs.find(*window->second.selected_product_tab_id);
  if (tab == window->second.tabs.end() || !tab->second.web_contents ||
      tab->second.provenance != TaskSourceTabProvenance::kUserOwned ||
      BrowserOwnedProvenance(tab->second.web_contents.get()) !=
          TaskSourceTabProvenance::kUserOwned ||
      tab->second.web_contents->GetBrowserContext() != browser_context_) {
    return nullptr;
  }
  return tab->second.web_contents.get();
}

uint64_t CoreTaskBrowserActions::SavedFlowStartSelectionRevision() const {
  return task_source_selections_->selection_revision();
}

content::WebContents* CoreTaskBrowserActions::SelectedSavedFlowStartTab()
    const {
  return task_source_selections_->SelectedUserOwnedTab();
}
}  // namespace taffy
