// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_VERIFIER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_VERIFIER_H_

#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/components/intelligence/content/browser_effect_source.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/postcondition_checks.h"
#include "taffy/components/intelligence/content/postcondition_evidence.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

// Decides whether an action did what it declared it would do
// (protocol section 11.6, CAP-PI-009).
//
// The rule this class exists to enforce, stated once and enforced structurally
// rather than by care: **renderer acknowledgement alone is DISPATCHED, never
// VERIFIED.** A renderer saying "I dispatched a click on the normal input
// path" is a statement about the renderer. A compromised one would say it
// regardless, and a merely buggy one would say it about the wrong node. So the
// only inputs this class can read are:
//
//   * browser-owned navigation and tab events, taken from Chromium's own
//     records on the UI thread;
//   * a fresh observation or a delta, taken after dispatch, re-resolved
//     through the broker, at a revision strictly newer than dispatch;
//   * a browser-owned flow event, reported by the layer that owns downloads,
//     file choosers and permission prompts.
//
// There is no field anywhere in postcondition_evidence.h for what the renderer
// said, so no check can be written that depends on it, so no path through this
// class can turn it into kVerified. The dispatcher records the acknowledgement
// in ActionResult::verified_by because it happened — not because it counts.
//
// **Every declared postcondition must be satisfied.** Not any. A proposal that
// declares two effects is claiming both, and verifying the easier one would let
// a proposal buy a cheap VERIFIED by declaring one thing it knew would happen
// alongside the thing it actually wanted.
//
// One verifier per action attempt, owned by the dispatcher, producing exactly
// one terminal outcome. Every exit — corroboration, contradiction, deadline,
// cancellation, navigation, crash, tab teardown — runs through Finish(), which
// is idempotent, so the public API's one-terminal-result contract holds by
// construction.
//
// UI thread only.

namespace taffy {

class PageIntelligenceBroker;

class PostconditionVerifier : public content::WebContentsObserver,
                              public BrowserEffectObserver {
 public:
  struct Outcome {
    ActionResultCode code = ActionResultCode::kInternalError;
    // One entry per declared postcondition, in declaration order, so a
    // reviewer can see which of them settled and which never did.
    std::vector<PostconditionOutcome> outcomes;
    // Distinct sources that corroborated something. Never contains
    // kRendererAcknowledgement: this class cannot observe it.
    std::vector<VerifierKind> verified_by;
    // The postcondition that decided the outcome — the last one satisfied on
    // the way to kVerified, or the one contradicted.
    std::optional<PostconditionKind> primary_postcondition;
    std::optional<GraphRevision> observed_graph_revision;
    VerifierOutcome telemetry = VerifierOutcome::kNotAttempted;
    base::TimeDelta latency;
  };

  using CompletionCallback = base::OnceCallback<void(Outcome)>;

  // Re-reads the small set of browser-checkable facts about a node. Supplied
  // by the dispatcher so the verifier needs no mojo remote of its own, and so
  // that pre-dispatch and post-dispatch resolution cannot drift apart.
  using ResolveNodeCallback = base::RepeatingCallback<void(
      const NodeHandle&,
      base::OnceCallback<void(std::optional<ResolvedNodeFacts>)>)>;

  PostconditionVerifier(content::WebContents* web_contents,
                        base::WeakPtr<PageIntelligenceBroker> broker,
                        ResolveNodeCallback resolve_node,
                        BrowserEffectSource* browser_effects);
  PostconditionVerifier(const PostconditionVerifier&) = delete;
  PostconditionVerifier& operator=(const PostconditionVerifier&) = delete;
  ~PostconditionVerifier() override;

  // Begins verification.
  //
  // `dispatch_revision` is the revision observed at the moment of dispatch: an
  // observation-based postcondition is believed only when it is read at a
  // strictly newer revision, so that state which was already true before the
  // action cannot be mistaken for its effect.
  //
  // `dispatch_watermark` is the same idea for the flows this class cannot
  // observe for itself. It is what the browser effect source returned when the
  // dispatcher told it an action was about to go out, and browser-flow
  // evidence counts only when the source bound it to that same watermark —
  // otherwise a download the person started in this tab, seconds after the
  // assistant clicked something else, would read as the assistant's work.
  void Start(const NodeHandle& target,
             std::vector<Postcondition> postconditions,
             GraphRevision dispatch_revision,
             DispatchWatermark dispatch_watermark,
             IdempotencyPolicy idempotency,
             base::TimeDelta deadline,
             CompletionCallback on_complete);

  // A delta advanced this frame's projection. Protocol section 11.6 accepts a
  // delta observation as evidence, so this triggers an immediate re-resolve
  // rather than waiting for the next poll — which is the whole latency benefit
  // of having a stream at all.
  void OnDeltaObserved(const FrameId& frame_id, GraphRevision revision);

  // Evidence from the product's dedicated task-tab creator. It is supplied
  // only after the pre-publication ownership claim and synchronous TabModel
  // registration both succeeded. Verification then follows the created
  // WebContents so success additionally requires its exact committed URL.
  void OnTaskTabCreated(content::WebContents* new_contents, const GURL& url);

  // The user cancelled, or the broker invalidated this tab. Terminal.
  void CancelWith(ActionResultCode code);

  // Settles the action when the navigation it started ended without
  // committing — the browser's own refusal of a hop, told at once rather than
  // waited out (decision 0227).
  void NoteNavigationThatDidNotCommit(content::NavigationHandle& handle);

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* handle) override;
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DidStopLoading() override;
  void DidOpenRequestedURL(content::WebContents* new_contents,
                           content::RenderFrameHost* source_render_frame_host,
                           const GURL& url,
                           const content::Referrer& referrer,
                           WindowOpenDisposition disposition,
                           ui::PageTransition transition,
                           bool started_from_context_menu,
                           bool renderer_initiated) override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;
  void WebContentsDestroyed() override;

  // BrowserEffectObserver:
  void OnBrowserFlowStarted(const BrowserFlowEvidence& evidence) override;

 private:
  void OnDeadline();
  void PollObservation();
  void OnNodeResolved(VerifierKind source,
                      std::optional<ResolvedNodeFacts> facts);

  // Runs every declared check against the evidence held so far and settles if
  // the answer is no longer pending. The single place kVerified is produced.
  void Evaluate();

  PostconditionCheck RunCheck(const Postcondition& postcondition) const;
  VerifierKind VerifierFor(PostconditionKind kind) const;
  void NoteVerifier(VerifierKind verifier);

  // Idempotent: the first call wins and every later call is dropped.
  void Finish(ActionResultCode code,
              std::optional<PostconditionKind> primary,
              VerifierOutcome telemetry);

  bool Declares(PostconditionKind kind) const;

  base::WeakPtr<PageIntelligenceBroker> broker_;
  ResolveNodeCallback resolve_node_;

  NodeHandle target_;
  std::vector<Postcondition> postconditions_;
  GraphRevision dispatch_revision_ = 0;
  // Default-constructed until Start(), and a default-constructed watermark
  // matches nothing — so a verifier that has not been started cannot be
  // corroborated by a flow either.
  DispatchWatermark dispatch_watermark_;
  IdempotencyPolicy idempotency_ = IdempotencyPolicy::kNonIdempotent;
  base::TimeTicks started_at_;
  bool finished_ = false;
  bool observation_poll_in_flight_ = false;

  NavigationEvidence navigation_;
  TabEvidence tab_;
  ObservationEvidence observation_;
  BrowserFlowEvidence flow_;
  // Chromium's id for the one navigation this action started, so a
  // non-committing end can be told from any other navigation in the tab.
  std::optional<int64_t> own_navigation_id_;
  std::vector<VerifierKind> verified_by_;

  base::OneShotTimer deadline_timer_;
  base::RepeatingTimer poll_timer_;
  CompletionCallback on_complete_;

  base::ScopedObservation<BrowserEffectSource, BrowserEffectObserver>
      browser_effect_observation_{this};

  base::WeakPtrFactory<PostconditionVerifier> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_VERIFIER_H_
