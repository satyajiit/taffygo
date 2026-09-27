// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BROWSER_NAVIGATION_RECORD_H_
#define TAFFY_BROWSER_BROWSER_NAVIGATION_RECORD_H_

#include <stddef.h>

#include <vector>

#include "base/time/time.h"
#include "taffy/browser/navigation_error_classifier.h"
#include "taffy/common/public/bip_identity.h"
#include "url/gurl.h"

namespace content {
class NavigationHandle;
}  // namespace content

// The browser process's own record of one committed navigation
// (PAR-NAV-001 URL navigation, -002 back/forward/reload/stop, -003 link open,
// -004 redirect and origin visibility, -006 offline and DNS errors,
// -007 TLS interstitials).
//
// **A renderer can never write one of these.** That is not a convention here,
// it is the shape of the class: the constructor is private, there is no
// setter, no mutable accessor, and no factory that takes a URL or an origin.
// The only way to obtain a record is FromCommittedNavigation(), which reads a
// content::NavigationHandle — a browser-process object whose URL and origin
// come from the navigation stack, not from anything a compromised renderer
// said. Any future code that wants to "correct" the URL from a renderer
// message has to change this file to do it, and that change is the review.
//
// This matters beyond tidiness. The BIP broker reads this record as its source
// of truth for URL and origin (protocol section 5.5), the address bar reads it
// for what to display, and the policy engine reads it to decide whether a
// cross-origin redirect invalidated the scope an action was authorized
// against. A renderer that could influence any one of those could authorize an
// action against a document the user never visited.
//
// What this record deliberately does not hold:
//
//   * the full intermediate redirect URLs. Chromium's NavigationController
//     already owns the entries; a second copy here would be a parallel source
//     of truth that drifts. The record keeps the *origins* of the chain,
//     because origin is what policy is expressed in, plus the length.
//   * page content of any kind — no title, no text, no favicon. Those belong
//     to the renderer's document and would put page-controlled bytes into a
//     browser-owned record.
//   * a wall clock. base::TimeTicks is monotonic, so a record cannot leak
//     browsing time across a trust boundary (bip_identity.h).
//
// UI thread only.

namespace taffy {

// How the document that is now committed came to be. Distinct from the error
// class: a restored back/forward cache entry and an ordinary commit both
// succeed, but only one of them ran the network stack.
enum class NavigationCommitKind : uint8_t {
  kNewDocument = 0,
  kSameDocument = 1,
  kReload = 2,
  // Session restore of a persisted entry (PAR-TAB-003).
  kSessionRestore = 3,
  // Back/forward cache. Whether this allocates a new page epoch is
  // [Open (OD-029)] and is decided in PageIntelligenceBroker, not here.
  kBackForwardCacheRestore = 4,
  kPrerenderActivation = 5,
};

class BrowserNavigationRecord {
 public:
  // The only production producer. `navigation_handle` must have committed;
  // the caller is DidFinishNavigation. Identity is supplied by the caller
  // because the broker owns identity allocation and this class must not become
  // a second allocator.
  static BrowserNavigationRecord FromCommittedNavigation(
      content::NavigationHandle* navigation_handle,
      const TabId& tab_id,
      const FrameId& frame_id);

  BrowserNavigationRecord(const BrowserNavigationRecord&);
  BrowserNavigationRecord& operator=(const BrowserNavigationRecord&);
  BrowserNavigationRecord(BrowserNavigationRecord&&);
  BrowserNavigationRecord& operator=(BrowserNavigationRecord&&);
  ~BrowserNavigationRecord();

  const TabId& tab_id() const { return tab_id_; }
  const FrameId& frame_id() const { return frame_id_; }

  // The URL the browser process committed. Canonical, already through GURL's
  // own parsing, and never replaced by a renderer-reported value.
  const GURL& committed_url() const { return committed_url_; }

  // The origin the browser process committed, in contract form. This is the
  // value PAR-NAV-004 makes visible and the value policy is checked against.
  const Origin& final_origin() const { return final_origin_; }

  // The origin the navigation *started* at, before any redirect. Equal to
  // final_origin() when nothing redirected.
  const Origin& initial_origin() const { return initial_origin_; }

  // Every distinct origin the chain passed through, in order, including the
  // first and the last. One entry when there was no redirect.
  const std::vector<Origin>& redirect_origins() const {
    return redirect_origins_;
  }

  // Server redirects only, as Chromium counts them. Zero for a direct load.
  size_t server_redirect_count() const { return server_redirect_count_; }

  // True when the chain crossed an origin boundary at any point. PAR-NAV-004
  // requires the final origin to be visible and the permission or model scope
  // to be rechecked after a cross-origin redirect; this is the flag that
  // recheck is keyed off, so that no consumer has to walk the chain and get
  // the opaque-origin comparison wrong.
  bool crossed_origin_during_redirect() const {
    return crossed_origin_during_redirect_;
  }

  NavigationCommitKind commit_kind() const { return commit_kind_; }

  bool is_primary_main_frame() const { return is_primary_main_frame_; }
  bool is_same_document() const { return is_same_document_; }

  // True when Chromium attributes a user activation to the navigation. The
  // download and external-intent seam consumes this
  // (public/taffy_download_intent.h); it never substitutes for the initiator.
  bool had_user_gesture() const { return had_user_gesture_; }

  bool is_renderer_initiated() const { return is_renderer_initiated_; }

  const NavigationErrorVerdict& error_verdict() const { return error_verdict_; }

  // Monotonic. Never a wall clock.
  base::TimeTicks committed_at() const { return committed_at_; }

  // True when the document that committed is a real response from the
  // requested resource. The task runtime consults this before recording that
  // it read anything: PAR-NAV-006 requires "no false completion by AI", and an
  // error page is a document too.
  bool CarriesRequestedContent() const;

 private:
  BrowserNavigationRecord();

  TabId tab_id_;
  FrameId frame_id_;
  GURL committed_url_;
  Origin final_origin_;
  Origin initial_origin_;
  std::vector<Origin> redirect_origins_;
  size_t server_redirect_count_ = 0;
  bool crossed_origin_during_redirect_ = false;
  NavigationCommitKind commit_kind_ = NavigationCommitKind::kNewDocument;
  bool is_primary_main_frame_ = false;
  bool is_same_document_ = false;
  bool had_user_gesture_ = false;
  bool is_renderer_initiated_ = false;
  NavigationErrorVerdict error_verdict_;
  base::TimeTicks committed_at_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_BROWSER_NAVIGATION_RECORD_H_
