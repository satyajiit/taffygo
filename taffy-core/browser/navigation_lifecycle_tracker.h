// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_NAVIGATION_LIFECYCLE_TRACKER_H_
#define TAFFY_BROWSER_NAVIGATION_LIFECYCLE_TRACKER_H_

#include <map>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "taffy/browser/browser_navigation_record.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"
// VERIFY AT SP-01: content::FrameTreeNodeId as a strong type in its own
// header, matching the assumption page_intelligence_broker.h already makes. If
// the pinned milestone still uses a bare int, change this include and the map
// key type; nothing else in this file depends on the shape.
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class RenderFrameHost;
class WebContents;
}  // namespace content

// The browser-owned navigation and lifecycle record for one WebContents
// (PAR-NAV-001, -002, -003, -004, -006, -007).
//
// One job: keep the last committed BrowserNavigationRecord for every live
// frame, and the control state of the tab, using only facts the browser
// process owns. Everything else asks this class instead of asking a renderer.
//
// Two consumers with two different needs, and the split is why this is a
// separate class from the BIP broker:
//
//   * The browser chrome needs the committed URL, the final origin after
//     redirects, the error and interstitial state, and whether back, forward,
//     reload and stop are available. All of that must work with the AI runtime
//     absent (PAR-AI-BR-001), so it cannot live behind anything the assistant
//     owns.
//   * The BIP broker needs a source of truth for URL and origin that no
//     renderer can influence (protocol section 5.5). It reads this record; it
//     does not maintain a second one.
//
// The dependency points one way. This class knows nothing about page
// intelligence, capabilities, tasks or the assistant, and it must stay that
// way: a navigation record that could be delayed by a model call would be a
// navigation record the address bar could not trust.
//
// Identity is not allocated here. TabId and FrameId belong to the broker,
// which is the single allocator; this class asks the registered
// NavigationIdentityProvider and leaves the fields unset when there is none.
// A record with unset identity is still authoritative for URL, origin, commit
// kind and error state — those are what the browser chrome needs, and the
// chrome runs whether or not the assistant was ever initialised.
//
// UI thread only.

namespace taffy {

// Whether the ordinary navigation controls are available right now
// (PAR-NAV-002). Read from Chromium's NavigationController, never cached
// across a commit, and never gated on anything the assistant owns.
struct NavigationControlState {
  bool can_go_back = false;
  bool can_go_forward = false;
  bool is_loading = false;
  // Stop is available exactly while something is loading, and reload exactly
  // while it is not. They are stored rather than derived so a consumer cannot
  // invent a third rule.
  bool can_stop = false;
  bool can_reload = false;

  friend bool operator==(const NavigationControlState&,
                         const NavigationControlState&) = default;
};

// Supplies contract identity for a frame. Implemented by the BIP broker, which
// is the single allocator of TabId and FrameId.
class NavigationIdentityProvider {
 public:
  virtual ~NavigationIdentityProvider() = default;

  virtual TabId GetTabId() = 0;
  // May return an invalid FrameId for a host the provider does not know about;
  // the tracker records the navigation either way.
  virtual FrameId GetFrameId(content::RenderFrameHost* render_frame_host) = 0;
};

class NavigationLifecycleTracker
    : public content::WebContentsObserver,
      public content::WebContentsUserData<NavigationLifecycleTracker> {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // A navigation committed in this WebContents. The record is owned by the
    // tracker and outlives the call; do not retain the reference past it.
    virtual void OnNavigationCommitted(const BrowserNavigationRecord& record) {}

    // The control state changed: loading started or stopped, or a commit moved
    // the history cursor. Delivered after OnNavigationCommitted for a commit,
    // so an observer that reads both sees a consistent pair.
    virtual void OnNavigationControlStateChanged(
        const NavigationControlState& state) {}

    // A frame went away and its record with it. Anything holding the frame's
    // last record must drop it here rather than waiting to be told again.
    virtual void OnFrameRecordDropped(content::FrameTreeNodeId node_id) {}
  };

  NavigationLifecycleTracker(const NavigationLifecycleTracker&) = delete;
  NavigationLifecycleTracker& operator=(const NavigationLifecycleTracker&) =
      delete;
  ~NavigationLifecycleTracker() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Null clears it. With no provider the records carry unset identity and
  // everything else about them is unchanged.
  void SetIdentityProvider(NavigationIdentityProvider* provider);

  // The last committed navigation for a frame, or null when the frame has not
  // committed one or has gone away.
  const BrowserNavigationRecord* LastCommittedForFrame(
      content::FrameTreeNodeId node_id) const;

  // The last committed navigation of the primary main frame — the record the
  // address bar displays and the record policy is checked against. Null before
  // the first commit.
  const BrowserNavigationRecord* PrimaryMainFrameRecord() const;

  const NavigationControlState& control_state() const { return control_state_; }

  // Executes one ordinary, destination-free control after re-reading the
  // controller state. This is the single authority used by both task-owned
  // controls and browser chrome; callers cannot turn a stale UI boolean into
  // permission to navigate.
  bool ExecuteControl(BrowserCommandType command_type);

  // How many frames currently have a record. Diagnostic only.
  size_t recorded_frame_count() const { return records_.size(); }

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void FrameDeleted(content::FrameTreeNodeId node_id) override;
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<NavigationLifecycleTracker>;

  explicit NavigationLifecycleTracker(content::WebContents* web_contents);

  void RefreshControlState();

  raw_ptr<NavigationIdentityProvider> identity_provider_ = nullptr;
  base::ObserverList<Observer> observers_;

  std::map<content::FrameTreeNodeId, BrowserNavigationRecord> records_;
  std::optional<content::FrameTreeNodeId> primary_main_frame_node_;
  NavigationControlState control_state_;

  // RESOLVED AT SP-01: the milestone still declares both macros, so the pair is
  // required. WebContentsUserData::UserDataKey() returns `&T::kUserDataKey`
  // (content/public/browser/web_contents_user_data.h:88), the _DECL macro is
  // what declares that member, and the _IMPL macro in the .cc defines it.
  // Without this line the .cc fails with "no member named 'kUserDataKey'".
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_NAVIGATION_LIFECYCLE_TRACKER_H_
