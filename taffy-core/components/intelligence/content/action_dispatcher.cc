// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The dispatcher's own state: what it holds, what it hands out, and how a
// pending action is cancelled or invalidated. The four numbered steps of the
// section 12 sequence live in the sibling translation units named for them.

#include "taffy/components/intelligence/content/action_dispatcher.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "taffy/components/intelligence/content/action_digest.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"

namespace taffy {

bool BrowserActionDelegate::StartTabControl(BrowserCommandType,
                                            const TabId&,
                                            content::WebContents*) {
  return false;
}

namespace {

class OnceTaskJournalAppendCallback final : public TaskJournalAppendCallback {
 public:
  explicit OnceTaskJournalAppendCallback(
      base::OnceCallback<void(bool)> callback)
      : callback_(std::move(callback)) {
    CHECK(callback_);
  }

  void Run(bool committed) override {
    CHECK(callback_);
    std::move(callback_).Run(committed);
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

}  // namespace

// --- PendingAction accessors ------------------------------------------------

const CapabilityGrant& ActionDispatcher::PendingAction::capability() const {
  CHECK(envelope.has_value() || command.has_value());
  return envelope ? envelope->capability : command->capability;
}

const TabId& ActionDispatcher::PendingAction::tab_id() const {
  CHECK(envelope.has_value() || command.has_value());
  return envelope ? envelope->target_handle.tab_id : command->tab_id;
}

IdempotencyPolicy ActionDispatcher::PendingAction::idempotency() const {
  CHECK(envelope.has_value() || command.has_value());
  return envelope ? envelope->idempotency_policy : command->idempotency_policy;
}

const std::vector<Postcondition>&
ActionDispatcher::PendingAction::postconditions() const {
  CHECK(envelope.has_value() || command.has_value());
  return envelope ? envelope->expected_postconditions
                  : command->expected_postconditions;
}

// --- construction -----------------------------------------------------------

ActionDispatcher::ActionDispatcher(content::WebContents* web_contents,
                                   base::WeakPtr<PageIntelligenceBroker> broker,
                                   ActorLeaseRegistry* leases,
                                   CapabilityLedger* capabilities,
                                   TaskJournalSink* journal,
                                   ObservabilityRecorder* observability)
    : broker_(std::move(broker)),
      leases_(leases),
      capabilities_(capabilities),
      journal_(journal),
      observability_(observability),
      web_contents_(web_contents) {
  CHECK(leases_);
  CHECK(capabilities_);
  CHECK(observability_);
}

ActionDispatcher::~ActionDispatcher() {
  destroying_ = true;
  // A storage reply may already be queued back to the UI sequence. Invalidate
  // it before settling the pending calls so a committed late intent can never
  // re-enter this object and cause its side effect during teardown.
  weak_factory_.InvalidateWeakPtrs();

  std::vector<RequestId> pending_requests;
  pending_requests.reserve(pending_.size());
  for (const auto& entry : pending_) {
    pending_requests.push_back(entry.first);
  }
  for (const RequestId& request_id : pending_requests) {
    FinishAction(request_id, ActionResultCode::kCancelledByUser, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kCancelled);
  }
}

// static
std::unique_ptr<TaskJournalAppendCallback>
ActionDispatcher::MakeJournalCallback(base::OnceCallback<void(bool)> callback) {
  return std::make_unique<OnceTaskJournalAppendCallback>(std::move(callback));
}

void ActionDispatcher::SetBrowserActionDelegate(
    BrowserActionDelegate* delegate) {
  browser_delegate_ = delegate;
}

void ActionDispatcher::SetObservedLinkResolver(ObservedLinkResolver* resolver) {
  observed_link_resolver_ = resolver;
}

void ActionDispatcher::SetBrowserEffectSource(BrowserEffectSource* source) {
  browser_effects_ = source;
}

void ActionDispatcher::SetValueReferenceVault(ValueReferenceVault* vault) {
  value_references_ = vault;
}

// --- node-targeted dispatch -------------------------------------------------

void ActionDispatcher::Dispatch(const RequestId& request_id,
                                AuthorizedActionEnvelope envelope,
                                CompletionCallback on_complete,
                                bool admit_from_task_grant) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(!destroying_);
  CHECK(request_id.is_valid());
  CHECK(!pending_.contains(request_id));

  PendingAction pending;
  pending.request_id = request_id;
  pending.envelope = std::move(envelope);
  pending.on_complete = std::move(on_complete);
  pending.started_at = base::TimeTicks::Now();
  pending.admit_from_task_grant = admit_from_task_grant;
  pending_.emplace(request_id, std::move(pending));

  const AuthorizedActionEnvelope& e = *pending_.at(request_id).envelope;

  // Refusals that spend nothing.
  //
  // An envelope may name a value and may not carry one (protocol 0.8,
  // ActionInput). This is the browser-process half of that shape rule, and it
  // is the reason a model cannot spell out what goes into a field: there is
  // nowhere upstream of here that bytes could have been written and survived.
  // An envelope arriving with text or an option value in it has had them
  // authored by the assistant runtime, and it is refused rather than used.
  if (!NamedInputMatchesAction(e.action_type, e.input)) {
    FinishAction(request_id, ActionResultCode::kDeniedByPolicy, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }
  if (!e.target_handle.is_well_formed()) {
    FinishAction(request_id, ActionResultCode::kDeniedByPolicy, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  // Steps 1, 2 and 3: the tab in the expected profile, an active document at
  // exactly the expected epoch, and the origin the handle was issued against.
  if (!broker_) {
    FinishAction(request_id, ActionResultCode::kTabGone, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }
  if (const std::optional<ActionResultCode> liveness =
          broker_->CheckHandleLiveness(e.target_handle,
                                       e.required_graph_revision)) {
    FinishAction(request_id, *liveness, std::nullopt, std::nullopt,
                 std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }

  // Step 4.
  const ContentDigest computed = ComputeActionDigest(e);
  if (const std::optional<ActionResultCode> refusal =
          AdmitOrRefuse(pending_.at(request_id), e.action_digest, computed,
                        base::TimeTicks::Now())) {
    FinishAction(request_id, *refusal, std::nullopt, std::nullopt, std::nullopt,
                 VerifierOutcome::kNotAttempted);
    return;
  }

  // Step 5: ask the renderer endpoint to resolve the non-reused node id at the
  // required revision.
  ResolveNodeFacts(
      e.target_handle,
      base::BindOnce(
          [](base::WeakPtr<ActionDispatcher> self, RequestId id,
             std::optional<ResolvedNodeFacts> facts) {
            if (!self) {
              return;
            }
            PendingAction* pending = self->Find(id);
            if (!pending) {
              return;  // Already terminal.
            }
            if (!facts.has_value()) {
              self->FinishAction(id, ActionResultCode::kNodeGone, std::nullopt,
                                 std::nullopt, PreconditionKind::kNodeExists,
                                 VerifierOutcome::kNotAttempted);
              return;
            }
            // Step 6: re-evaluate role, actions, sensitivity, visibility,
            // enabled and editable state, destination, and the
            // proposal-specific preconditions.
            if (const auto failure =
                    self->EvaluatePreconditions(*pending->envelope, *facts)) {
              self->FinishAction(id, failure->second, std::nullopt,
                                 std::nullopt, failure->first,
                                 VerifierOutcome::kNotAttempted);
              return;
            }
            self->JournalAndDispatch(id, *facts);
          },
          weak_factory_.GetWeakPtr(), request_id));
}

void ActionDispatcher::DispatchBrowserCommand(const RequestId& request_id,
                                              AuthorizedBrowserCommand command,
                                              CompletionCallback on_complete,
                                              bool admit_from_task_grant) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(!destroying_);
  CHECK(request_id.is_valid());
  CHECK(!pending_.contains(request_id));

  PendingAction pending;
  pending.request_id = request_id;
  pending.command = std::move(command);
  pending.on_complete = std::move(on_complete);
  pending.started_at = base::TimeTicks::Now();
  pending.admit_from_task_grant = admit_from_task_grant;
  pending_.emplace(request_id, std::move(pending));

  const AuthorizedBrowserCommand& c = *pending_.at(request_id).command;
  if (!broker_ || c.tab_id != broker_->tab_id()) {
    FinishAction(request_id, ActionResultCode::kTabGone, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }
  const bool opens_observed_link =
      c.command_type == BrowserCommandType::kOpenObservedLink;
  if (opens_observed_link != c.source_handle.has_value()) {
    FinishAction(request_id, ActionResultCode::kDeniedByPolicy, std::nullopt,
                 std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
    return;
  }
  if (opens_observed_link) {
    const NodeHandle& handle = *c.source_handle;
    if (!handle.is_well_formed() || handle.tab_id != c.tab_id) {
      FinishAction(request_id, ActionResultCode::kDeniedByPolicy, std::nullopt,
                   std::nullopt, std::nullopt, VerifierOutcome::kNotAttempted);
      return;
    }
    if (const std::optional<ActionResultCode> liveness =
            broker_->CheckHandleLiveness(handle, handle.graph_revision)) {
      FinishAction(request_id, *liveness, std::nullopt, std::nullopt,
                   std::nullopt, VerifierOutcome::kNotAttempted);
      return;
    }
    const std::optional<std::string> current =
        observed_link_resolver_
            ? observed_link_resolver_->ResolveObservedLink(handle)
            : std::nullopt;
    if (!current || *current != c.argument) {
      FinishAction(request_id, ActionResultCode::kStaleGraph, std::nullopt,
                   std::nullopt, PreconditionKind::kExpectedDestination,
                   VerifierOutcome::kNotAttempted);
      return;
    }
  }

  const ContentDigest computed = ComputeBrowserCommandDigest(c);
  if (const std::optional<ActionResultCode> refusal =
          AdmitOrRefuse(pending_.at(request_id), c.action_digest, computed,
                        base::TimeTicks::Now())) {
    FinishAction(request_id, *refusal, std::nullopt, std::nullopt, std::nullopt,
                 VerifierOutcome::kNotAttempted);
    return;
  }

  if (opens_observed_link) {
    ResolveAndDispatchObservedLink(request_id);
    return;
  }

  // Other browser-owned commands have no node to resolve: Chromium's own
  // navigation path is the dispatch mechanism, so steps 5 and 6 have nothing
  // to check beyond what step 1 already proved.
  JournalAndDispatch(request_id, ResolvedNodeFacts());
}

void ActionDispatcher::ResolveNodeFacts(
    const NodeHandle& handle,
    base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> on_resolved) {
  if (!broker_) {
    std::move(on_resolved).Run(std::nullopt);
    return;
  }
  FrameObservationEndpoint* endpoint =
      broker_->GetActionableEndpoint(handle.frame_id);
  if (!endpoint || !endpoint->remote().is_bound()) {
    std::move(on_resolved).Run(std::nullopt);
    return;
  }

  const std::string resolve_id =
      base::StrCat({"resolve_", base::NumberToString(next_command_value_++)});

  auto request = mojom::ResolveNodeRequest::New();
  request->schema_version = kBipSchemaVersion;
  request->request_id = resolve_id;
  request->node_handle = ToMojom(handle);
  request->required_graph_revision = handle.graph_revision;

  // Bounded, like every call that leaves this process (protocol section 15).
  //
  // A resolve carries no budget of its own — the caller hands over a node
  // handle and nothing else — so there is nothing here to derive a deadline
  // from. It therefore takes the process ceiling through the same clamp helper
  // the snapshot and negotiation paths use, which is what keeps one number
  // behind every bounded renderer call rather than a constant per call site.
  //
  // Only one of the two callers was previously bounded at all, and it was
  // bounded by something else: PostconditionVerifier::Start runs its own
  // base::Milliseconds(deadline_ms) timer over the whole verification, so a
  // resolve that never answered on that path eventually surfaced as
  // kPostconditionTimeout. Step 5 of the dispatch sequence had no such bound
  // and is the hole this closes. Bounding the helper itself means both callers
  // are answered from one place instead of one of them being answered twice.
  PendingResolve entry;
  entry.on_resolved = std::move(on_resolved);
  entry.deadline = RendererCallDeadline::Arm(
      base::Milliseconds(ClampDeadlineMs(0, GetProcessBudgetLimits())),
      base::BindOnce(&ActionDispatcher::OnResolveNodeDeadline,
                     weak_factory_.GetWeakPtr(), resolve_id));
  scoped_refptr<RendererCallDeadline> guard = entry.deadline;
  resolves_.emplace(resolve_id, std::move(entry));

  endpoint->remote()->ResolveNode(
      std::move(request),
      BindReplyWithDeadline(
          std::move(guard),
          base::BindOnce(&ActionDispatcher::OnResolveNodeResult,
                         weak_factory_.GetWeakPtr(), resolve_id)));
}

void ActionDispatcher::OnResolveNodeResult(const std::string& resolve_id,
                                           mojom::ResolveNodeResultPtr result) {
  auto it = resolves_.find(resolve_id);
  if (it == resolves_.end()) {
    return;  // The deadline already answered this one.
  }
  PendingResolve entry = std::move(it->second);
  resolves_.erase(it);

  if (!result || result->code != mojom::NodeResolutionCode::kOk ||
      !result->node) {
    std::move(entry.on_resolved).Run(std::nullopt);
    return;
  }
  std::move(entry.on_resolved).Run(FromMojom(*result->node));
}

void ActionDispatcher::OnResolveNodeDeadline(const std::string& resolve_id) {
  auto it = resolves_.find(resolve_id);
  if (it == resolves_.end()) {
    return;  // Already answered. Exactly one answer per resolve.
  }
  PendingResolve entry = std::move(it->second);
  resolves_.erase(it);

  // std::nullopt is this helper's existing failure answer and every caller
  // already handles it: step 5 turns it into kNodeGone against
  // PreconditionKind::kNodeExists, and the verifier treats it as a check that
  // did not corroborate. A renderer that says nothing is not evidence that the
  // node is there, so answering "unresolved" is the honest reading rather than
  // a new code invented for the occasion.
  std::move(entry.on_resolved).Run(std::nullopt);
}

// --- cancellation and invalidation ------------------------------------------

void ActionDispatcher::Cancel(const RequestId& request_id) {
  if (!Find(request_id)) {
    return;
  }
  FinishAction(request_id, ActionResultCode::kCancelledByUser, std::nullopt,
               std::nullopt, std::nullopt, VerifierOutcome::kCancelled);
}

void ActionDispatcher::OnPageInvalidated(
    const InvalidationNotice& notice,
    const std::vector<ActorLeaseId>& revoked_page_leases) {
  // Runs inside the broker's synchronous notification, so an action cannot slip
  // through between the invalidation and the cancellation.
  std::vector<RequestId> affected;
  for (const auto& [request_id, pending] : pending_) {
    const bool lease_revoked = std::ranges::contains(
        revoked_page_leases, pending.capability().actor_lease_id);
    if (pending.command) {
      // The old page's lease is still revoked. Only the already-started
      // browser effect may finish verification; it cannot authorize more work.
      if (!lease_revoked || !pending.dispatched_at.is_null()) {
        continue;
      }
    } else {
      const NodeHandle& handle = pending.envelope->target_handle;
      if (!lease_revoked &&
          (handle.tab_id != notice.tab_id || handle.frame_id != notice.frame_id)) {
        continue;
      }
      // A page action already handed to the renderer, on a document that is
      // still the same document, keeps what the browser command above keeps:
      // it may finish verification, and it authorizes nothing more. A
      // single-page app changing its own route is what a press on it usually
      // does, and that route change routinely reaches the browser before the
      // renderer's reply. Cancelling here reported the press as cut short
      // when it had done what it was for, and the model was told to leave
      // alone the one control that led to the form. The lease stands — a
      // route change retires no epoch — and the press's own postcondition,
      // that the document moved past it, decides it (decision 0242).
      if (!lease_revoked && !notice.retires_page_epoch &&
          !pending.dispatched_at.is_null()) {
        LOG(WARNING) << "[taffy_action_kept_through_route_change]"
                     << " acknowledged=" << pending.renderer_acknowledged
                     << " reason=" << static_cast<int>(notice.reason);
        continue;
      }
    }
    affected.push_back(request_id);
  }
  for (const RequestId& request_id : affected) {
    PendingAction* pending = Find(request_id);
    if (!pending) {
      continue;
    }
    // Already dispatched and consequential: the effect may have happened and
    // the browser can no longer observe whether it did. Never a verified
    // result, and never an automatic replay.
    const bool ambiguous = pending->renderer_acknowledged &&
                           RepeatMayDuplicateEffect(pending->idempotency());
    // A page action the renderer never received, on a document that is still
    // the same document, did nothing: the page moved before it was sent.
    // That is the preflight refusal's own meaning — read the page again —
    // and not a navigation that may have cut an effect short, which the core
    // can only settle by handing the errand to the person (decision 0242).
    const bool moved_before_sent = pending->envelope &&
                                   !notice.retires_page_epoch &&
                                   pending->dispatched_at.is_null();
    FinishAction(
        request_id,
        ambiguous           ? ActionResultCode::kOutcomeUnknown
        : moved_before_sent ? ActionResultCode::kGraphMovedDuringPreflight
                            : ActionResultCode::kCancelledByNavigation,
        std::nullopt, std::nullopt, std::nullopt, VerifierOutcome::kCancelled);
  }
}

void ActionDispatcher::OnActorLeasesPreempted(
    const std::vector<ActorLeaseId>& lease_ids) {
  std::vector<RequestId> affected;
  for (const auto& [request_id, pending] : pending_) {
    if (std::ranges::contains(lease_ids, pending.capability().actor_lease_id)) {
      affected.push_back(request_id);
    }
  }
  for (const RequestId& request_id : affected) {
    PendingAction* pending = Find(request_id);
    if (!pending) {
      continue;
    }
    // Taking over revokes undispatched mutation authority (protocol section
    // 17.4). Work already dispatched cannot be un-dispatched, so it is reported
    // as ambiguous rather than as cancelled.
    FinishAction(
        request_id,
        pending->renderer_acknowledged ? ActionResultCode::kOutcomeUnknown
                                       : ActionResultCode::kCancelledByUser,
        std::nullopt, std::nullopt, std::nullopt, VerifierOutcome::kCancelled);
  }
}

ActionDispatcher::PendingAction* ActionDispatcher::Find(
    const RequestId& request_id) {
  auto it = pending_.find(request_id);
  return it == pending_.end() ? nullptr : &it->second;
}

}  // namespace taffy
