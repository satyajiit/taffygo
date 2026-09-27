// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/postcondition_checks.h"

#include <algorithm>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace taffy {

namespace {

// Whether `actual` is the same site as `allowed`, answering over https only.
//
// `INCLUDE_PRIVATE_REGISTRIES` is the stricter reading: it keeps hosts under a
// private registry such as a shared pages domain as separate sites, which is
// what a boundary wants. The port has to match too — a different port is a
// different origin for every purpose this product has.
bool SameRegistrableDomain(const Origin& allowed, const Origin& actual) {
  const GURL allowed_url(allowed.serialization);
  const GURL actual_url(actual.serialization);
  return allowed_url.is_valid() && actual_url.is_valid() &&
         allowed_url.SchemeIs(url::kHttpsScheme) &&
         actual_url.SchemeIs(url::kHttpsScheme) &&
         allowed_url.EffectiveIntPort() == actual_url.EffectiveIntPort() &&
         net::registry_controlled_domains::SameDomainOrHost(
             allowed_url, actual_url,
             net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

bool OriginMatches(const Origin& allowed,
                   const Origin& actual,
                   bool allow_siblings) {
  return allowed == actual ||
         (allow_siblings && SameRegistrableDomain(allowed, actual));
}

// Whether a committed URL satisfies a declared destination, using only what
// the destination actually disclosed.
//
// A destination that carried origin-only disclosure is satisfied by an origin
// match. Demanding more than the policy disclosed would make the verifier fail
// on data it deliberately does not have, which would turn a privacy decision
// into a reliability bug (protocol section 7.2).
bool DestinationSatisfied(const Destination& expected,
                          const UrlMetadata& committed,
                          const Origin& committed_origin,
                          bool allow_siblings) {
  if (!OriginMatches(expected.url_metadata.origin, committed_origin,
                     allow_siblings)) {
    return false;
  }
  switch (expected.url_metadata.disclosure) {
    case UrlDisclosure::kOriginOnly:
      return true;
    case UrlDisclosure::kOriginAndPath:
      return expected.url_metadata.path.has_value() &&
             committed.path.has_value() &&
             *expected.url_metadata.path == *committed.path;
    case UrlDisclosure::kFullUrl:
      return expected.url_metadata.url.has_value() &&
             committed.url.has_value() &&
             *expected.url_metadata.url == *committed.url;
  }
  // An unrecognized disclosure level satisfies nothing. Fail closed.
  return false;
}

// Whether this flow is bound to the dispatch that is asking about it.
//
// One flow kind can answer at all. A download carries the watermark its
// witness stamped on it, and comparing that against the dispatcher's is the
// whole of the binding. The other three have no witness — nothing reports
// them, so nothing can bind them — and answering false for those is not a gap
// to be filled later by a default: it is why an action declaring one is
// refused at admission rather than left to time out. When a witness for one of
// them is written, it brings its own binding here, and this switch is where
// the compiler will ask for it.
bool IsAttributedTo(const BrowserFlowEvidence& evidence,
                    const DispatchWatermark& dispatch) {
  switch (evidence.kind) {
    case BrowserFlowKind::kDownload:
      return evidence.download.has_value() &&
             evidence.download->attributed_to.has_value() &&
             *evidence.download->attributed_to == dispatch;
    case BrowserFlowKind::kFileChooser:
    case BrowserFlowKind::kPermissionPrompt:
    case BrowserFlowKind::kExternalIntent:
      return false;
  }
  // An unrecognized kind is bound to nothing. Every other switch in this file
  // carries the same statement for the same reason: the enumerators above are
  // what this build knows, and a value outside them must refuse rather than
  // fall off the end of a function whose answer authorizes a verified result.
  return false;
}

bool OriginAllowed(const std::vector<Origin>& allowed,
                   const Origin& actual,
                   bool allow_siblings = false) {
  return std::any_of(allowed.begin(), allowed.end(),
                     [&actual, allow_siblings](const Origin& origin) {
                       return OriginMatches(origin, actual, allow_siblings);
                     });
}


// Whether the first hop Chromium recorded is the address the request named.
//
// `GetRedirectChain()` begins with the URL the request was made for, so this
// is the claim "we asked for what we said we would" — the half of a declared
// destination that a redirect cannot take away.
bool ChainStartedAtTheDeclaredDestination(const Postcondition& postcondition,
                                          const NavigationEvidence& evidence,
                                          bool allow_siblings) {
  if (evidence.redirect_chain.empty()) {
    return false;
  }
  const NavigationHopEvidence& first = evidence.redirect_chain.front();
  if (!postcondition.allowed_origins.empty() &&
      !OriginAllowed(postcondition.allowed_origins, first.origin,
                     allow_siblings)) {
    return false;
  }
  return !postcondition.expected_destination.has_value() ||
         DestinationSatisfied(*postcondition.expected_destination, first.url,
                              first.origin, allow_siblings);
}

}  // namespace

bool PostconditionCarriesAnObservableClaim(const Postcondition& postcondition) {
  switch (postcondition.kind) {
    case PostconditionKind::kNoMutation:
    case PostconditionKind::kSectionVisible:
      return true;
    case PostconditionKind::kCommittedNavigation:
      return postcondition.expected_destination.has_value() ||
             !postcondition.allowed_origins.empty();
    case PostconditionKind::kNewTabCreated:
      return postcondition.expected_destination.has_value() ||
             !postcondition.allowed_origins.empty();
    case PostconditionKind::kSearchResultState:
      return !postcondition.allowed_origins.empty();
    case PostconditionKind::kNodeStateChanged:
      return postcondition.expected_node_state.has_value();
    case PostconditionKind::kNodeValueChanged:
      // A value-derived digest is itself a disclosure for the small domains
      // form values commonly occupy. The only admissible exact claim is the
      // node-local form-state revision carried by fresh renderer evidence.
      return !postcondition.expected_value_digest.has_value();
    case PostconditionKind::kBrowserFlowStarted:
      return postcondition.expected_flow.has_value();
    case PostconditionKind::kLoadingStopped:
    case PostconditionKind::kDocumentAdvanced:
      return true;
  }
  // An unrecognized kind carries no claim this build can check. Fail closed:
  // the action is refused rather than treated as trivially verifiable.
  return false;
}

bool AllPostconditionsAreVerifiable(
    const std::vector<Postcondition>& postconditions) {
  if (postconditions.empty()) {
    return false;
  }
  return std::all_of(postconditions.begin(), postconditions.end(),
                     PostconditionCarriesAnObservableClaim);
}

ActionResultCode ContradictionCodeFor(PostconditionKind kind) {
  switch (kind) {
    case PostconditionKind::kCommittedNavigation:
    case PostconditionKind::kNewTabCreated:
    case PostconditionKind::kSearchResultState:
      // It went somewhere, and not where the capability allowed. The isolated
      // core's next legal move is a fresh observation, which this code says and
      // kPostconditionFailed would not.
      return ActionResultCode::kDestinationChanged;
    case PostconditionKind::kNoMutation:
    case PostconditionKind::kSectionVisible:
    case PostconditionKind::kNodeStateChanged:
    case PostconditionKind::kNodeValueChanged:
    case PostconditionKind::kBrowserFlowStarted:
    case PostconditionKind::kLoadingStopped:
    // Unreachable for this one: `CheckDocumentAdvanced` answers only satisfied
    // or pending, so a document that has not moved ends at the deadline as
    // POSTCONDITION_TIMEOUT rather than here. The arm exists because the
    // switch is total and a silent default would be how that stops being true.
    case PostconditionKind::kDocumentAdvanced:
      return ActionResultCode::kPostconditionFailed;
  }
  return ActionResultCode::kPostconditionFailed;
}

bool IsBrowserObservedPostcondition(PostconditionKind kind) {
  switch (kind) {
    case PostconditionKind::kCommittedNavigation:
    case PostconditionKind::kNewTabCreated:
    case PostconditionKind::kBrowserFlowStarted:
    case PostconditionKind::kLoadingStopped:
      return true;
    case PostconditionKind::kNoMutation:
    case PostconditionKind::kSectionVisible:
    case PostconditionKind::kNodeStateChanged:
    case PostconditionKind::kNodeValueChanged:
      return false;
    case PostconditionKind::kSearchResultState:
    case PostconditionKind::kDocumentAdvanced:
      // Both. For a search the commit is browser owned and the results are
      // observed, and the check requires each. For an advanced document either
      // one alone settles it, so the verifier has to be listening for both.
      return true;
  }
  return true;
}

bool PostconditionNeedsObservationEvidence(PostconditionKind kind) {
  switch (kind) {
    case PostconditionKind::kNoMutation:
    case PostconditionKind::kSectionVisible:
    case PostconditionKind::kNodeStateChanged:
    case PostconditionKind::kNodeValueChanged:
    case PostconditionKind::kSearchResultState:
    case PostconditionKind::kDocumentAdvanced:
      return true;
    case PostconditionKind::kCommittedNavigation:
    case PostconditionKind::kNewTabCreated:
    case PostconditionKind::kBrowserFlowStarted:
    case PostconditionKind::kLoadingStopped:
      return false;
  }
  // An unrecognized kind is refused before a verifier is built. Polling for it
  // costs one read and cannot make a refusal into a success, so the fail-safe
  // answer here is the one that gathers more evidence rather than less.
  return true;
}

PostconditionCheck CheckNoMutation(const Postcondition& postcondition,
                                   const ObservationEvidence& evidence) {
  if (!evidence.attempted) {
    return PostconditionCheck::kPending;
  }
  // "Nothing changed" is contradicted by the handle dying or by the revision
  // moving past dispatch. Both are things the broker owns, so neither depends
  // on the renderer agreeing.
  if (!evidence.handle_still_live) {
    return PostconditionCheck::kContradicted;
  }
  if (evidence.observed_at_revision > evidence.dispatch_revision) {
    return PostconditionCheck::kContradicted;
  }
  return PostconditionCheck::kSatisfied;
}

PostconditionCheck CheckCommittedNavigation(
    const Postcondition& postcondition,
    const NavigationEvidence& evidence) {
  if (!evidence.committed) {
    return PostconditionCheck::kPending;
  }
  if (evidence.is_error_page) {
    // A commit happened and it was an error document. The declared destination
    // was not reached, and waiting for the deadline would not change that.
    return PostconditionCheck::kContradicted;
  }
  const bool siblings = postcondition.allows_registrable_domain_siblings;
  // A redirect the browser followed is a redirect the navigation authority
  // admitted, hop by hop, as it was requested: the throttle cancels a hop it
  // does not allow, so an inadmissible one never commits. Where a followed
  // link lands after such a chain is therefore already decided, and deciding
  // it again here — against the redirector's own origin, which is all the
  // declared destination can be — refused every search result whose link is a
  // redirector (decision 0179). The chain must still have started where the
  // request was sent.
  if (postcondition.allows_redirected_landing &&
      evidence.redirect_chain.size() > 1u &&
      ChainStartedAtTheDeclaredDestination(postcondition, evidence, siblings)) {
    const GURL committed(evidence.committed_origin.serialization);
    return !evidence.committed_origin.is_opaque() && committed.is_valid() &&
                   committed.SchemeIs(url::kHttpsScheme)
               ? PostconditionCheck::kSatisfied
               : PostconditionCheck::kContradicted;
  }
  if (!postcondition.allowed_origins.empty() &&
      !OriginAllowed(postcondition.allowed_origins, evidence.committed_origin,
                     siblings)) {
    return PostconditionCheck::kContradicted;
  }
  if (postcondition.expected_destination.has_value() &&
      !DestinationSatisfied(*postcondition.expected_destination,
                            evidence.committed_url, evidence.committed_origin,
                            siblings)) {
    return PostconditionCheck::kContradicted;
  }
  for (const NavigationHopEvidence& hop : evidence.redirect_chain) {
    if ((!postcondition.allowed_origins.empty() &&
         !OriginAllowed(postcondition.allowed_origins, hop.origin, siblings)) ||
        (postcondition.expected_destination.has_value() &&
         !DestinationSatisfied(*postcondition.expected_destination, hop.url,
                               hop.origin, siblings))) {
      return PostconditionCheck::kContradicted;
    }
  }
  return PostconditionCheck::kSatisfied;
}

PostconditionCheck CheckLoadingStopped(const Postcondition&,
                                       const NavigationEvidence& evidence) {
  return evidence.loading_stopped ? PostconditionCheck::kSatisfied
                                  : PostconditionCheck::kPending;
}

PostconditionCheck CheckNewTabCreated(const Postcondition& postcondition,
                                      const TabEvidence& evidence) {
  if (!evidence.tab_created) {
    return PostconditionCheck::kPending;
  }
  // The opener policy is checked before the destination, because a tab that
  // can reach its opener is a different thing from the one that was approved
  // whatever it ended up showing.
  if (evidence.has_opener_reference != postcondition.expects_opener_reference) {
    return PostconditionCheck::kContradicted;
  }
  if (!postcondition.allowed_origins.empty() &&
      !OriginAllowed(postcondition.allowed_origins,
                     evidence.destination_origin)) {
    return PostconditionCheck::kContradicted;
  }
  if (postcondition.expected_destination.has_value() &&
      // A new tab is not widened: `kNewTabCreated` carries a destination the
      // browser resolved, not one a model spelled.
      !DestinationSatisfied(*postcondition.expected_destination,
                            evidence.destination_url,
                            evidence.destination_origin,
                            /*allow_siblings=*/false)) {
    return PostconditionCheck::kContradicted;
  }
  return PostconditionCheck::kSatisfied;
}

PostconditionCheck CheckSectionVisible(const Postcondition& postcondition,
                                       const ObservationEvidence& evidence) {
  if (!evidence.attempted) {
    return PostconditionCheck::kPending;
  }
  if (evidence.node_gone) {
    // The thing that was supposed to become visible no longer exists.
    return PostconditionCheck::kContradicted;
  }
  if (!evidence.node_resolved || !evidence.facts.has_value()) {
    return PostconditionCheck::kPending;
  }
  const ResolvedNodeFacts& facts = *evidence.facts;
  // Visible and not obscured. The obscured signal is renderer reported and is
  // honoured only in this direction, which is the same restraint the action
  // path applies while [Open (OD-054)] is unresolved: it can withhold a
  // verification, never grant one.
  const bool in_view = facts.HasState(NodeState::kVisible) &&
                       !facts.HasState(NodeState::kObscured);
  if (!evidence.IsNewerThanDispatch()) {
    // The claim is where the page ends up, not that it changed: a line that
    // was already in view is where a scroll into view leaves it, and such a
    // scroll moves nothing, so no newer reading ever comes. Waiting for one
    // timed a scroll out on a phone and handed its errand to the person
    // (decision 0245). Only this direction is read at the dispatch revision.
    // "Off screen" there may be the reading from before the scroll, so it is
    // waited on rather than believed, and a reading older than the dispatch
    // is not about the page the action ran on at all.
    return in_view &&
                   evidence.observed_at_revision == evidence.dispatch_revision
               ? PostconditionCheck::kSatisfied
               : PostconditionCheck::kPending;
  }
  if (facts.HasState(NodeState::kNotVisible) ||
      facts.HasState(NodeState::kOffscreen)) {
    return PostconditionCheck::kContradicted;
  }
  if (in_view) {
    return PostconditionCheck::kSatisfied;
  }
  return PostconditionCheck::kPending;
}

PostconditionCheck CheckSearchResultState(
    const Postcondition& postcondition,
    const NavigationEvidence& navigation,
    const ObservationEvidence& observation) {
  // The browser-owned half first. Search is a browser command precisely so
  // that its effect is a committed navigation somebody other than the page can
  // confirm (protocol section 11.1).
  const PostconditionCheck committed =
      CheckCommittedNavigation(postcondition, navigation);
  if (committed != PostconditionCheck::kSatisfied) {
    return committed;
  }
  // Then the observed half. Landing on the results origin is not the same as
  // results existing, and an empty result page is a legitimate outcome that
  // the task has to be able to tell apart from a failed search.
  if (!observation.attempted || !observation.IsNewerThanDispatch()) {
    return PostconditionCheck::kPending;
  }
  if (observation.node_gone) {
    return PostconditionCheck::kContradicted;
  }
  if (!observation.node_resolved || !observation.facts.has_value()) {
    return PostconditionCheck::kPending;
  }
  if (postcondition.expected_node_state.has_value() &&
      !observation.facts->HasState(*postcondition.expected_node_state)) {
    return PostconditionCheck::kPending;
  }
  return PostconditionCheck::kSatisfied;
}

PostconditionCheck CheckNodeStateChanged(const Postcondition& postcondition,
                                         const ObservationEvidence& evidence) {
  if (!postcondition.expected_node_state.has_value()) {
    // Unverifiable, and refused at authorization time. Reaching here means
    // something bypassed that check, so the honest answer is a contradiction
    // rather than a success nobody can justify.
    return PostconditionCheck::kContradicted;
  }
  if (!evidence.attempted) {
    return PostconditionCheck::kPending;
  }
  if (evidence.node_gone) {
    return PostconditionCheck::kContradicted;
  }
  if (!evidence.node_resolved || !evidence.facts.has_value() ||
      !evidence.IsNewerThanDispatch()) {
    return PostconditionCheck::kPending;
  }
  return evidence.facts->HasState(*postcondition.expected_node_state)
             ? PostconditionCheck::kSatisfied
             : PostconditionCheck::kPending;
}

PostconditionCheck CheckNodeValueChanged(const Postcondition& postcondition,
                                         const ObservationEvidence& evidence) {
  // A value-derived digest over a small domain is the value. The exact claim
  // for this class is therefore an exact node-local form-state mutation after
  // this dispatch, not equality against material that must never leave the
  // browser-owned value path.
  if (postcondition.expected_value_digest.has_value()) {
    return PostconditionCheck::kContradicted;
  }
  if (!evidence.attempted) {
    return PostconditionCheck::kPending;
  }
  if (evidence.node_gone) {
    return PostconditionCheck::kContradicted;
  }
  if (!evidence.node_resolved || !evidence.facts.has_value() ||
      !evidence.IsNewerThanDispatch()) {
    return PostconditionCheck::kPending;
  }
  return evidence.facts->value_changed_at_revision > evidence.dispatch_revision
             ? PostconditionCheck::kSatisfied
             : PostconditionCheck::kPending;
}

PostconditionCheck CheckDocumentAdvanced(const Postcondition&,
                                         const ObservationEvidence& observation,
                                         const NavigationEvidence& navigation) {
  // A commit is the whole answer and needs no observation behind it. An error
  // document counts: the page the control was on is gone either way, and
  // calling that a contradiction would end the action on a code meaning "do
  // not retry" when the useful next move is to read what actually loaded.
  if (navigation.committed) {
    return PostconditionCheck::kSatisfied;
  }
  if (!observation.attempted) {
    return PostconditionCheck::kPending;
  }
  // The control is gone, or the handle it was minted against is. Either is the
  // document having moved out from under this dispatch.
  if (observation.node_gone || !observation.handle_still_live) {
    return PostconditionCheck::kSatisfied;
  }
  if (!observation.node_resolved || !observation.IsNewerThanDispatch()) {
    // Still the graph this action was dispatched against. Not a contradiction
    // — the page may be about to move — so the deadline is what ends it, and
    // it ends as a timeout, which is what "nothing happened" honestly is.
    return PostconditionCheck::kPending;
  }
  return PostconditionCheck::kSatisfied;
}

PostconditionCheck CheckBrowserFlowStarted(const Postcondition& postcondition,
                                           const BrowserFlowEvidence& evidence,
                                           const DispatchWatermark& dispatch) {
  if (!postcondition.expected_flow.has_value()) {
    return PostconditionCheck::kContradicted;
  }
  if (!evidence.flow_started) {
    return PostconditionCheck::kPending;
  }
  // Attribution is read before the kind, and the order is the point. A flow
  // nobody bound to this dispatch says nothing about this action — not that it
  // succeeded and not that it failed — so it must not be able to contradict a
  // postcondition either. Asking about the kind first would let a download the
  // person started refute an action that declared a file chooser.
  if (!IsAttributedTo(evidence, dispatch)) {
    return PostconditionCheck::kPending;
  }
  return evidence.kind == *postcondition.expected_flow
             ? PostconditionCheck::kSatisfied
             : PostconditionCheck::kContradicted;
}

}  // namespace taffy
