// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>

#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// How many created tabs the register remembers.
//
// A bound rather than an unbounded list, because this is durable structure
// whose size would otherwise be a function of how many tabs a task opened.
// Android never reuses a tab id, so the only cost of dropping the oldest row
// is that a tab created long enough ago, and still open, could be offered as
// a source — and 512 created tabs behind one that is still open is far past
// anything a session produces.
constexpr size_t kMaxRememberedAssistantTabs = 512u;

// The register, or nothing when it cannot be read.
//
// Read through `Preference::GetValue()`, which is type-filtered and never
// asserts. `PrefService::GetList` would abort the browser on a profile whose
// stored value is not a list, which is a corrupt profile taking the browser
// with it rather than one refused tab.
const base::ListValue* RememberedTabs(PrefService* prefs) {
  if (!prefs) {
    return nullptr;
  }
  const PrefService::Preference* preference =
      prefs->FindPreference(profile_preferences::kAssistantCreatedTaskTabs);
  if (!preference) {
    return nullptr;
  }
  const base::Value* value = preference->GetValue();
  return value && value->is_list() ? &value->GetList() : nullptr;
}

// The profile's own preference store, reached without asking whether this
// browser context is a `Profile`.
//
// `Profile::FromBrowserContext` is the obvious call and is the wrong one here:
// it DCHECKs on a context that is not a profile and static-casts it anyway, so
// it would abort this registry's own unit suites — which use a
// `TestBrowserContext` on purpose, so that they need no Profile, no //chrome
// and no Chrome test environment — and would read a bogus pointer in a build
// with DCHECKs off. `UserPrefs` is the layered question and answers null for a
// context that has no store, which the register reads as "unreadable" rather
// than as "empty".
PrefService* ProfilePreferences(content::BrowserContext* browser_context) {
  return browser_context && user_prefs::UserPrefs::IsInitialized(browser_context)
             ? user_prefs::UserPrefs::Get(browser_context)
             : nullptr;
}

class AssistantCreatedTaskTabMarker final
    : public content::WebContentsUserData<AssistantCreatedTaskTabMarker> {
 public:
  ~AssistantCreatedTaskTabMarker() override = default;

  const std::string& task_id() const { return task_id_; }
  void SetCreatingTask(const std::string& task_id) { task_id_ = task_id; }

 private:
  friend class content::WebContentsUserData<AssistantCreatedTaskTabMarker>;
  explicit AssistantCreatedTaskTabMarker(content::WebContents* web_contents)
      : content::WebContentsUserData<AssistantCreatedTaskTabMarker>(
            *web_contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
  std::string task_id_;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(AssistantCreatedTaskTabMarker);

bool IsTaskIdentity(const std::string& value) {
  return !value.empty() && value.size() <= service::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

}  // namespace

// static
void TaskSourceSelectionRegistry::MarkAssistantCreatedTab(
    content::WebContents* web_contents) {
  if (web_contents &&
      !AssistantCreatedTaskTabMarker::FromWebContents(web_contents)) {
    AssistantCreatedTaskTabMarker::CreateForWebContents(web_contents);
  }
}

// static
TaskSourceTabProvenance TaskSourceSelectionRegistry::BrowserOwnedProvenance(
    content::WebContents* web_contents) {
  return web_contents &&
                 AssistantCreatedTaskTabMarker::FromWebContents(web_contents)
             ? TaskSourceTabProvenance::kAssistantCreated
             : TaskSourceTabProvenance::kUserOwned;
}

// static
std::string TaskSourceSelectionRegistry::BrowserOwnedTaskId(
    content::WebContents* web_contents) {
  const auto* marker = web_contents
                           ? AssistantCreatedTaskTabMarker::FromWebContents(
                                 web_contents)
                           : nullptr;
  return marker ? marker->task_id() : std::string();
}

bool TaskSourceSelectionRegistry::BindBrowserActionPlatform(
    WindowToken window_token,
    TaskBrowserActionPlatform* platform) {
  auto window = windows_.find(window_token);
  if (!platform || window == windows_.end() || window->second.browser_actions) {
    return false;
  }
  window->second.browser_actions = platform;
  return true;
}

void TaskSourceSelectionRegistry::UnbindBrowserActionPlatform(
    WindowToken window_token,
    TaskBrowserActionPlatform* platform) {
  auto window = windows_.find(window_token);
  if (window != windows_.end() && window->second.browser_actions == platform) {
    window->second.browser_actions = nullptr;
  }
}

TaskBrowserActionPlatform*
TaskSourceSelectionRegistry::BrowserActionPlatformFor(
    content::WebContents* web_contents) const {
  TaskBrowserActionPlatform* match = nullptr;
  for (const auto& [window_token, window] : windows_) {
    for (const auto& [product_tab_id, tab] : window.tabs) {
      if (tab.web_contents.get() != web_contents) {
        continue;
      }
      if (match || !window.browser_actions) {
        return nullptr;
      }
      match = window.browser_actions;
    }
  }
  return match;
}

bool TaskSourceSelectionRegistry::ClaimAssistantCreatedTab(
    WindowToken window_token,
    const std::string& task_id,
    const std::string& action_id,
    content::WebContents* web_contents) {
  PruneOwnedTaskTabs();
  const auto window = windows_.find(window_token);
  const std::string creating_task = BrowserOwnedTaskId(web_contents);
  if (window == windows_.end() || !window->second.browser_actions ||
      !IsTaskIdentity(task_id) || !IsTaskIdentity(action_id) || !web_contents ||
      web_contents->GetBrowserContext() != browser_context_ ||
      !TaffyPageIntelligenceHost::IsEligible(web_contents) ||
      IsRegisteredProductContents(web_contents) ||
      (!creating_task.empty() && creating_task != task_id) ||
      FindOwnedTaskTab(web_contents)) {
    return false;
  }
  MarkAssistantCreatedTab(web_contents);
  AssistantCreatedTaskTabMarker::FromWebContents(web_contents)
      ->SetCreatingTask(task_id);
  session_task_ids_.insert(task_id);
  owned_task_tabs_.push_back(OwnedTaskTab{
      .task_id = task_id,
      .action_id = action_id,
      .window_token = window_token,
      .web_contents = web_contents->GetWeakPtr(),
  });
  return true;
}

bool TaskSourceSelectionRegistry::IsTaskOwnedTab(
    const std::string& task_id,
    content::WebContents* web_contents) const {
  const OwnedTaskTab* owned = FindOwnedTaskTab(web_contents);
  return owned && owned->task_id == task_id && owned->product_tab_id &&
         owned->window_token != 0u && IsRegisteredProductContents(web_contents);
}

bool TaskSourceSelectionRegistry::HasExactTaskTabClaim(
    const std::string& task_id,
    const std::string& action_id,
    content::WebContents* web_contents) const {
  const OwnedTaskTab* owned = FindOwnedTaskTab(web_contents);
  return owned && owned->task_id == task_id && owned->action_id == action_id;
}

void TaskSourceSelectionRegistry::ForgetUnpublishedTaskTab(
    const std::string& task_id,
    content::WebContents* web_contents) {
  std::erase_if(owned_task_tabs_, [&](const OwnedTaskTab& owned) {
    return owned.task_id == task_id && !owned.product_tab_id &&
           owned.web_contents.get() == web_contents;
  });
}

std::vector<content::WebContents*>
TaskSourceSelectionRegistry::BeginReleaseTaskTabs(const std::string& task_id) {
  PruneOwnedTaskTabs();
  std::vector<content::WebContents*> tabs;
  for (OwnedTaskTab& owned : owned_task_tabs_) {
    content::WebContents* web_contents = owned.web_contents.get();
    if (owned.task_id != task_id || owned.release_in_flight || !web_contents ||
        !owned.product_tab_id || owned.window_token == 0u) {
      continue;
    }
    const auto window = windows_.find(owned.window_token);
    if (window == windows_.end() || !window->second.browser_actions) {
      continue;
    }
    const auto product_tab = window->second.tabs.find(*owned.product_tab_id);
    if (product_tab == window->second.tabs.end() ||
        product_tab->second.web_contents.get() != web_contents ||
        product_tab->second.provenance !=
            TaskSourceTabProvenance::kAssistantCreated) {
      continue;
    }
    owned.release_in_flight = true;
    tabs.push_back(web_contents);
  }
  return tabs;
}

void TaskSourceSelectionRegistry::CompleteTaskTabRelease(
    content::WebContents* web_contents,
    bool closed) {
  OwnedTaskTab* owned = FindOwnedTaskTab(web_contents);
  if (!owned) {
    return;
  }
  if (!closed) {
    owned->release_in_flight = false;
    return;
  }
  std::erase_if(owned_task_tabs_, [&](const OwnedTaskTab& candidate) {
    return candidate.web_contents.get() == web_contents;
  });
}

bool TaskSourceSelectionRegistry::HasTaskTabsPendingRelease(
    const std::string& task_id) const {
  return std::any_of(owned_task_tabs_.begin(), owned_task_tabs_.end(),
                     [&](const OwnedTaskTab& owned) {
                       return owned.task_id == task_id &&
                              owned.web_contents.get();
                     });
}

bool TaskSourceSelectionRegistry::CanReconcileTaskTabs(
    const std::string& task_id) const {
  if (!IsTaskIdentity(task_id)) {
    return false;
  }
  if (session_task_ids_.contains(task_id)) {
    return true;
  }
  for (const auto& [window_token, window] : windows_) {
    for (const auto& [product_tab_id, tab] : window.tabs) {
      if (tab.web_contents &&
          tab.provenance == TaskSourceTabProvenance::kUnverifiedRestored) {
        return false;
      }
    }
  }
  return true;
}

void TaskSourceSelectionRegistry::PruneOwnedTaskTabs() {
  std::erase_if(owned_task_tabs_,
                [](const OwnedTaskTab& owned) { return !owned.web_contents; });
}

TaskSourceSelectionRegistry::OwnedTaskTab*
TaskSourceSelectionRegistry::FindOwnedTaskTab(
    content::WebContents* web_contents) {
  const auto found =
      std::find_if(owned_task_tabs_.begin(), owned_task_tabs_.end(),
                   [web_contents](const OwnedTaskTab& owned) {
                     return owned.web_contents.get() == web_contents;
                   });
  return found == owned_task_tabs_.end() ? nullptr : &*found;
}

const TaskSourceSelectionRegistry::OwnedTaskTab*
TaskSourceSelectionRegistry::FindOwnedTaskTab(
    content::WebContents* web_contents) const {
  const auto found =
      std::find_if(owned_task_tabs_.begin(), owned_task_tabs_.end(),
                   [web_contents](const OwnedTaskTab& owned) {
                     return owned.web_contents.get() == web_contents;
                   });
  return found == owned_task_tabs_.end() ? nullptr : &*found;
}

std::optional<bool> TaskSourceSelectionRegistry::RememberedAssistantCreatedTab(
    int product_tab_id) const {
  const base::ListValue* remembered =
      RememberedTabs(ProfilePreferences(browser_context_));
  if (!remembered) {
    return std::nullopt;
  }
  for (const base::Value& entry : *remembered) {
    if (entry.is_int() && entry.GetInt() == product_tab_id) {
      return true;
    }
  }
  return false;
}

void TaskSourceSelectionRegistry::RememberAssistantCreatedTab(
    int product_tab_id) {
  PrefService* prefs = ProfilePreferences(browser_context_);
  const base::ListValue* remembered = RememberedTabs(prefs);
  if (!remembered) {
    return;
  }
  for (const base::Value& entry : *remembered) {
    if (entry.is_int() && entry.GetInt() == product_tab_id) {
      return;
    }
  }
  base::ListValue updated = remembered->Clone();
  updated.Append(product_tab_id);
  while (updated.size() > kMaxRememberedAssistantTabs) {
    updated.erase(updated.begin());
  }
  prefs->SetList(profile_preferences::kAssistantCreatedTaskTabs,
                 std::move(updated));
}

}  // namespace taffy
