// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_OAUTH_CONTINUITY_TRACKER_H_
#define TAFFY_BROWSER_OAUTH_CONTINUITY_TRACKER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "taffy/browser/oauth_return_facts.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/taffy_download_intent.h"

// External authorization app-switch continuity (PAR-AUTH-004, PAR-AND-003).
//
// A website hands the user to a provider — an installed application or a
// browser tab at another origin — and the user comes back through a redirect
// to a registered callback. Three things have to be true for that to be both
// usable and safe:
//
//   1. The tab the user left is the tab they come back to, with its session
//      intact. That is Chromium's; the tracker only remembers which tab was
//      waiting so the browser layer can bring it forward.
//   2. Nothing about the callback reaches the assistant. Enforced by
//      OAuthReturnFacts, which discards the query and the fragment at
//      construction — the authorization code, the state parameter and, on the
//      implicit flows, the tokens themselves.
//   3. The assistant cannot observe or act on the tab while the switch is
//      outstanding. Enforced by suspending access through CredentialBoundary
//      for exactly as long as the handoff is pending.
//
// The tracker never starts an app switch and never completes one. The
// navigation is Chromium's, the intent is the Android layer's, and this class
// is the browser-process record that says which tab is waiting for what. That
// is why every method is a decision or a query, and none of them performs.
//
// UI thread only.

namespace taffy {

class CredentialBoundary;

struct OAuthHandoffRequest {
  TabId tab_id;

  // The origin that initiated the authorization — the site the user was on.
  // Compared against the callback's origin on return.
  Origin initiating_origin;

  // The origin the callback is expected to arrive at. Usually the same as
  // initiating_origin; a site that registered a dedicated callback host makes
  // them differ, which is why both are recorded.
  Origin expected_callback_origin;

  // Registered redirect path. Empty means "any path at the expected origin",
  // which is weaker and is recorded as such.
  std::string expected_callback_path;

  NavigationInitiator initiator = NavigationInitiator::kUnknown;
  bool has_user_activation = false;
};

enum class OAuthHandoffResult : uint8_t {
  kRecorded = 0,
  kRefusedUnattributedInitiator = 1,
  // The assistant does not get to send a user into a sign-in flow. Credential
  // handoff design is [Open (OD-056)] and lands no earlier than M5.
  kRefusedAssistantInitiated = 2,
  kRefusedNoUserActivation = 3,
  kRefusedInvalidTab = 4,
  // A second handoff while one is outstanding. Refusing keeps one tab from
  // holding two pending authorizations that a callback could not be attributed
  // between.
  kRefusedAlreadyPending = 5,
};

enum class OAuthReturnDecision : uint8_t {
  // Bring the waiting tab forward and let Chromium's navigation finish. The
  // decision carries no payload, which is the point: there is nothing about
  // the callback for a caller to pass on.
  kResumeWaitingTab = 0,
  kRefusedNoPendingHandoff = 1,
  // The callback arrived at an origin the handoff did not expect. Something is
  // wrong, and continuing would resume a tab against a redirect somebody else
  // chose.
  kRefusedOriginMismatch = 2,
  kRefusedPathMismatch = 3,
  // The handoff outlived its window. The tab is still there and the user can
  // start again; what is refused is the automatic resume.
  kRefusedExpired = 4,
};

class OAuthContinuityTracker {
 public:
  // `boundary` may be null in a test that is only exercising the decisions.
  // In the product it is never null: suspending assistant access for the life
  // of the handoff is half of what this class is for.
  explicit OAuthContinuityTracker(CredentialBoundary* boundary);
  OAuthContinuityTracker(const OAuthContinuityTracker&) = delete;
  OAuthContinuityTracker& operator=(const OAuthContinuityTracker&) = delete;
  ~OAuthContinuityTracker();

  // Test seam. The product uses the default tick clock.
  void SetTickClockForTesting(const base::TickClock* clock);

  OAuthHandoffResult BeginAppSwitch(const OAuthHandoffRequest& request);

  // `facts` has already discarded the callback's query and fragment; this
  // method could not read them if it wanted to.
  OAuthReturnDecision CompleteAppSwitch(const OAuthReturnFacts& facts);

  // The user gave up, the tab closed, or the document navigated away. Clearing
  // the record also resumes assistant access.
  void CancelHandoff(const TabId& tab_id);

  bool HasPendingHandoff(const TabId& tab_id) const;
  size_t pending_count() const { return pending_.size(); }

  // The window a handoff stays valid for. Exposed so that a test can state it
  // rather than restate it, and so that the value lives in exactly one place.
  static base::TimeDelta HandoffWindow();

 private:
  struct PendingHandoff {
    Origin initiating_origin;
    Origin expected_callback_origin;
    std::string expected_callback_path;
    base::TimeTicks started_at;
  };

  void Clear(const TabId& tab_id);

  raw_ptr<CredentialBoundary> boundary_;
  raw_ptr<const base::TickClock> tick_clock_;
  std::map<TabId, PendingHandoff> pending_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_OAUTH_CONTINUITY_TRACKER_H_
