// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_source_selection_registry.h"

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
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

}  // namespace

TaskSourceSelectionRegistry::TaskSourceSelectionRegistry(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

TaskSourceSelectionRegistry::~TaskSourceSelectionRegistry() = default;

// static
std::optional<std::string> TaskSourceSelectionRegistry::CanonicalSourceLocator(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return std::nullopt;
  }
  const GURL& committed = web_contents->GetLastCommittedURL();
  if (!committed.is_valid() || !committed.SchemeIsHTTPOrHTTPS() ||
      committed.has_username() || committed.has_password() ||
      committed.has_query()) {
    return std::nullopt;
  }
  GURL::Replacements replacements;
  replacements.ClearRef();
  const GURL canonical = committed.ReplaceComponents(replacements);
  if (!canonical.is_valid() || canonical.spec().empty() ||
      canonical.spec().size() > service::kMaxSourceLocatorBytes) {
    return std::nullopt;
  }
  return canonical.spec();
}

TaskSourceSelectionRegistry::WindowToken
TaskSourceSelectionRegistry::RegisterProductWindow() {
  if (!browser_context_ || browser_context_->IsOffTheRecord() ||
      next_window_token_ == 0u ||
      next_window_token_ == std::numeric_limits<WindowToken>::max()) {
    return 0u;
  }
  const WindowToken token = next_window_token_++;
  if (!windows_.emplace(token, ProductWindow()).second) {
    return 0u;
  }
  return token;
}

void TaskSourceSelectionRegistry::UnregisterProductWindow(
    WindowToken window_token) {
  const auto window = windows_.find(window_token);
  if (window != windows_.end()) {
    for (const auto& [product_tab_id, tab] : window->second.tabs) {
      EraseIssuedSourcesForContents(tab.web_contents.get());
    }
  }
  for (OwnedTaskTab& owned : owned_task_tabs_) {
    if (owned.window_token == window_token) {
      owned.window_token = 0u;
      owned.product_tab_id.reset();
      owned.release_in_flight = false;
    }
  }
  windows_.erase(window_token);
  if (active_windows_.erase(window_token) > 0u) {
    ++selection_revision_;
  }
}

bool TaskSourceSelectionRegistry::RegisterProductTab(
    WindowToken window_token,
    int product_tab_id,
    content::WebContents* web_contents,
    bool restored_without_provenance) {
  auto window = windows_.find(window_token);
  if (window == windows_.end() || product_tab_id < 0 || !web_contents ||
      web_contents->GetBrowserContext() != browser_context_ ||
      !TaffyPageIntelligenceHost::IsEligible(web_contents)) {
    return false;
  }

  // A movable tab is owned by one product window at a time. Registration in
  // its new selector wins even if the old selector's posted unregistration has
  // not run yet; that later callback is scoped to the old window and cannot
  // remove the new entry.
  //
  // Identity here is the WebContents as well as the number a window calls it
  // by. Evicting only by id was a state this registry could enter and never
  // leave: ResolveConsentPreview refuses a contents that
  // IsRegisteredProductContents finds under more than one window, so a contents
  // left registered in an old window under a different id made every later
  // resolution fail, with nothing reporting why and no sequence of window
  // operations able to recover it. The rule the comment above states is the one
  // applied.
  for (auto& [other_token, other_window] : windows_) {
    if (other_token == window_token) {
      continue;
    }
    for (auto entry = other_window.tabs.begin();
         entry != other_window.tabs.end();) {
      if (entry->first != product_tab_id &&
          entry->second.web_contents.get() != web_contents) {
        ++entry;
        continue;
      }
      if (other_window.selected_product_tab_id == entry->first) {
        ++selection_revision_;
        other_window.selected_product_tab_id.reset();
      }
      entry = other_window.tabs.erase(entry);
    }
  }

  const auto prior = window->second.tabs.find(product_tab_id);
  if (prior != window->second.tabs.end() &&
      prior->second.web_contents.get() != web_contents) {
    if (window->second.selected_product_tab_id == product_tab_id) {
      ++selection_revision_;
    }
    EraseIssuedSourcesForContents(prior->second.web_contents.get());
  }

  // A restored tab is asked about rather than guessed at (decision 0151). The
  // WebContents marker dies with the process, so a restored tab used to carry
  // no trustworthy ownership fact and was refused as a consent source for the
  // life of the profile — every tab a person had open before the last restart
  // answered "Taffy could not read this request" and always would. The
  // profile's own register of created tab ids is that fact, and a register
  // that cannot be read still refuses, because unreadable and empty are
  // different answers.
  const TaskSourceTabProvenance marked = BrowserOwnedProvenance(web_contents);
  TaskSourceTabProvenance provenance = TaskSourceTabProvenance::kUserOwned;
  if (marked == TaskSourceTabProvenance::kAssistantCreated) {
    provenance = marked;
  } else if (restored_without_provenance) {
    const std::optional<bool> remembered =
        RememberedAssistantCreatedTab(product_tab_id);
    provenance = !remembered.has_value()
                     ? TaskSourceTabProvenance::kUnverifiedRestored
                     : (*remembered
                            ? TaskSourceTabProvenance::kAssistantCreated
                            : TaskSourceTabProvenance::kUserOwned);
  }
  OwnedTaskTab* owned = FindOwnedTaskTab(web_contents);
  if (owned && (provenance != TaskSourceTabProvenance::kAssistantCreated ||
                (!owned->product_tab_id && owned->window_token != 0u &&
                 owned->window_token != window_token))) {
    return false;
  }
  if (provenance == TaskSourceTabProvenance::kAssistantCreated) {
    RememberAssistantCreatedTab(product_tab_id);
    // The register's answer goes back on the browser object as well as into
    // this entry (decision 0232). Every other reader of provenance — the tab
    // switcher, the Ask list, time on sites — asks the WebContents marker, so
    // a restored tab the register names used to be Taffy's here and the
    // person's everywhere else: the start refusal counted it `not_user_owned`
    // while the switcher said "0 opened by Taffy" over the same tabs. The
    // marker carries no creating task, so nothing can claim, list or close the
    // tab as a task's; it only stops the tab being offered as the person's.
    MarkAssistantCreatedTab(web_contents);
  }
  window->second.tabs.insert_or_assign(
      product_tab_id,
      ProductTab{product_tab_id, web_contents->GetWeakPtr(), provenance});
  if (owned) {
    owned->window_token = window_token;
    owned->product_tab_id = product_tab_id;
  }
  return true;
}

void TaskSourceSelectionRegistry::UnregisterProductTab(WindowToken window_token,
                                                       int product_tab_id) {
  auto window = windows_.find(window_token);
  if (window == windows_.end()) {
    return;
  }
  const auto tab = window->second.tabs.find(product_tab_id);
  if (tab != window->second.tabs.end()) {
    EraseIssuedSourcesForContents(tab->second.web_contents.get());
    if (OwnedTaskTab* owned =
            FindOwnedTaskTab(tab->second.web_contents.get())) {
      if (owned->window_token == window_token &&
          owned->product_tab_id == product_tab_id) {
        owned->window_token = 0u;
        owned->product_tab_id.reset();
        owned->release_in_flight = false;
      }
    }
  }
  window->second.tabs.erase(product_tab_id);
  if (window->second.selected_product_tab_id == product_tab_id) {
    if (window->second.selected_product_tab_id) {
      ++selection_revision_;
    }
    window->second.selected_product_tab_id.reset();
  }
}

bool TaskSourceSelectionRegistry::SelectProductTab(
    WindowToken window_token,
    int product_tab_id,
    content::WebContents* web_contents) {
  auto window = windows_.find(window_token);
  if (window == windows_.end() || !web_contents) {
    return false;
  }
  const auto tab = window->second.tabs.find(product_tab_id);
  if (tab == window->second.tabs.end() ||
      tab->second.web_contents.get() != web_contents ||
      web_contents->GetBrowserContext() != browser_context_) {
    return false;
  }
  if (window->second.selected_product_tab_id != product_tab_id) {
    ++selection_revision_;
  }
  window->second.selected_product_tab_id = product_tab_id;
  return true;
}

void TaskSourceSelectionRegistry::ClearProductSelection(
    WindowToken window_token) {
  const auto window = windows_.find(window_token);
  if (window != windows_.end()) {
    if (window->second.selected_product_tab_id) {
      ++selection_revision_;
    }
    window->second.selected_product_tab_id.reset();
  }
}

bool TaskSourceSelectionRegistry::ActivateProductWindow(
    WindowToken window_token) {
  if (!windows_.contains(window_token)) {
    return false;
  }
  if (active_windows_.insert(window_token).second) {
    ++selection_revision_;
  }
  return true;
}

void TaskSourceSelectionRegistry::DeactivateProductWindow(
    WindowToken window_token) {
  if (active_windows_.erase(window_token) > 0u) {
    ++selection_revision_;
  }
}

bool TaskSourceSelectionRegistry::IsRegisteredProductContents(
    content::WebContents* web_contents) const {
  size_t matches = 0u;
  for (const auto& [window_token, window] : windows_) {
    for (const auto& [product_tab_id, tab] : window.tabs) {
      if (tab.web_contents.get() == web_contents) {
        ++matches;
      }
    }
  }
  return matches == 1u;
}

}  // namespace taffy
