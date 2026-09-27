// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/tab_session_registry.h"

#include <algorithm>
#include <string_view>

#include "content/public/browser/browser_thread.h"

namespace taffy {

namespace {

bool DispositionIsSupportedAtM1(WindowOpenDisposition disposition) {
  // PAR-NAV-003 asks for the current tab and a new tab, foreground and
  // background. Everything else — a new window, a popup, an off-the-record
  // window, a download — is a separate parity row with its own milestone, and
  // approximating one of them here would be a capability nobody reviewed.
  switch (disposition) {
    case WindowOpenDisposition::CURRENT_TAB:
    case WindowOpenDisposition::NEW_FOREGROUND_TAB:
    case WindowOpenDisposition::NEW_BACKGROUND_TAB:
      return true;
    default:
      return false;
  }
}

std::string_view AsView(const RecordIdentifier& identifier) {
  return std::string_view(identifier.chars.data());
}

}  // namespace

TabSessionRegistry::TabSessionRegistry() = default;
TabSessionRegistry::~TabSessionRegistry() = default;

void TabSessionRegistry::SetDelegate(TabSessionDelegate* delegate) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  delegate_ = delegate;
}

TabOpenResult TabSessionRegistry::OpenTab(const TabOpenRequest& request,
                                          TabId* opened_tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  switch (request.request_origin) {
    case TabRequestOrigin::kUnknown:
      // Fail closed, the same rule the download and external-intent seam uses.
      return TabOpenResult::kRefusedUnattributedOrigin;
    case TabRequestOrigin::kAssistantTask:
      if (!request.owning_task_id.is_valid()) {
        // An assistant request that names no task has no scope to be inside,
        // and a tab opened outside a task scope belongs to nothing that can be
        // paused, stopped or cleaned up.
        return TabOpenResult::kRefusedAssistantOutsideTaskScope;
      }
      break;
    case TabRequestOrigin::kUserGesture:
    case TabRequestOrigin::kBrowserTeardown:
    case TabRequestOrigin::kSessionRestore:
      break;
  }

  if (!DispositionIsSupportedAtM1(request.disposition)) {
    return TabOpenResult::kRefusedUnsupportedDisposition;
  }
  if (!delegate_) {
    return TabOpenResult::kRefusedNoDelegate;
  }

  const TabId tab_id = delegate_->OpenTab(request);
  if (!tab_id.is_valid() || IsRetired(tab_id)) {
    // A platform that reissued a retired identifier is a platform whose tabs
    // cannot be told apart. Refusing is the only safe answer, and it is loud.
    return TabOpenResult::kPlatformRefused;
  }

  TabRecord record;
  record.tab_id = tab_id;
  record.profile_id = request.profile_id;
  record.window_id = request.window_id;
  record.index = tabs_.size();
  record.ownership = request.request_origin == TabRequestOrigin::kAssistantTask
                         ? TabOwnership::kAssistant
                         : request.ownership;
  if (record.ownership == TabOwnership::kUnknown) {
    // An unattributed tab is the user's. The rules that protect the user's
    // tabs are the stricter ones.
    record.ownership = TabOwnership::kUser;
  }
  record.owning_task_id = request.owning_task_id;
  record.is_active =
      request.disposition == WindowOpenDisposition::NEW_FOREGROUND_TAB ||
      request.disposition == WindowOpenDisposition::CURRENT_TAB;

  Insert(record);
  if (record.is_active) {
    active_tab_id_ = tab_id;
  }
  if (opened_tab_id) {
    *opened_tab_id = tab_id;
  }
  return TabOpenResult::kOpened;
}

TabActivateResult TabSessionRegistry::ActivateTab(
    const TabId& tab_id,
    TabRequestOrigin request_origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const TabRecord* record = FindTab(tab_id);
  if (!record) {
    return TabActivateResult::kRefusedUnknownTab;
  }
  if (request_origin == TabRequestOrigin::kUnknown) {
    return TabActivateResult::kRefusedUnattributedOrigin;
  }
  if (request_origin == TabRequestOrigin::kAssistantTask &&
      record->ownership != TabOwnership::kAssistant) {
    // Rule 2. The assistant may bring its own tab forward; taking focus away
    // from what the user is reading is not its to do.
    return TabActivateResult::kRefusedAssistantMayNotStealFocus;
  }
  if (active_tab_id_ == tab_id) {
    return TabActivateResult::kAlreadyActive;
  }
  if (!delegate_) {
    return TabActivateResult::kRefusedNoDelegate;
  }
  if (!delegate_->ActivateTab(tab_id)) {
    return TabActivateResult::kPlatformRefused;
  }

  NoteTabActivatedByPlatform(tab_id);
  return TabActivateResult::kActivated;
}

TabCloseResult TabSessionRegistry::CloseTab(const TabId& tab_id,
                                            TabCloseOrigin close_origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const TabRecord* record = FindTab(tab_id);
  if (!record) {
    return TabCloseResult::kRefusedUnknownTab;
  }

  switch (close_origin) {
    case TabCloseOrigin::kUnknown:
      return TabCloseResult::kRefusedUnattributedOrigin;
    case TabCloseOrigin::kAssistantCleanup:
      if (record->ownership != TabOwnership::kAssistant) {
        // Rule 2. Closing a tab the user opened destroys work they can see.
        // The refusal lives here so that it holds for every caller, including
        // one that has not been written yet.
        return TabCloseResult::kRefusedAssistantMayNotCloseUserTab;
      }
      break;
    case TabCloseOrigin::kUserGesture:
    case TabCloseOrigin::kBrowserTeardown:
      break;
  }

  if (!delegate_) {
    return TabCloseResult::kRefusedNoDelegate;
  }
  if (!delegate_->CloseTab(tab_id)) {
    return TabCloseResult::kPlatformRefused;
  }

  Retire(tab_id);
  return TabCloseResult::kClosed;
}

std::vector<TabRecord> TabSessionRegistry::ListTabs() const {
  std::vector<TabRecord> listed;
  listed.reserve(tabs_.size());
  for (const auto& [tab_id, record] : tabs_) {
    listed.push_back(record);
  }
  std::stable_sort(listed.begin(), listed.end(),
                   [](const TabRecord& left, const TabRecord& right) {
                     if (left.window_id.value != right.window_id.value) {
                       return left.window_id.value < right.window_id.value;
                     }
                     return left.index < right.index;
                   });
  return listed;
}

const TabRecord* TabSessionRegistry::FindTab(const TabId& tab_id) const {
  auto it = tabs_.find(tab_id);
  return it == tabs_.end() ? nullptr : &it->second;
}

bool TabSessionRegistry::IsRetired(const TabId& tab_id) const {
  return retired_tab_ids_.count(tab_id) != 0;
}

void TabSessionRegistry::AdoptRecoveryPlan(const SessionRecoveryPlan& plan,
                                           const ProfileId& profile_id,
                                           const BrowserWindowId& window_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  for (const TabRestoreDirective& directive : plan.tabs) {
    if (directive.action == TabRestoreAction::kDiscardTemporaryTab) {
      continue;
    }

    TabRecord record;
    record.tab_id = TabId{std::string(AsView(directive.tab_id))};
    if (!record.tab_id.is_valid() || IsRetired(record.tab_id)) {
      // A persisted identifier that this session already retired names a tab
      // that no longer exists. Skipping is the fail-closed answer; adopting it
      // would resurrect a name.
      continue;
    }
    record.profile_id = profile_id;
    record.window_id = window_id;
    record.index = directive.index;
    record.is_active = directive.activate;
    record.ownership =
        directive.action == TabRestoreAction::kRestoreAsAssistantTabPaused
            ? TabOwnership::kAssistant
            : TabOwnership::kUser;

    Insert(record);
    if (record.is_active) {
      active_tab_id_ = record.tab_id;
    }
  }
}

void TabSessionRegistry::NoteTabOpenedByPlatform(const TabRecord& record) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!record.is_valid() || IsRetired(record.tab_id)) {
    return;
  }
  Insert(record);
  if (record.is_active) {
    active_tab_id_ = record.tab_id;
  }
}

void TabSessionRegistry::NoteTabClosedByPlatform(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Retire(tab_id);
}

void TabSessionRegistry::NoteTabActivatedByPlatform(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (tabs_.find(tab_id) == tabs_.end()) {
    return;
  }
  for (auto& [id, record] : tabs_) {
    record.is_active = id == tab_id;
  }
  active_tab_id_ = tab_id;
}

void TabSessionRegistry::Insert(const TabRecord& record) {
  tabs_.insert_or_assign(record.tab_id, record);
  if (record.is_active) {
    for (auto& [id, existing] : tabs_) {
      existing.is_active = id == record.tab_id;
    }
  }
}

void TabSessionRegistry::Retire(const TabId& tab_id) {
  if (tabs_.erase(tab_id) == 0) {
    return;
  }
  // Rule 1. The identifier is retired for the life of the session, so a stale
  // handle carrying it can never name a live tab.
  retired_tab_ids_.insert(tab_id);

  if (active_tab_id_ != tab_id) {
    return;
  }
  active_tab_id_ = TabId{};
  // Focus falls to the first remaining tab in list order. Chromium decides
  // this for real; the registry only keeps its record from claiming that a
  // closed tab is still active.
  const std::vector<TabRecord> remaining = ListTabs();
  if (!remaining.empty()) {
    NoteTabActivatedByPlatform(remaining.front().tab_id);
  }
}

}  // namespace taffy
