// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_DISPATCHER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_DISPATCHER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/common/public/page_intelligence_service.h"
#include "taffy/components/intelligence/content/browser_effect_source.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/observed_link_resolver.h"
#include "taffy/components/intelligence/content/postcondition_checks.h"
#include "taffy/components/intelligence/content/postcondition_verifier.h"
#include "taffy/components/intelligence/content/renderer_call_deadline.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/components/security/browser/value_reference_vault.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

namespace content {
class WebContents;
}  // namespace content

// The action dispatch path (protocol section 12).
//
// The ten steps of the stale-node algorithm are implemented in order, with one
// block per step and the step number in the comment. That is not decoration:
// the order is the security property, and both of the easy mistakes — consuming
// the capability after the renderer command, journalling the intent after the
// side effect — turn a safe failure into an unaccountable one.
//
// Three orderings must never change:
//
//   * The capability is consumed in this process before any renderer command
//     exists (protocol section 4). A renderer receives a bounded one-use
//     command id, never a capability reference.
//   * The dispatch intent is journalled before the side effect. The append is
//     asynchronous on the profile's blocking writer, and dispatch begins only
//     after its committed callback. If it fails, the action does not happen
//     (protocol section 12, step 7).
//   * VERIFIED comes only from the verifier. The renderer's reply moves the
//     attempt to dispatched and nothing further (protocol section 11.6).
//
// UI thread only.
//
// How this class is laid out
//
// One class, four translation units, split on the step boundaries above rather
// than on line count. A step is the unit a reviewer reads and the unit an
// ordering guarantee is about, so each has a file named for it:
//
//   action_dispatcher.cc               state, accessors, cancellation
//   action_dispatcher_admission.cc     steps 4 and 6, decisions over values
//   action_dispatcher_dispatch.cc      steps 7 and 8, journal then dispatch
//   action_dispatcher_verification.cc  steps 9 and 10, verify then finish
//
// The monotonic clock every record is stamped with is monotonic_clock.h, one
// reader for the whole browser side.

namespace taffy {

class PageIntelligenceBroker;

// Guards derived from the browser's just-resolved node facts immediately
// before dispatch. They close the last mutation window: the renderer must see
// the same role and advertised action that browser preflight just admitted.
std::vector<Precondition> BrowserDerivedRendererGuards(
    ActionType action_type,
    const ResolvedNodeFacts& facts);

// Commands Chromium's browser layer performs, which //taffy cannot
// perform for itself because the tab model and the search path live above it.
// Implemented by //taffy/app/android.
struct BrowserActionStart {
  bool started = false;
  // Present only for a successfully published task-owned tab. The verifier
  // switches to this WebContents before its exact navigation commits.
  raw_ptr<content::WebContents> created_web_contents = nullptr;
};

class BrowserActionDelegate {
 public:
  virtual ~BrowserActionDelegate() = default;

  // Opens a tab owned by the task. Returns false when the platform layer
  // refused or is not wired up; the dispatcher then reports kUnsupported rather
  // than pretending the tab exists.
  virtual BrowserActionStart OpenTaskTab(
      const TaskId& task_id,
      const ActionId& action_id,
      const TabId& opener_tab_id,
      content::WebContents* opener_web_contents,
      const std::string& normalized_destination) = 0;

  // Runs a search through the browser's own search and navigation command.
  // Search is a browser command precisely so that the assistant never learns to
  // fill an arbitrary page form (protocol section 11.1).
  virtual bool StartBrowserSearch(
      const TaskId& task_id,
      const TabId& tab_id,
      content::WebContents* web_contents,
      const std::string& query,
      const std::string& normalized_destination,
      const std::optional<TaskDiscoveryCapabilityBinding>& discovery) = 0;

  // Starts the exact browser-resolved navigation for an observed link. The
  // delegate owns the NavigationHandle authority marker consumed by the
  // browser's pre-network throttle; doing this through an ordinary OpenURL in
  // the content component would have no way to stop a redirect before it left
  // the process.
  virtual bool StartObservedLinkNavigation(
      const TabId& tab_id,
      content::WebContents* web_contents,
      const NodeHandle& source_handle,
      const std::string& normalized_destination) = 0;

  // Executes a destination-free back/forward/reload/stop request using the
  // browser's live navigation authority. The default is deliberately a
  // refusal so non-browser test/platform delegates cannot accidentally claim
  // support.
  virtual bool StartTabControl(BrowserCommandType command_type,
                               const TabId& tab_id,
                               content::WebContents* web_contents);
};

class ActionDispatcher {
 public:
  using CompletionCallback = base::OnceCallback<void(ActionResult)>;

  ActionDispatcher(content::WebContents* web_contents,
                   base::WeakPtr<PageIntelligenceBroker> broker,
                   ActorLeaseRegistry* leases,
                   CapabilityLedger* capabilities,
                   TaskJournalSink* journal,
                   ObservabilityRecorder* observability);
  ActionDispatcher(const ActionDispatcher&) = delete;
  ActionDispatcher& operator=(const ActionDispatcher&) = delete;
  ~ActionDispatcher();

  void SetBrowserActionDelegate(BrowserActionDelegate* delegate);
  void SetObservedLinkResolver(ObservedLinkResolver* resolver);

  // Browser-owned effects the verifier cannot observe for itself: a download,
  // a file chooser, a permission prompt, an external intent. Optional — with
  // no source installed, an action declaring kBrowserFlowStarted can never be
  // verified, so the dispatcher refuses it rather than letting it time out.
  void SetBrowserEffectSource(BrowserEffectSource* source);

  // The profile's holder of values a person entered into Taffy's own controls
  // (value_reference_vault.h). Optional, and a dispatcher without one refuses
  // every envelope that names a value rather than dispatching one without it:
  // an action that was authorized to type something and types nothing is a
  // different action.
  void SetValueReferenceVault(ValueReferenceVault* vault);

  // Runs the stale-node algorithm and, if every step passes, dispatches.
  // Exactly one call to `on_complete` follows, always.
  void Dispatch(const RequestId& request_id,
                AuthorizedActionEnvelope envelope,
                CompletionCallback on_complete,
                bool admit_from_task_grant = false);

  // The browser-owned command path. Same authority, same journal, same
  // verification; no node resolution and no renderer command.
  void DispatchBrowserCommand(const RequestId& request_id,
                              AuthorizedBrowserCommand command,
                              CompletionCallback on_complete,
                              bool admit_from_task_grant = false);

  // Terminal, with a cancellation code. A late renderer reply after this is
  // dropped and counted.
  void Cancel(const RequestId& request_id);

  // Forwarded from the broker. Navigation invalidation has priority over queued
  // action work (protocol section 6.3), so these run synchronously inside the
  // broker's notification.
  // Revoked page leases also cancel queued commands outside the invalidated
  // frame. A browser command already dispatched keeps its navigation verifier;
  // page retirement is evidence for that verifier, not user preemption.
  void OnPageInvalidated(
      const InvalidationNotice& notice,
      const std::vector<ActorLeaseId>& revoked_page_leases = {});

  // Forwarded from the delta path. A stream that advanced this frame's
  // projection lets a waiting verifier read the new state now instead of at
  // its next poll (protocol section 11.6 accepts a delta observation).
  void OnDeltaObserved(const FrameId& frame_id, GraphRevision revision);
  void OnActorLeasesPreempted(const std::vector<ActorLeaseId>& lease_ids);

  // Re-reads the browser-checkable facts about a node through the renderer's
  // ResolveNode. Shared with the verifier so that pre-dispatch and
  // post-dispatch resolution cannot drift apart.
  void ResolveNodeFacts(
      const NodeHandle& handle,
      base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> on_resolved);

  // The canonical digest lives in action_digest.h. It moved out of this class
  // when the file grew past the size //taffy treats as a design
  // smell: it is a pure function with a name, and a reviewer checking the
  // encoding should not have to read the dispatch path to do it.

 private:
  struct PendingAction {
    RequestId request_id;
    // Exactly one of these is set.
    std::optional<AuthorizedActionEnvelope> envelope;
    std::optional<AuthorizedBrowserCommand> command;

    CompletionCallback on_complete;
    base::TimeTicks started_at;
    base::TimeTicks dispatched_at;
    GraphRevision dispatch_revision = 0;
    // Taken from the browser effect source in step 7, beside the revision and
    // for the same reason: it is what a flow has to be bound to before the
    // verifier will read it as this action's effect. Stays default — matching
    // nothing — when no source is installed or no flow was dispatched.
    DispatchWatermark dispatch_watermark;
    bool capability_admitted = false;
    // True when policy signed the reducer's proposal digest and this envelope
    // or command was assembled from that grant's live document facts. `Admit`
    // would compare a BIP digest against that proposal digest and refuse
    // every granted click or in-tab navigation; `AdmitTaskAction` and
    // `AdmitTaskBrowserCommand` match the grant instead.
    bool admit_from_task_grant = false;
    bool journalled = false;
    bool renderer_acknowledged = false;
    std::unique_ptr<PostconditionVerifier> verifier;
    // The bounded wait on ExecuteRendererAction's reply. Held here, and not
    // only inside the reply wrapper, because mojo destroys a pending reply
    // callback on a pipe disconnect rather than running it: a guard whose only
    // reference lived there would take its timer with it, and the action would
    // have no terminal result at all.
    scoped_refptr<RendererCallDeadline> renderer_deadline;

    const CapabilityGrant& capability() const;
    const TabId& tab_id() const;
    IdempotencyPolicy idempotency() const;
    const std::vector<Postcondition>& postconditions() const;
  };

  // One outstanding ResolveNode call.
  //
  // A resolve has no pending record of its own to hang a guard from: it is
  // step 5 of a pending action on one path and a repeating callback the
  // postcondition verifier holds on the other, so there is no single struct
  // that outlives both. The dispatcher outlives both, which is why these live
  // on the dispatcher — and living here is what keeps the timer alive when a
  // pipe disconnect destroys the reply callback without running it.
  //
  // The answer callback is held beside the guard rather than inside either
  // outcome's closure, so that whichever of the two settles the call takes it
  // from one place. That is what makes exactly-one structural instead of
  // asserted.
  struct PendingResolve {
    base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> on_resolved;
    scoped_refptr<RendererCallDeadline> deadline;
  };

  // Shared preamble for both dispatch paths: refusals that spend nothing, then
  // step 4's capability admission.
  std::optional<ActionResultCode> AdmitOrRefuse(PendingAction& pending,
                                                const ContentDigest& authorized,
                                                const ContentDigest& computed,
                                                base::TimeTicks now);

  // Storage completion is an asynchronous authority boundary. A task stop,
  // generation replacement, direct-user preemption, or deadline may revoke
  // an already-admitted action while its intent is being committed. Re-read
  // the exact in-flight capability and lease before any physical dispatch.
  std::optional<ActionResultCode> RevalidateAdmittedAuthority(
      const PendingAction& pending,
      base::TimeTicks now) const;

  // Step 6.
  std::optional<std::pair<PreconditionKind, ActionResultCode>>
  EvaluatePreconditions(const AuthorizedActionEnvelope& envelope,
                        const ResolvedNodeFacts& facts) const;

  // Settling one ResolveNode call, from whichever of the two outcomes claimed
  // it. Both take the answer callback out of `resolves_` and erase the entry,
  // so the other outcome finds nothing to answer with.
  void OnResolveNodeResult(const std::string& resolve_id,
                           mojom::ResolveNodeResultPtr result);
  void OnResolveNodeDeadline(const std::string& resolve_id);

  // Steps 7 and 8.
  void JournalAndDispatch(const RequestId& request_id,
                          const ResolvedNodeFacts& facts);
  static std::unique_ptr<TaskJournalAppendCallback> MakeJournalCallback(
      base::OnceCallback<void(bool)> callback);
  void OnDispatchIntentRecorded(const RequestId& request_id,
                                ResolvedNodeFacts facts,
                                bool committed);
  void OnPostJournalNodeResolved(const RequestId& request_id,
                                 std::optional<ResolvedNodeFacts> facts);
  void DispatchJournalled(PendingAction& pending,
                          const ResolvedNodeFacts& facts);
  void DispatchToRenderer(PendingAction& pending,
                          const ResolvedNodeFacts& facts);
  void DispatchBrowserOwned(PendingAction& pending);
  void ResolveAndDispatchObservedLink(const RequestId& request_id);
  void OnRendererActionResult(const RequestId& request_id,
                              mojom::RendererActionResultPtr result);
  // The renderer never answered ExecuteRendererAction.
  void OnRendererActionDeadline(const RequestId& request_id);

  // Step 9.
  void StartVerification(PendingAction& pending);
  void OnVerified(const RequestId& request_id,
                  PostconditionVerifier::Outcome outcome);

  // Step 10. Idempotent per request id: the first terminal result wins.
  //
  // The two trailing parameters carry what only the verifier knows: one
  // outcome per declared postcondition, and every distinct source that
  // corroborated something. They default to empty because most terminal
  // results are refusals that never reached a verifier at all, and a refusal
  // that claimed a postcondition outcome would be inventing evidence.
  void FinishAction(
      const RequestId& request_id,
      ActionResultCode code,
      std::optional<PostconditionKind> verified,
      std::optional<VerifierKind> verifier,
      std::optional<PreconditionKind> failed_precondition,
      VerifierOutcome verifier_outcome,
      std::vector<PostconditionOutcome> postcondition_outcomes = {},
      std::vector<VerifierKind> verified_by = {});

  PendingAction* Find(const RequestId& request_id);

  // raw_ptr per Chromium's pointer-field rule. The authority ledgers are
  // profile-owned and outlive this tab; the remaining collaborators are owned
  // by PageIntelligenceServiceImpl.
  base::WeakPtr<PageIntelligenceBroker> broker_;
  const raw_ptr<ActorLeaseRegistry> leases_;
  const raw_ptr<CapabilityLedger> capabilities_;
  const raw_ptr<TaskJournalSink> journal_;
  const raw_ptr<ObservabilityRecorder> observability_;
  raw_ptr<BrowserActionDelegate> browser_delegate_ = nullptr;
  raw_ptr<ObservedLinkResolver> observed_link_resolver_ = nullptr;
  raw_ptr<BrowserEffectSource> browser_effects_ = nullptr;
  // Profile-owned, like the authority ledgers beside it. A value a person
  // entered is held for the profile and for one task, never for a tab.
  raw_ptr<ValueReferenceVault> value_references_ = nullptr;
  const raw_ptr<content::WebContents> web_contents_;

  std::map<RequestId, PendingAction> pending_;
  // Keyed by the `resolve_` identifier built for the request that is
  // outstanding, which is the only name both outcomes of that call share.
  std::map<std::string, PendingResolve> resolves_;
  uint64_t next_command_value_ = 1;

  bool destroying_ = false;

  base::WeakPtrFactory<ActionDispatcher> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_DISPATCHER_H_
