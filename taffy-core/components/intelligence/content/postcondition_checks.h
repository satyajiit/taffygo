// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_CHECKS_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_CHECKS_H_

#include <stdint.h>

#include <vector>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/components/intelligence/content/postcondition_evidence.h"

// One pure check per postcondition class (protocol section 11.6).
//
// Each function answers the same three-valued question — satisfied,
// contradicted, or not yet decided — from a postcondition and the evidence
// available for its class. None of them times anything, cancels anything, or
// touches a Chromium object, so all of them are provable on a host without a
// browser, which is where the interesting cases live: an action that landed on
// the wrong origin, a node that changed to the wrong state, a tab that opened
// with an opener reference it was not supposed to have.
//
// Three-valued rather than boolean because "not yet" and "no" have completely
// different consequences. Not yet means keep waiting until the deadline and
// report POSTCONDITION_TIMEOUT if it never resolves — an outcome that says the
// effect may still be pending. No means the effect happened and it was the
// wrong one, which is POSTCONDITION_FAILED or a more specific code, and is
// terminal immediately. Collapsing the two would turn every contradiction into
// a slow timeout and lose the distinction the result taxonomy depends on.

namespace taffy {

enum class PostconditionCheck : uint8_t {
  // No evidence yet, or the evidence is not newer than dispatch.
  kPending = 0,
  kSatisfied = 1,
  // The effect happened and it was not the declared one. Terminal.
  kContradicted = 2,
};

// Whether a postcondition says anything a browser-owned event or a fresh
// observation could confirm.
//
// An action whose declared effect is unobservable can never be verified, and
// "cannot be verified" and "verified" must not be the same outcome. Rather
// than letting the verifier decide such an action succeeded because nothing
// contradicted it, the dispatcher refuses the envelope before a capability is
// consumed — so this predicate is a policy check that happens at authorization
// time, not a verifier detail.
//
// The rule per kind:
//
//   kNoMutation           always: the claim is that nothing changed, and the
//                         epoch and revision are enough to check it.
//   kCommittedNavigation  needs an expected destination or an allowed-origin
//                         set. "It navigated somewhere" is not a claim.
//   kNewTabCreated        same, plus the opener expectation, which always has
//                         a value.
//   kSectionVisible       always: the claim is that the target is in view and
//                         not obscured once the action has run. A reading no
//                         newer than dispatch may satisfy it and never
//                         contradicts it (decision 0245).
//   kSearchResultState    needs an allowed-origin set, because search is a
//                         browser command and its result is a committed
//                         navigation the browser owns.
//   kNodeStateChanged     needs the state it expects. Without one the claim is
//                         "something about this node is different", which a
//                         revision bump would satisfy for any reason at all.
//   kNodeValueChanged     always: an exact node-local form-state mutation
//                         revision is the claim. No value-derived digest may
//                         cross out of the browser-owned value path.
//   kBrowserFlowStarted   needs the flow kind.
//   kLoadingStopped       always: the browser lifecycle event is the claim.
//   kDocumentAdvanced     always: the claim is that the document moved past
//                         this dispatch, and a commit, a dead target or a
//                         strictly newer revision each say so on their own.
bool PostconditionCarriesAnObservableClaim(const Postcondition& postcondition);

// True when every postcondition in the list carries a claim and the list is
// not empty. What the dispatcher actually calls.
bool AllPostconditionsAreVerifiable(
    const std::vector<Postcondition>& postconditions);

// The result code a contradiction of this class produces. More specific than
// POSTCONDITION_FAILED where the taxonomy has a better word, because the
// isolated core's next legal move differs between "it went somewhere else" and
// "it did not do what it said".
ActionResultCode ContradictionCodeFor(PostconditionKind kind);

PostconditionCheck CheckNoMutation(const Postcondition& postcondition,
                                   const ObservationEvidence& evidence);

PostconditionCheck CheckCommittedNavigation(const Postcondition& postcondition,
                                            const NavigationEvidence& evidence);

PostconditionCheck CheckLoadingStopped(const Postcondition& postcondition,
                                       const NavigationEvidence& evidence);

PostconditionCheck CheckNewTabCreated(const Postcondition& postcondition,
                                      const TabEvidence& evidence);

PostconditionCheck CheckSectionVisible(const Postcondition& postcondition,
                                       const ObservationEvidence& evidence);

// Two-part: the browser's own commit says the search ran, and the observation
// says the results are there. Both are required, which is why this one takes
// both kinds of evidence.
PostconditionCheck CheckSearchResultState(
    const Postcondition& postcondition,
    const NavigationEvidence& navigation,
    const ObservationEvidence& observation);

PostconditionCheck CheckNodeStateChanged(const Postcondition& postcondition,
                                         const ObservationEvidence& evidence);

PostconditionCheck CheckNodeValueChanged(const Postcondition& postcondition,
                                         const ObservationEvidence& evidence);

// The claim an ordinary activation makes: the document moved past this
// dispatch. Two kinds of evidence settle it because either one is enough — a
// committed navigation, or the target being gone or the graph standing at a
// strictly newer revision. It is never contradicted: a page that did not move
// is a page that has not moved *yet*, and the deadline is what ends that wait.
//
// It deliberately says nothing about what the control did. The browser cannot
// know that, and a claim it cannot check is a claim it must not make; the
// caller finds out by reading the page again, which is what it does next
// regardless. An error page is a commit and satisfies this — the document did
// move, and reading it is how the caller learns the move went wrong.
PostconditionCheck CheckDocumentAdvanced(const Postcondition& postcondition,
                                         const ObservationEvidence& observation,
                                         const NavigationEvidence& navigation);

// `dispatch` is the watermark the dispatcher took for this attempt. Evidence
// that is not bound to it is not evidence about this action, however well it
// matches otherwise — the browser-flow analogue of the observation path's
// strictly-newer-revision rule, and the reason a download the person started
// in the same tab cannot settle the assistant's postcondition.
PostconditionCheck CheckBrowserFlowStarted(const Postcondition& postcondition,
                                           const BrowserFlowEvidence& evidence,
                                           const DispatchWatermark& dispatch);

// True when the class is settled by a browser-owned event rather than by an
// observation. Used to decide which evidence a verifier has to wait for.
bool IsBrowserObservedPostcondition(PostconditionKind kind);

// True when the class needs an observation taken after dispatch, so a verifier
// knows whether to poll. This is not the negation of the predicate above:
// kSearchResultState and kDocumentAdvanced each read both kinds of evidence,
// so both functions answer true for them. It is total over the enumeration on
// purpose — the two of them used to be one predicate and one hand-written
// exception beside it at the single call site, which is a shape where a kind
// added later inherits whichever polling behaviour the expression happens to
// give it, silently.
bool PostconditionNeedsObservationEvidence(PostconditionKind kind);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_CHECKS_H_
