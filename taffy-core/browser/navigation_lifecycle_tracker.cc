// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/navigation_lifecycle_tracker.h"

#include <utility>

#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace taffy {

NavigationLifecycleTracker::NavigationLifecycleTracker(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<NavigationLifecycleTracker>(*web_contents) {
  RefreshControlState();
}

NavigationLifecycleTracker::~NavigationLifecycleTracker() = default;

void NavigationLifecycleTracker::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void NavigationLifecycleTracker::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void NavigationLifecycleTracker::SetIdentityProvider(
    NavigationIdentityProvider* provider) {
  identity_provider_ = provider;
}

const BrowserNavigationRecord*
NavigationLifecycleTracker::LastCommittedForFrame(
    content::FrameTreeNodeId node_id) const {
  auto it = records_.find(node_id);
  return it == records_.end() ? nullptr : &it->second;
}

const BrowserNavigationRecord*
NavigationLifecycleTracker::PrimaryMainFrameRecord() const {
  if (!primary_main_frame_node_.has_value()) {
    return nullptr;
  }
  return LastCommittedForFrame(*primary_main_frame_node_);
}

bool NavigationLifecycleTracker::ExecuteControl(
    BrowserCommandType command_type) {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return false;
  }
  RefreshControlState();
  content::NavigationController& controller = contents->GetController();
  switch (command_type) {
    case BrowserCommandType::kGoBack:
      if (!control_state_.can_go_back) {
        return false;
      }
      controller.GoBack();
      return true;
    case BrowserCommandType::kGoForward:
      if (!control_state_.can_go_forward) {
        return false;
      }
      controller.GoForward();
      return true;
    case BrowserCommandType::kReload:
      if (!control_state_.can_reload) {
        return false;
      }
      controller.Reload(content::ReloadType::NORMAL,
                        /*check_for_repost=*/false);
      return true;
    case BrowserCommandType::kStopLoading:
      if (!control_state_.can_stop) {
        return false;
      }
      contents->Stop();
      return true;
    case BrowserCommandType::kNavigate:
    case BrowserCommandType::kOpenTaskTab:
    case BrowserCommandType::kSearch:
    case BrowserCommandType::kOpenObservedLink:
    case BrowserCommandType::kListTaskTabs:
    case BrowserCommandType::kActivateTaskTab:
    case BrowserCommandType::kCloseTaskTab:
    case BrowserCommandType::kStartDownload:
    case BrowserCommandType::kListDownloads:
    case BrowserCommandType::kCancelDownload:
      return false;
  }
  return false;
}

void NavigationLifecycleTracker::DidFinishNavigation(
    content::NavigationHandle* handle) {
  // An uncommitted navigation changed nothing about what the user is looking
  // at. It can still change whether stop is available, so the control state is
  // refreshed on the way out.
  if (!handle->HasCommitted()) {
    RefreshControlState();
    return;
  }

  TabId tab_id;
  FrameId frame_id;
  if (identity_provider_) {
    tab_id = identity_provider_->GetTabId();
    frame_id = identity_provider_->GetFrameId(handle->GetRenderFrameHost());
  }

  BrowserNavigationRecord record =
      BrowserNavigationRecord::FromCommittedNavigation(handle, tab_id,
                                                       frame_id);

  const content::FrameTreeNodeId node_id = handle->GetFrameTreeNodeId();
  if (record.is_primary_main_frame()) {
    primary_main_frame_node_ = node_id;
  }

  // insert_or_assign rather than operator[]: BrowserNavigationRecord has no
  // public default constructor, which is the point — a record only ever comes
  // from a committed navigation.
  auto [it, inserted] = records_.insert_or_assign(node_id, std::move(record));

  for (Observer& observer : observers_) {
    observer.OnNavigationCommitted(it->second);
  }

  RefreshControlState();
}

void NavigationLifecycleTracker::DidStartLoading() {
  RefreshControlState();
}

void NavigationLifecycleTracker::DidStopLoading() {
  RefreshControlState();
}

void NavigationLifecycleTracker::FrameDeleted(
    content::FrameTreeNodeId node_id) {
  if (records_.erase(node_id) == 0) {
    return;
  }
  if (primary_main_frame_node_.has_value() &&
      *primary_main_frame_node_ == node_id) {
    primary_main_frame_node_.reset();
  }
  for (Observer& observer : observers_) {
    observer.OnFrameRecordDropped(node_id);
  }
}

void NavigationLifecycleTracker::PrimaryPageChanged(content::Page& page) {
  // The primary page moved — a prerender activated, or a portal or a
  // back/forward cache entry became primary. Re-derive the node rather than
  // trusting the one the last commit set, because an activation can make a
  // frame primary without a fresh DidFinishNavigation for it.
  //
  // VERIFY AT SP-04: PrimaryPageChanged(content::Page&) and
  // Page::GetMainDocument() at the pinned milestone. Upstream files to read:
  // content/public/browser/web_contents_observer.h and
  // content/public/browser/page.h. If the signature differs, the fact this
  // method needs — which RenderFrameHost is now primary — is available from
  // WebContents::GetPrimaryMainFrame() either way.
  content::RenderFrameHost& primary = page.GetMainDocument();
  primary_main_frame_node_ = primary.GetFrameTreeNodeId();
  RefreshControlState();
}

void NavigationLifecycleTracker::RefreshControlState() {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return;
  }
  content::NavigationController& controller = contents->GetController();

  NavigationControlState next;
  next.can_go_back = controller.CanGoBack();
  next.can_go_forward = controller.CanGoForward();
  next.is_loading = contents->IsLoading();
  // Stop while loading, reload while not. Stored rather than derived at each
  // call site so that no consumer can invent a third rule and disagree with
  // the toolbar.
  next.can_stop = next.is_loading;
  next.can_reload = !next.is_loading;

  if (next == control_state_) {
    return;
  }
  control_state_ = next;
  for (Observer& observer : observers_) {
    observer.OnNavigationControlStateChanged(control_state_);
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(NavigationLifecycleTracker);

}  // namespace taffy
