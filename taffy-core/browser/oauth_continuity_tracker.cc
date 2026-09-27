// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/oauth_continuity_tracker.h"

#include "base/time/default_tick_clock.h"
#include "taffy/browser/credential_boundary.h"
#include "content/public/browser/browser_thread.h"

namespace taffy {

namespace {

// How long a pending authorization app switch stays resumable.
//
// This is the only place the value exists. It is not a quality target — those
// live in the metric registry — and it is not a version pin. It is the one
// behavioral constant this seam needs, and it is provisional: the secure
// credential handoff design is [Open (OD-056)], and the window it settles on
// belongs to that decision.
//
// The reasoning behind the current value: an app switch that involves
// installing a provider application, a biometric prompt and a password manager
// can legitimately take minutes, while a window measured in hours would let a
// callback resume a tab whose context the user has long forgotten. Ten minutes
// is long enough for the slow legitimate case and short enough that an expired
// handoff is still recognisably the same task.
constexpr base::TimeDelta kHandoffWindow = base::Minutes(10);

bool PathMatches(const std::string& expected, const std::string& actual) {
  // An empty expectation means the site registered no specific path. That is
  // weaker than a registered path and is recorded as such by the caller; here
  // it simply matches.
  return expected.empty() || expected == actual;
}

}  // namespace

OAuthContinuityTracker::OAuthContinuityTracker(CredentialBoundary* boundary)
    : boundary_(boundary), tick_clock_(base::DefaultTickClock::GetInstance()) {}

OAuthContinuityTracker::~OAuthContinuityTracker() = default;

void OAuthContinuityTracker::SetTickClockForTesting(
    const base::TickClock* clock) {
  tick_clock_ = clock ? clock : base::DefaultTickClock::GetInstance();
}

// static
base::TimeDelta OAuthContinuityTracker::HandoffWindow() {
  return kHandoffWindow;
}

OAuthHandoffResult OAuthContinuityTracker::BeginAppSwitch(
    const OAuthHandoffRequest& request) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!request.tab_id.is_valid()) {
    return OAuthHandoffResult::kRefusedInvalidTab;
  }

  switch (request.initiator) {
    case NavigationInitiator::kAssistant:
      // Sending a person into a sign-in flow is not the assistant's to do.
      // Credential handoff design is [Open (OD-056)] and lands no earlier
      // than M5.
      return OAuthHandoffResult::kRefusedAssistantInitiated;
    case NavigationInitiator::kUnknown:
      return OAuthHandoffResult::kRefusedUnattributedInitiator;
    case NavigationInitiator::kUser:
    case NavigationInitiator::kPage:
      break;
  }

  if (!request.has_user_activation) {
    // Every real authorization flow starts with somebody tapping "sign in".
    // One that did not is a page redirecting into a provider on its own, and
    // resuming a tab for it later is not continuity, it is automation.
    return OAuthHandoffResult::kRefusedNoUserActivation;
  }

  if (pending_.find(request.tab_id) != pending_.end()) {
    // Two outstanding handoffs in one tab produce a callback that cannot be
    // attributed to either.
    return OAuthHandoffResult::kRefusedAlreadyPending;
  }

  PendingHandoff handoff;
  handoff.initiating_origin = request.initiating_origin;
  handoff.expected_callback_origin =
      request.expected_callback_origin.is_valid()
          ? request.expected_callback_origin
          : request.initiating_origin;
  handoff.expected_callback_path = request.expected_callback_path;
  handoff.started_at = tick_clock_->NowTicks();
  pending_.insert_or_assign(request.tab_id, handoff);

  if (boundary_) {
    // The assistant cannot observe or act on the tab for the life of the
    // handoff. This is the half a compromised renderer cannot argue with,
    // because the state is browser-owned.
    boundary_->SetAuthorizationHandoffPending(request.tab_id, true);
  }
  return OAuthHandoffResult::kRecorded;
}

OAuthReturnDecision OAuthContinuityTracker::CompleteAppSwitch(
    const OAuthReturnFacts& facts) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  auto it = pending_.find(facts.tab_id());
  if (it == pending_.end()) {
    return OAuthReturnDecision::kRefusedNoPendingHandoff;
  }
  const PendingHandoff handoff = it->second;

  if (tick_clock_->NowTicks() - handoff.started_at > kHandoffWindow) {
    // The tab is still there and the user can start again. What is refused is
    // the automatic resume.
    Clear(facts.tab_id());
    return OAuthReturnDecision::kRefusedExpired;
  }

  // Origin comparison uses the contract's Origin equality, which distinguishes
  // opaque origins by their session-local identifier rather than by a
  // serialization that every opaque origin shares (bip_identity.h).
  if (!(facts.callback_origin() == handoff.expected_callback_origin)) {
    Clear(facts.tab_id());
    return OAuthReturnDecision::kRefusedOriginMismatch;
  }
  if (!PathMatches(handoff.expected_callback_path, facts.callback_path())) {
    Clear(facts.tab_id());
    return OAuthReturnDecision::kRefusedPathMismatch;
  }

  Clear(facts.tab_id());
  // No payload. The caller brings the tab forward and lets Chromium's
  // navigation finish; there is nothing about the callback to pass on, and
  // that is the parity row.
  return OAuthReturnDecision::kResumeWaitingTab;
}

void OAuthContinuityTracker::CancelHandoff(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Clear(tab_id);
}

bool OAuthContinuityTracker::HasPendingHandoff(const TabId& tab_id) const {
  return pending_.find(tab_id) != pending_.end();
}

void OAuthContinuityTracker::Clear(const TabId& tab_id) {
  if (pending_.erase(tab_id) == 0) {
    return;
  }
  if (boundary_) {
    boundary_->SetAuthorizationHandoffPending(tab_id, false);
  }
}

}  // namespace taffy
