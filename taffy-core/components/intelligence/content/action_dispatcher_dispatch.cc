// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Steps 7 and 8: journal the intent, then dispatch it — browser-owned actions
// through the delegate, node-targeted actions to the renderer. The order is
// the guarantee: nothing reaches a page that was not journalled first.

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "taffy/components/intelligence/content/action_dispatcher.h"
#include "taffy/components/intelligence/content/action_precondition_evaluator.h"
#include "taffy/components/intelligence/content/action_value_resolution.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace taffy {

std::vector<Precondition> BrowserDerivedRendererGuards(
    ActionType action_type,
    const ResolvedNodeFacts& facts) {
  std::vector<Precondition> guards;
  guards.reserve(3u);

  Precondition role;
  role.kind = PreconditionKind::kNodeRoleUnchanged;
  role.expected_role = facts.role;
  guards.push_back(std::move(role));

  Precondition action;
  action.kind = PreconditionKind::kNodeActionAvailable;
  action.expected_action_type = action_type;
  guards.push_back(std::move(action));

  switch (action_type) {
    case ActionType::kSetText:
    case ActionType::kSelectOption:
    case ActionType::kToggle:
    case ActionType::kSubmitForm: {
      // Browser admission has already accepted the exact live field class.
      // Freeze that class into the renderer's independent re-read as well:
      // without this guard the renderer defaults to kNotSensitive and refuses
      // every legitimately fillable personal/identity field after the browser
      // has journalled it. An unfillable class must never become a permissive
      // ceiling if a future caller reaches this helper without admission.
      Precondition sensitivity;
      sensitivity.kind = PreconditionKind::kNotSensitiveField;
      sensitivity.max_sensitivity =
          FillClearance::For(facts.sensitivity).has_value()
              ? facts.sensitivity
              : Sensitivity::kNotSensitive;
      guards.push_back(std::move(sensitivity));
      break;
    }
    case ActionType::kActivate:
    case ActionType::kFocus:
    case ActionType::kScrollIntoView:
      // The renderer's default kNotSensitive ceiling is the intended stricter
      // rule for non-form actions, with the one exception the browser's own
      // check makes: a scroll into view of a challenge's answer. The
      // renderer is handed that exact class and nothing wider (decision
      // 0244).
      if (NonFormActionMayReach(action_type, facts.sensitivity)) {
        Precondition sensitivity;
        sensitivity.kind = PreconditionKind::kNotSensitiveField;
        sensitivity.max_sensitivity = facts.sensitivity;
        guards.push_back(std::move(sensitivity));
      }
      break;
  }

  return guards;
}

// --- steps 7 and 8 ----------------------------------------------------------

void ActionDispatcher::JournalAndDispatch(const RequestId& request_id,
                                          const ResolvedNodeFacts& facts) {
  PendingAction* pending = Find(request_id);
  if (!pending) {
    return;
  }

  // Step 7: record the dispatch intent in the task journal BEFORE the side
  // effect. Physical storage runs asynchronously on the profile's blocking
  // sequenced writer. Nothing below this call runs until that writer reports a
  // committed row; a failed append is a hard failure because an unrecorded
  // side effect is one nobody can reconcile afterwards.
  DispatchIntentRecord record;
  record.capability_reference = pending->capability().capability_reference;
  record.actor_lease_id = pending->capability().actor_lease_id;
  record.tab_id = pending->tab_id();
  record.idempotency_policy = pending->idempotency();
  record.graph_revision = facts.observed_at_revision;
  record.recorded_at_monotonic_ms = NowMonotonicMs();
  if (pending->envelope) {
    const AuthorizedActionEnvelope& e = *pending->envelope;
    record.dispatch_id = e.dispatch_id;
    record.task_id = e.task_id;
    record.action_id = e.action_id;
    record.action_type = e.action_type;
    record.frame_id = e.target_handle.frame_id;
    record.page_epoch = e.target_handle.page_epoch;
    record.origin = e.target_handle.expected_origin;
  } else {
    const AuthorizedBrowserCommand& c = *pending->command;
    record.dispatch_id = c.dispatch_id;
    record.task_id = c.task_id;
    record.action_id = c.action_id;
    record.command_type = c.command_type;
    if (c.source_handle) {
      record.frame_id = c.source_handle->frame_id;
      record.page_epoch = c.source_handle->page_epoch;
      record.origin = c.source_handle->expected_origin;
    }
  }

  if (!journal_) {
    FinishAction(request_id, ActionResultCode::kDispatchFailed, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  journal_->RecordDispatching(
      std::move(record), MakeJournalCallback(base::BindOnce(
                             &ActionDispatcher::OnDispatchIntentRecorded,
                             weak_factory_.GetWeakPtr(), request_id, facts)));
}

void ActionDispatcher::OnDispatchIntentRecorded(const RequestId& request_id,
                                                ResolvedNodeFacts facts,
                                                bool committed) {
  PendingAction* pending = Find(request_id);
  if (!pending) {
    // Cancellation or teardown won the race. A writer may have committed the
    // intent, but no side effect follows a request that no longer exists. The
    // missing terminal deliberately leaves that durable row ambiguous.
    return;
  }
  if (!committed) {
    FinishAction(request_id, ActionResultCode::kDispatchFailed, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  pending->journalled = true;

  // Committing an intent never re-authorizes it. Cancellation and generation
  // teardown revoke the task's exact capability and lease while the blocking
  // writer may still own this callback; direct user input and expiry can close
  // the same window. Re-read those browser-owned facts before either the
  // renderer or a browser command receives a physical dispatch.
  if (const std::optional<ActionResultCode> refusal =
          RevalidateAdmittedAuthority(*pending, base::TimeTicks::Now())) {
    FinishAction(request_id, *refusal, std::nullopt, std::nullopt, std::nullopt,
                 VerifierOutcome::kNotAttempted);
    return;
  }

  // Storage introduced an asynchronous interval after the last browser
  // liveness check. Navigation invalidation normally cancels the pending
  // action synchronously, but this second check makes that security property
  // local to the dispatch point as well. The renderer guards below remain the
  // independent last check for a same-document node mutation.
  if (pending->envelope) {
    if (!broker_) {
      FinishAction(request_id, ActionResultCode::kTabGone, std::nullopt,
                   std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
      return;
    }
    if (const std::optional<ActionResultCode> liveness =
            broker_->CheckHandleLiveness(
                pending->envelope->target_handle,
                pending->envelope->required_graph_revision)) {
      FinishAction(request_id, *liveness, std::nullopt, std::nullopt,
                   std::nullopt, VerifierOutcome::kNotAttempted);
      return;
    }
    // The journal callback is an asynchronous boundary. Re-resolve the node
    // after it, then repeat every browser-side live gate over the new facts:
    // action availability, sensitivity clearance, state and graph revision.
    // The renderer still repeats its local role/action guards immediately
    // before the accessibility action; this browser pass is what prevents a
    // value from being resolved against pre-journal classification.
    ResolveNodeFacts(
        pending->envelope->target_handle,
        base::BindOnce(&ActionDispatcher::OnPostJournalNodeResolved,
                       weak_factory_.GetWeakPtr(), request_id));
    return;
  }

  DispatchJournalled(*pending, facts);
}

void ActionDispatcher::OnPostJournalNodeResolved(
    const RequestId& request_id,
    std::optional<ResolvedNodeFacts> facts) {
  PendingAction* pending = Find(request_id);
  if (!pending || !pending->envelope) {
    return;
  }
  if (!facts.has_value()) {
    FinishAction(request_id, ActionResultCode::kNodeGone, std::nullopt,
                 std::nullopt, PreconditionKind::kNodeExists,
                 VerifierOutcome::kNotAttempted);
    return;
  }
  if (const auto failure = EvaluatePreconditions(*pending->envelope, *facts)) {
    FinishAction(request_id, failure->second, std::nullopt, std::nullopt,
                 failure->first, VerifierOutcome::kNotAttempted);
    return;
  }
  DispatchJournalled(*pending, *facts);
}

void ActionDispatcher::DispatchJournalled(PendingAction& pending,
                                          const ResolvedNodeFacts& facts) {
  pending.dispatch_revision = facts.observed_at_revision;

  // The browser-flow half of the same watermark. It has to be taken here
  // rather than in step 9: verification starts after the renderer has replied,
  // and a download the click caused can be created and finished before that.
  // A window opened too late would leave the action's own effect outside it,
  // and the honest reading of a flow outside the window is "not ours" — so a
  // late watermark does not merely lose latency, it makes the action
  // unverifiable.
  if (browser_effects_) {
    pending.dispatch_watermark = browser_effects_->NoteDispatch();
  }

  // Step 8: dispatch through the normal browser or renderer input path.
  if (pending.command) {
    DispatchBrowserOwned(pending);
    return;
  }
  DispatchToRenderer(pending, facts);
}

void ActionDispatcher::DispatchBrowserOwned(PendingAction& pending) {
  const AuthorizedBrowserCommand& c = *pending.command;
  const RequestId request_id = pending.request_id;

  if (c.command_type == BrowserCommandType::kOpenObservedLink) {
    if (!c.source_handle || !broker_ ||
        broker_->CheckHandleLiveness(*c.source_handle,
                                     c.source_handle->graph_revision) ||
        !observed_link_resolver_ ||
        observed_link_resolver_->ResolveObservedLink(*c.source_handle) !=
            std::optional<std::string>(c.argument)) {
      FinishAction(request_id, ActionResultCode::kStaleGraph, std::nullopt,
                   std::nullopt, PreconditionKind::kExpectedDestination,
                   VerifierOutcome::kNotAttempted);
      return;
    }
  }

  // Install the browser observer before invoking a platform method. TabModel
  // registration is synchronous and a fast navigation may begin on the same
  // stack; starting afterwards would permanently miss browser-owned evidence.
  StartVerification(pending);
  if (!Find(request_id)) {
    return;
  }

  bool started = false;
  content::WebContents* created_web_contents = nullptr;
  switch (c.command_type) {
    case BrowserCommandType::kNavigate:
      if (web_contents_ && !c.argument.empty()) {
        // Chromium's own navigation path, with its own policies,
        // interstitials and permission surfaces intact. BIP does not replace
        // Chromium's navigation system (protocol section 3.2).
        //
        // Confirmed at the pin (152.0.7977.42): OpenURL is declared on
        // content::PageNavigator, which WebContents derives from, and its
        // navigation-handle callback is a required second parameter with no
        // default — an empty callback is how a caller that does not want to
        // observe the handle spells it, not something that can be omitted.
        content::OpenURLParams params(GURL(c.argument), content::Referrer(),
                                      WindowOpenDisposition::CURRENT_TAB,
                                      ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                      /*is_renderer_initiated=*/false);
        web_contents_->OpenURL(params, /*navigation_handle_callback=*/{});
        started = true;
      }
      break;
    case BrowserCommandType::kOpenObservedLink:
      if (browser_delegate_ && c.source_handle) {
        started = browser_delegate_->StartObservedLinkNavigation(
            c.tab_id, web_contents_, *c.source_handle, c.argument);
      }
      break;
    case BrowserCommandType::kOpenTaskTab:
      if (browser_delegate_) {
        const BrowserActionStart result = browser_delegate_->OpenTaskTab(
            c.task_id, c.action_id, c.tab_id, web_contents_, c.argument);
        started = result.started;
        created_web_contents = result.created_web_contents;
      }
      break;
    case BrowserCommandType::kSearch:
      if (browser_delegate_ && c.transient_search_query) {
        started = browser_delegate_->StartBrowserSearch(
            c.task_id, c.tab_id, web_contents_, *c.transient_search_query,
            c.argument, c.capability.task_discovery);
      }
      break;
    case BrowserCommandType::kGoBack:
    case BrowserCommandType::kGoForward:
    case BrowserCommandType::kReload:
    case BrowserCommandType::kStopLoading:
      if (browser_delegate_) {
        started = browser_delegate_->StartTabControl(c.command_type, c.tab_id,
                                                     web_contents_);
      }
      break;
    case BrowserCommandType::kListTaskTabs:
    case BrowserCommandType::kActivateTaskTab:
    case BrowserCommandType::kCloseTaskTab:
    case BrowserCommandType::kStartDownload:
    case BrowserCommandType::kListDownloads:
    case BrowserCommandType::kCancelDownload:
      // Task-tab and task-download commands return typed Core Service results
      // and are executed by their profile-owned adapters after their own
      // journal append. They can never be projected onto this
      // navigation-shaped dispatcher.
      break;
  }

  if (!started) {
    // No platform layer is wired up, or the command did not carry what it
    // needs. kUnsupported is the honest answer; anything else would claim a
    // browser flow started when none did.
    FinishAction(request_id, ActionResultCode::kUnsupported, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  // `started` is the adapter's statement that a navigation or another
  // browser-owned effect actually began. Journal commitment alone is not a
  // dispatch: keeping the timestamp null until here prevents a refused
  // delegate call from being reported as an attempted side effect.
  PendingAction* current = Find(request_id);
  if (!current) {
    return;
  }
  current->dispatched_at = base::TimeTicks::Now();

  if (created_web_contents) {
    if (!current || !current->verifier) {
      return;
    }
    current->verifier->OnTaskTabCreated(created_web_contents, GURL(c.argument));
  }

  // Step 9 continues in the already-installed verifier. Even a browser-owned
  // command is settled only by browser navigation/tab evidence, never by the
  // platform method's `started` answer.
}

void ActionDispatcher::DispatchToRenderer(PendingAction& pending,
                                          const ResolvedNodeFacts& facts) {
  const AuthorizedActionEnvelope& e = *pending.envelope;
  const RequestId request_id = pending.request_id;

  if (!broker_) {
    FinishAction(request_id, ActionResultCode::kTabGone, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }
  FrameObservationEndpoint* endpoint =
      broker_->GetActionableEndpoint(e.target_handle.frame_id);
  if (!endpoint || !endpoint->remote().is_bound()) {
    FinishAction(request_id, ActionResultCode::kFrameGone, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  // The narrowed one-use command. It carries the renderer-local target and
  // operation and nothing else: no capability reference, no approval receipt,
  // no task or action identity (protocol section 11.3).
  auto command = mojom::RendererActionCommand::New();
  command->schema_version = kBipSchemaVersion;
  command->command_id =
      base::StrCat({"cmd_", base::NumberToString(next_command_value_++)});
  command->frame_id = e.target_handle.frame_id.value;
  command->page_epoch = e.target_handle.page_epoch.value;
  // The floor the caller decided at, not the one this preflight read. The
  // renderer refuses a node whose record changed after the floor it is given
  // (protocol section 12 step 5), so this is what makes "that control is not
  // as the model saw it" a refusal - the policy gate admits a handle read
  // before the page last changed and binds it at its own revision (decision
  // 0188). It can only add refusals: the envelope's floor is never newer
  // than `facts`, which `EvaluatePreconditions` checked. A zero floor names
  // no reading and keeps the preflight's.
  command->required_graph_revision = e.required_graph_revision != 0u
                                         ? e.required_graph_revision
                                         : facts.observed_at_revision;
  command->node_id = e.target_handle.node_id.value;
  command->operation = ToMojom(e.action_type);

  // The named value becomes bytes here, and here only: one statement before
  // the message that carries them is handed to the renderer, and after the
  // journal append above.
  //
  // That order is deliberate in both directions. Resolving *after* the journal
  // keeps the durable record free of the value - what step 7 writes is the
  // dispatch intent, and DispatchIntentRecord has no field a value, a length
  // or a digest of one could go into, which is why an unresolvable reference
  // is a terminal result on an already-recorded intent rather than an
  // unrecorded attempt. Resolving *immediately before the send* is what keeps
  // the bytes' life measured in statements: they exist in one local, are
  // copied into one message, and the vault record they came from is gone.
  //
  // The reference is spent by this call. The dispatcher has already matched
  // the named-reference kind to the exact operation; the resolver repeats the
  // live sensitivity/class/task checks while spending it so a value cannot be
  // rebound after authorization or reused after this statement.
  ResolvedActionInput resolved =
      ResolveActionInput(e, facts, value_references_, base::TimeTicks::Now());
  if (resolved.refusal.has_value()) {
    FinishAction(request_id, *resolved.refusal, std::nullopt, std::nullopt,
                 std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }
  if (resolved.input.has_value()) {
    command->input = ToMojom(*resolved.input);
  }
  // What the browser tells the renderer to stop by. It is a request, not a
  // bound: a renderer that is wedged, spinning, or compromised reads this
  // field and does nothing about it.
  const uint32_t deadline_ms = ClampDeadlineMs(0, GetProcessBudgetLimits());
  command->deadline_ms = deadline_ms;

  for (const Precondition& precondition : e.preconditions) {
    if (mojom::PreconditionPtr wire = ToRendererPrecondition(precondition)) {
      command->renderer_preconditions.push_back(std::move(wire));
    }
  }
  for (const Precondition& precondition :
       BrowserDerivedRendererGuards(e.action_type, facts)) {
    if (mojom::PreconditionPtr wire = ToRendererPrecondition(precondition)) {
      command->renderer_preconditions.push_back(std::move(wire));
    }
  }

  // What the browser holds for itself. The same number as the field above and
  // a different mechanism: that one asks the renderer to stop, this one is a
  // timer in this process, and only the second still produces a result when
  // the renderer never replies (protocol section 15). Two independent layers,
  // drawn from one clamp so they cannot drift apart.
  //
  // Armed onto the pending record before the call goes out, and a copy of the
  // reference is what the reply wrapper carries. The record's reference is the
  // load-bearing one: mojo destroys a pending reply callback on a pipe
  // disconnect instead of running it, so a guard that lived only in the
  // wrapper would die with it and take the timer with it.
  pending.renderer_deadline = RendererCallDeadline::Arm(
      base::Milliseconds(deadline_ms),
      base::BindOnce(&ActionDispatcher::OnRendererActionDeadline,
                     weak_factory_.GetWeakPtr(), request_id));
  scoped_refptr<RendererCallDeadline> guard = pending.renderer_deadline;

  // From this statement onward the one-use renderer command may have caused
  // an effect even if its reply is lost. Set the attempt marker immediately
  // before crossing that seam, never when the earlier journal append lands.
  pending.dispatched_at = base::TimeTicks::Now();

  // kDoDefault on a submit control can run script and commit a navigation on
  // the same stack. Install the browser-owned observer before the renderer
  // call so that fast commit cannot fall between dispatch and verification.
  // Observation-based form claims start after the reply as before: they need
  // the dispatch revision the renderer reports as their lower bound.
  if (e.action_type == ActionType::kSubmitForm) {
    StartVerification(pending);
    if (!Find(request_id)) {
      return;
    }
  }

  endpoint->remote()->ExecuteRendererAction(
      std::move(command),
      BindReplyWithDeadline(
          std::move(guard),
          base::BindOnce(&ActionDispatcher::OnRendererActionResult,
                         weak_factory_.GetWeakPtr(), request_id)));
}

void ActionDispatcher::OnRendererActionDeadline(const RequestId& request_id) {
  // kOutcomeUnknown, and it is the only code that is true here. The command
  // was journalled and then handed to the renderer; the renderer said nothing.
  // The side effect may or may not have happened and the browser cannot prove
  // which, which is exactly what that code means — and why an action settled
  // this way is never retried automatically when repetition could cause an
  // external effect.
  //
  // FinishAction is idempotent per request id, so a deadline that arrives
  // after a cancellation, a navigation invalidation or a lease preemption
  // already settled the action finds nothing to settle and does nothing.
  FinishAction(request_id, ActionResultCode::kOutcomeUnknown, std::nullopt,
               std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
}

void ActionDispatcher::OnRendererActionResult(
    const RequestId& request_id,
    mojom::RendererActionResultPtr result) {
  PendingAction* pending = Find(request_id);
  if (!pending) {
    // Already terminal — cancelled, invalidated by navigation, or the tab went
    // away. A late reply is dropped and counted (protocol section 6.3).
    return;
  }

  if (!result) {
    // The pipe closed with the command outstanding. The effect may or may not
    // have happened.
    FinishAction(request_id, ActionResultCode::kOutcomeUnknown, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  if (result->outcome != mojom::RendererActionOutcome::kDispatched) {
    FinishAction(request_id, FromMojom(result->outcome), std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  // The renderer says it dispatched on the normal input path. That is
  // DISPATCHED. It is not VERIFIED, and there is no branch here that could
  // make it so (protocol section 11.6).
  pending->renderer_acknowledged = true;
  pending->dispatch_revision =
      std::max(pending->dispatch_revision, result->observed_at_revision);
  if (!pending->verifier) {
    StartVerification(*pending);
  }
}

}  // namespace taffy
