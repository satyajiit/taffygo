// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/postcondition_verifier.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/logging.h"
#include "base/functional/bind.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

namespace {

// How often an observation-based postcondition is re-read while waiting.
//
// Polling rather than depending on a subscription keeps the verifier correct
// when no stream exists, which matters because deltas are an optimization and
// correctness must always fall back to a bounded fresh observation
// (protocol section 10). When a stream does exist, OnDeltaObserved short
// circuits the wait, so the poll is the floor on latency rather than the
// mechanism.
//
// The interval is the process minimum delta interval, so the verifier cannot
// ask a renderer for state faster than the endpoint is willing to produce it.
// That number lives in budget_clamp.cc with the rest, and is [Open (OD-031)].
base::TimeDelta ObservationPollInterval() {
  return base::Milliseconds(GetProcessBudgetLimits().min_delta_interval_ms);
}

}  // namespace

PostconditionVerifier::PostconditionVerifier(
    content::WebContents* web_contents,
    base::WeakPtr<PageIntelligenceBroker> broker,
    ResolveNodeCallback resolve_node,
    BrowserEffectSource* browser_effects)
    : content::WebContentsObserver(web_contents),
      broker_(std::move(broker)),
      resolve_node_(std::move(resolve_node)) {
  if (browser_effects) {
    browser_effect_observation_.Observe(browser_effects);
  }
}

PostconditionVerifier::~PostconditionVerifier() = default;

void PostconditionVerifier::Start(const NodeHandle& target,
                                  std::vector<Postcondition> postconditions,
                                  GraphRevision dispatch_revision,
                                  DispatchWatermark dispatch_watermark,
                                  IdempotencyPolicy idempotency,
                                  base::TimeDelta deadline,
                                  CompletionCallback on_complete) {
  CHECK(!on_complete_);
  target_ = target;
  postconditions_ = std::move(postconditions);
  dispatch_revision_ = dispatch_revision;
  dispatch_watermark_ = dispatch_watermark;
  idempotency_ = idempotency;
  started_at_ = base::TimeTicks::Now();
  on_complete_ = std::move(on_complete);
  observation_.dispatch_revision = dispatch_revision;

  // An envelope with no declared effect, or with one that carries no
  // observable claim, never reaches here: the dispatcher refuses it before a
  // capability is consumed. Asserting rather than inventing a default, because
  // "verify nothing" silently succeeding is the exact failure this class
  // exists to prevent.
  CHECK(AllPostconditionsAreVerifiable(postconditions_));

  deadline_timer_.Start(FROM_HERE, deadline,
                        base::BindOnce(&PostconditionVerifier::OnDeadline,
                                       weak_factory_.GetWeakPtr()));

  // The observation-based classes need polling. The browser-observed ones do
  // not: their evidence arrives as an event, and a timer would only burn
  // battery waiting for it.
  const bool needs_observation = std::any_of(
      postconditions_.begin(), postconditions_.end(),
      [](const Postcondition& postcondition) {
        return PostconditionNeedsObservationEvidence(postcondition.kind);
      });
  if (needs_observation) {
    // One immediate read, then the periodic one. Without the immediate read a
    // "no mutation" command would wait a whole interval to say what the broker
    // already knows.
    PollObservation();
    poll_timer_.Start(
        FROM_HERE, ObservationPollInterval(),
        base::BindRepeating(&PostconditionVerifier::PollObservation,
                            weak_factory_.GetWeakPtr()));
  } else {
    Evaluate();
  }
}

bool PostconditionVerifier::Declares(PostconditionKind kind) const {
  return std::any_of(postconditions_.begin(), postconditions_.end(),
                     [kind](const Postcondition& postcondition) {
                       return postcondition.kind == kind;
                     });
}

// --- observed evidence ------------------------------------------------------

void PostconditionVerifier::OnDeltaObserved(const FrameId& frame_id,
                                            GraphRevision revision) {
  if (finished_ || frame_id != target_.frame_id ||
      revision <= dispatch_revision_) {
    return;
  }
  // A delta says something moved. Re-reading now rather than at the next tick
  // is the whole latency benefit of holding a stream, and the resolution still
  // goes through the broker, so the delta is a prompt to look rather than
  // evidence in itself.
  if (!observation_poll_in_flight_) {
    observation_poll_in_flight_ = true;
    resolve_node_.Run(target_,
                      base::BindOnce(&PostconditionVerifier::OnNodeResolved,
                                     weak_factory_.GetWeakPtr(),
                                     VerifierKind::kDeltaObservation));
  }
}

void PostconditionVerifier::PollObservation() {
  if (finished_ || observation_poll_in_flight_) {
    return;
  }
  // A browser-owned command has no node to resolve, so its only observation
  // evidence is whether the handle is still live. Asking the renderer would be
  // asking about a node the proposal never named.
  if (!target_.node_id.is_valid()) {
    observation_.attempted = true;
    observation_.handle_still_live =
        broker_ &&
        !broker_->CheckHandleLiveness(target_, dispatch_revision_).has_value();
    Evaluate();
    return;
  }
  observation_poll_in_flight_ = true;
  resolve_node_.Run(
      target_,
      base::BindOnce(&PostconditionVerifier::OnNodeResolved,
                     weak_factory_.GetWeakPtr(), VerifierKind::kFreshSnapshot));
}

void PostconditionVerifier::OnNodeResolved(
    VerifierKind source,
    std::optional<ResolvedNodeFacts> facts) {
  observation_poll_in_flight_ = false;
  if (finished_) {
    return;
  }
  observation_.attempted = true;
  observation_.source = source;
  observation_.handle_still_live =
      broker_ &&
      !broker_->CheckHandleLiveness(target_, dispatch_revision_).has_value();

  if (!facts.has_value()) {
    observation_.node_resolved = false;
    observation_.node_gone = true;
    observation_.facts.reset();
  } else {
    observation_.node_resolved = true;
    observation_.node_gone = false;
    observation_.observed_at_revision = facts->observed_at_revision;
    observation_.facts = std::move(facts);
  }
  Evaluate();
}

// --- the decision -----------------------------------------------------------

PostconditionCheck PostconditionVerifier::RunCheck(
    const Postcondition& postcondition) const {
  switch (postcondition.kind) {
    case PostconditionKind::kNoMutation:
      return CheckNoMutation(postcondition, observation_);
    case PostconditionKind::kCommittedNavigation:
      return CheckCommittedNavigation(postcondition, navigation_);
    case PostconditionKind::kNewTabCreated:
      return CheckNewTabCreated(postcondition, tab_);
    case PostconditionKind::kSectionVisible:
      return CheckSectionVisible(postcondition, observation_);
    case PostconditionKind::kSearchResultState:
      return CheckSearchResultState(postcondition, navigation_, observation_);
    case PostconditionKind::kNodeStateChanged:
      return CheckNodeStateChanged(postcondition, observation_);
    case PostconditionKind::kNodeValueChanged:
      return CheckNodeValueChanged(postcondition, observation_);
    case PostconditionKind::kBrowserFlowStarted:
      return CheckBrowserFlowStarted(postcondition, flow_, dispatch_watermark_);
    case PostconditionKind::kLoadingStopped:
      return CheckLoadingStopped(postcondition, navigation_);
    case PostconditionKind::kDocumentAdvanced:
      return CheckDocumentAdvanced(postcondition, observation_, navigation_);
  }
  // An unrecognized kind is contradicted, not pending. Fail closed: waiting out
  // the deadline would report a timeout, which reads as "maybe it worked".
  return PostconditionCheck::kContradicted;
}

VerifierKind PostconditionVerifier::VerifierFor(PostconditionKind kind) const {
  switch (kind) {
    case PostconditionKind::kCommittedNavigation:
    case PostconditionKind::kSearchResultState:
    case PostconditionKind::kLoadingStopped:
      return VerifierKind::kBrowserNavigationEvent;
    case PostconditionKind::kDocumentAdvanced:
      // Whichever half settled it. A commit is the browser's own event; an
      // advanced revision or a dead target came from the observation path, and
      // `observation_.source` names which one read it.
      return navigation_.committed ? VerifierKind::kBrowserNavigationEvent
                                   : observation_.source;
    case PostconditionKind::kNewTabCreated:
      return VerifierKind::kBrowserTabEvent;
    case PostconditionKind::kBrowserFlowStarted:
      return VerifierKind::kBrowserTabEvent;
    case PostconditionKind::kNoMutation:
    case PostconditionKind::kSectionVisible:
    case PostconditionKind::kNodeStateChanged:
    case PostconditionKind::kNodeValueChanged:
      return observation_.source;
  }
  return VerifierKind::kFreshSnapshot;
}

void PostconditionVerifier::NoteVerifier(VerifierKind verifier) {
  // kRendererAcknowledgement cannot appear here: this class has no field that
  // holds it. The CHECK states that rather than leaving it to be inferred.
  CHECK_NE(verifier, VerifierKind::kRendererAcknowledgement);
  if (!std::ranges::contains(verified_by_, verifier)) {
    verified_by_.push_back(verifier);
  }
}

void PostconditionVerifier::Evaluate() {
  if (finished_) {
    return;
  }

  bool all_satisfied = true;
  std::optional<PostconditionKind> last_satisfied;
  for (const Postcondition& postcondition : postconditions_) {
    const PostconditionCheck check = RunCheck(postcondition);
    if (check == PostconditionCheck::kContradicted) {
      // Which clause refused, in one line. A contradicted navigation reaches
      // the task as kDestinationChanged and nothing else, and telling "the
      // site sent it to another site" from "a hop on the way was not the
      // destination" needed a journal decode until this existed. Origins
      // only: an origin is what the postcondition compares, and the path a
      // person browsed is not a diagnostic.
      LOG(WARNING) << "[taffy_task_postcondition_contradicted]"
                   << " kind=" << static_cast<int>(postcondition.kind)
                   << " declared="
                   << (postcondition.allowed_origins.empty()
                           ? std::string("none")
                           : postcondition.allowed_origins.front().serialization)
                   << " committed=" << navigation_.committed_origin.serialization
                   << " error_page=" << navigation_.is_error_page
                   << " hops=" << navigation_.redirect_chain.size()
                   << " siblings="
                   << postcondition.allows_registrable_domain_siblings
                   << " redirected_landing="
                   << postcondition.allows_redirected_landing;
      NoteVerifier(VerifierFor(postcondition.kind));
      Finish(ContradictionCodeFor(postcondition.kind), postcondition.kind,
             VerifierOutcome::kContradicted);
      return;
    }
    if (check == PostconditionCheck::kSatisfied) {
      NoteVerifier(VerifierFor(postcondition.kind));
      last_satisfied = postcondition.kind;
      continue;
    }
    all_satisfied = false;
  }

  if (!all_satisfied) {
    return;  // Keep waiting; the deadline timer bounds this.
  }
  // Every declared effect was corroborated by a browser-owned event or by an
  // observation taken after dispatch. This is the only place kVerified is
  // produced, and there is no branch into it that a renderer reply could take.
  Finish(ActionResultCode::kVerified, last_satisfied,
         VerifierOutcome::kCorroborated);
}

void PostconditionVerifier::Finish(ActionResultCode code,
                                   std::optional<PostconditionKind> primary,
                                   VerifierOutcome telemetry) {
  if (finished_) {
    return;
  }
  finished_ = true;
  deadline_timer_.Stop();
  poll_timer_.Stop();

  Outcome outcome;
  outcome.code = code;
  outcome.primary_postcondition = primary;
  outcome.verified_by = verified_by_;
  outcome.telemetry = telemetry;
  outcome.latency = base::TimeTicks::Now() - started_at_;
  if (observation_.observed_at_revision != 0) {
    outcome.observed_graph_revision = observation_.observed_at_revision;
  }

  // One entry per declared postcondition, in declaration order. A reviewer
  // reading a terminal result can see which effects settled and which never
  // did, which a single "verified" flag would hide.
  for (const Postcondition& postcondition : postconditions_) {
    PostconditionOutcome entry;
    entry.postcondition = postcondition;
    entry.satisfied = RunCheck(postcondition) == PostconditionCheck::kSatisfied;
    entry.verifier = VerifierFor(postcondition.kind);
    if (observation_.observed_at_revision != 0) {
      entry.observed_graph_revision = observation_.observed_at_revision;
    }
    outcome.outcomes.push_back(std::move(entry));
  }

  if (on_complete_) {
    std::move(on_complete_).Run(std::move(outcome));
  }
}

}  // namespace taffy
