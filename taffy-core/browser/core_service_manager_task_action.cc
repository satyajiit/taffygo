// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "url/gurl.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

// Gives back the lease an action was granted but never used.
//
// A lease is authority for one action, and the registry allows one mutating
// lease per tab at a time. An action refused before anything is dispatched
// never used its lease, and keeping it stops every later mutating move on that
// tab until it expires — up to thirty seconds.
//
// On a phone that was the end of an errand. A `browser.link.open` was granted
// at 13:32:30.208 and refused thirteen milliseconds later by the exactness
// gate below; the `browser.navigate` the model proposed three seconds after
// that came back `[taffy_task_policy_denied] at=actor-lease`, whose recovery is
// `Abandon`, and the task stopped on the site it had just reached (decision
// 0174).
void ReleaseUnusedActionLease(CapabilityLedger& capabilities,
                              ActorLeaseRegistry& actor_leases,
                              const service_mojom::TaskActionEffect& action) {
  CapabilityGrant capability;
  if (capabilities.CopyRegisteredCapability(
          CapabilityReference{action.capability_id}, capability)) {
    actor_leases.Release(capability.actor_lease_id);
  }
}

}  // namespace

void CoreServiceManager::ExecuteTaskPageAction(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const service_mojom::TaskActionEffect& action = *effect->action;
  const std::optional<TaskPolicyDocumentContext> live =
      ResolveTaskPolicyDocument(browser_context_.get(),
                                action.executable->tab_id);
  // Six clauses, and this refusal is invisible everywhere else: it is not a
  // policy denial, so no `[taffy_task_policy_*]` line covers it, and it never
  // reaches the dispatcher, so no `[taffy_action_refused]` line does either.
  //
  // Naming them in the log was half the fix. The completion still carried no
  // code, so the bridge settled every one as `DeniedByPolicy` — `DoNotRetry`,
  // a standing prohibition — for a document that had moved and would answer a
  // fresh look perfectly well. Each clause now answers its own code
  // (decision 0207).
  const char* refused_at = nullptr;
  service_mojom::TaskActionResultCode refused_code =
      service_mojom::TaskActionResultCode::kDocumentInactive;
  if (!live) {
    refused_at = "no-live-document";
    refused_code = service_mojom::TaskActionResultCode::kDocumentInactive;
  } else if (live->frame_id != action.document->frame_id) {
    refused_at = "frame-changed";
    refused_code = service_mojom::TaskActionResultCode::kFrameGone;
  } else if (live->page_epoch != action.document->page_epoch) {
    refused_at = "epoch-changed";
    refused_code = service_mojom::TaskActionResultCode::kStalePageEpoch;
  } else if (live->origin != action.document->normalized_origin) {
    refused_at = "origin-changed";
    refused_code = service_mojom::TaskActionResultCode::kOriginChanged;
  } else if (live->graph_revision < action.document->graph_revision) {
    refused_at = "graph-behind-the-action";
    refused_code =
        service_mojom::TaskActionResultCode::kGraphMovedDuringPreflight;
  } else if (!accepted_approvals_.IsTaskSourceAuthorized(
                 effect->task_id, action.executable->tab_id, *effect->operation,
                 live->origin, service_generation_)) {
    refused_at = "source-not-authorized";
    refused_code = service_mojom::TaskActionResultCode::kEgressNotAuthorized;
  }
  if (refused_at) {
    LOG(WARNING) << "[taffy_task_page_action_refused] at=" << refused_at;
    ReleaseUnusedActionLease(capabilities_, actor_leases_, action);
    std::move(callback).Run(
        MakeRefusedTaskActionCompletion(*effect, refused_code));
    return;
  }
  service_mojom::TaskEffectBindingPtr retained = effect.Clone();
  DispatchTaskActionOnTabForCore(
      browser_context_.get(), action, effect->task_id,
      effect->operation->deadline_monotonic_ms, capabilities_,
      base::BindOnce(&CoreServiceManager::OnTaskActionCompleted,
                     weak_factory_.GetWeakPtr(), std::move(retained),
                     std::move(callback)));
}

void CoreServiceManager::ExecuteTaskNavigate(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const service_mojom::TaskActionEffect& action = *effect->action;
  // Two documents carry no site of their own and they are not the same thing.
  // One is the browser-created blank of a zero-source errand: whatever the
  // model does there — the search it was built for, or a typed navigate to
  // the site it already knows — is judged against the discovery authority
  // rather than a live document. The other is the error document Chromium
  // writes when an address does not answer, which the task is leaving. The
  // capability says which: only a discovery grant carries discovery authority
  // (decision 0176).
  std::optional<TaskDiscoveryCapabilityBinding> discovery_authority;
  if (action.document->opaque_origin_id) {
    CapabilityGrant capability;
    if (capabilities_.CopyRegisteredCapability(
            CapabilityReference{action.capability_id}, capability)) {
      discovery_authority = std::move(capability.task_discovery);
    }
  }
  const bool on_discovery_tab =
      action.document->opaque_origin_id && discovery_authority;
  const bool leaving_a_document_with_no_site =
      action.document->opaque_origin_id && !discovery_authority;
  std::optional<TaskPolicyDocumentContext> live;
  std::optional<TaskDiscoveryDocumentContext> discovery_live;
  std::optional<TaskDepartureDocumentContext> departure;
  if (on_discovery_tab) {
    discovery_live = ResolveTaskDiscoveryDocument(browser_context_.get(),
                                                  action.executable->tab_id);
  } else if (leaving_a_document_with_no_site) {
    departure = ResolveTaskDepartureDocument(browser_context_.get(),
                                             action.executable->tab_id);
  } else {
    live = ResolveTaskPolicyDocument(browser_context_.get(),
                                     action.executable->tab_id);
  }
  bool destination_is_exact = false;
  if (live && IsTaskTabControlOperation(action.executable->operation_kind)) {
    destination_is_exact = IsValidTaskTabControlAction(action);
  } else if ((live || discovery_live || departure) &&
             action.executable->destination_origin) {
    if (IsTaskNavigationOperation(action.executable->operation_kind)) {
      // The grant's destination origin is the typed address's own, resolved
      // by the browser before policy was asked, so a navigate is exact when
      // the address still leads there and is https. It may leave the origin
      // the tab is on: where it lands is admitted, counted and bound when the
      // outcome comes back, below.
      destination_is_exact =
          IsValidInTabNavigateAction(action) &&
          GURL(*action.executable->destination_address).SchemeIs("https");
    } else if (IsTaskSearchOperation(action.executable->operation_kind) &&
               IsValidTaskOwnedBrowserAction(action) &&
               action.executable->transient_search_query &&
               action.executable->destination_address) {
      const std::optional<std::string> resolved =
          ResolveTaskSearchAddress(action.executable->tab_id,
                                   *action.executable->transient_search_query);
      destination_is_exact =
          resolved && *resolved == *action.executable->destination_address;
    } else if (IsTaskTabOpenOperation(action.executable->operation_kind)) {
      destination_is_exact = IsValidTaskOwnedBrowserAction(action);
    } else if (IsTaskLinkOpenOperation(action.executable->operation_kind)) {
      // Five clauses, and the one that fired on a phone was none of the ones
      // that already log. `ObservedLinkHandleIsCurrent` and the registry both
      // name their own refusals; a malformed action and an unreadable handle
      // named nothing, so a granted link open died here in silence and the
      // only evidence was a journal decode (decision 0174).
      const char* at = nullptr;
      if (!IsValidObservedLinkOpenAction(action)) {
        at = "action-shape";
      } else {
        const std::optional<CanonicalLinkOpenHandle> handle =
            ReadCanonicalLinkOpenHandle(action.executable->canonical_intent);
        const std::optional<std::string> resolved =
            handle ? ResolveTaskObservedLink(browser_context_.get(), *handle)
                   : std::nullopt;
        if (!handle) {
          at = "handle-unreadable";
        } else if (!resolved) {
          at = "not-in-registry";
        } else if (!action.executable->destination_address) {
          at = "no-destination";
        } else if (*resolved != *action.executable->destination_address) {
          at = "destination-differs";
        }
      }
      if (at) {
        LOG(WARNING) << "[taffy_link_open_dispatch_refused] at=" << at;
      }
      destination_is_exact = at == nullptr;
    }
  }
  bool source_is_exact = false;
  if (on_discovery_tab && discovery_live && discovery_authority) {
    TaffyPageIntelligenceHost* host = FindPageIntelligenceHost(
        browser_context_.get(), action.executable->tab_id);
    content::WebContents* web_contents =
        host ? host->observed_web_contents() : nullptr;
    source_is_exact =
        discovery_authority->browser_session_id == browser_session_id_ &&
        discovery_authority->tab_id.value == action.executable->tab_id &&
        discovery_authority->frame_id.value == action.document->frame_id &&
        discovery_authority->page_epoch.value == action.document->page_epoch &&
        discovery_authority->opaque_origin_id ==
            *action.document->opaque_origin_id &&
        discovery_live->frame_id == action.document->frame_id &&
        discovery_live->page_epoch == action.document->page_epoch &&
        discovery_live->opaque_origin_id ==
            *action.document->opaque_origin_id &&
        accepted_approvals_.HasTaskSourceDiscoveryBootstrapAuthority(
            effect->task_id, *effect->operation, service_generation_,
            browser_session_id_,
            discovery_authority->remaining_new_source_cap) &&
        IsExactTaskDiscoveryTab(
            effect->task_id, action.executable->tab_id, browser_session_id_,
            discovery_authority->remaining_new_source_cap, web_contents);
  } else if (departure) {
    // The document being left is identified, not consented to: the same frame
    // and epoch the grant was bound against, no origin at all, no graph, and
    // a tab this task holds. What may be reached from here was decided by the
    // destination above.
    source_is_exact =
        departure->frame_id == action.document->frame_id &&
        departure->page_epoch == action.document->page_epoch &&
        departure->opaque_origin_id == *action.document->opaque_origin_id &&
        action.document->normalized_origin.empty() &&
        action.document->graph_revision == 0u &&
        accepted_approvals_.IsTaskTabAuthorized(
            effect->task_id, action.executable->tab_id, *effect->operation,
            service_generation_);
  } else if (live) {
    source_is_exact =
        live->frame_id == action.document->frame_id &&
        live->page_epoch == action.document->page_epoch &&
        live->origin == action.document->normalized_origin &&
        live->graph_revision >= action.document->graph_revision &&
        accepted_approvals_.IsTaskSourceAuthorized(
            effect->task_id, action.executable->tab_id, *effect->operation,
            live->origin, service_generation_);
  }
  if (!source_is_exact || !destination_is_exact) {
    // Two booleans stand for about twenty clauses between them, and the
    // link-open branch above names its own. Everything here is a flag or a
    // compiled-in enumeration member.
    LOG(WARNING) << "[taffy_task_navigate_refused]"
                 << " op="
                 << static_cast<int>(action.executable->operation_kind)
                 << " source=" << (source_is_exact ? 1 : 0)
                 << " destination=" << (destination_is_exact ? 1 : 0)
                 << " discovery=" << (on_discovery_tab ? 1 : 0)
                 << " departing=" << (leaving_a_document_with_no_site ? 1 : 0)
                 << " live=" << (live ? 1 : 0)
                 << " discovery_live=" << (discovery_live ? 1 : 0)
                 << " departure=" << (departure ? 1 : 0)
                 << " dest_origin="
                 << (action.executable->destination_origin ? 1 : 0)
                 << " dest_address="
                 << (action.executable->destination_address ? 1 : 0);
    ReleaseUnusedActionLease(capabilities_, actor_leases_, action);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  service_mojom::TaskEffectBindingPtr retained = effect.Clone();
  DispatchTaskNavigateOnTabForCore(
      browser_context_.get(), action, effect->task_id,
      effect->operation->deadline_monotonic_ms, capabilities_,
      base::BindOnce(&CoreServiceManager::OnTaskActionCompleted,
                     weak_factory_.GetWeakPtr(), std::move(retained),
                     std::move(callback)));
}

void CoreServiceManager::OnTaskActionCompleted(
    service_mojom::TaskEffectBindingPtr binding,
    ExecuteTaskEffectCallback callback,
    std::optional<ActionResult> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  service_mojom::TaskEffectCompletionStatus status =
      result ? CompletionStatusForActionResult(result->result_code)
             : service_mojom::TaskEffectCompletionStatus::kUnavailable;
  if (!binding || !binding->operation || !binding->action ||
      !binding->action->document || !binding->action->executable ||
      (result && (result->action_id.value != binding->action->action_id ||
                  result->dispatch_id.value != binding->action->dispatch_id))) {
    status = service_mojom::TaskEffectCompletionStatus::kRefused;
    result.reset();
  }
  auto completion = MakeTaskEffectCompletion(binding.get(), status);
  if (status != service_mojom::TaskEffectCompletionStatus::kSucceeded) {
    // A refused action names the code it was refused with.
    //
    // `TaskEffectCompletionStatus` has one refusal member, so a node that
    // moved, a control that is not available and a policy denial all reached
    // the reducer as the one generic denial — which its recovery table reads
    // as "this will be decided the same way again; do not retry". A press
    // refused `kRoleOrActionChanged` on a live portal therefore ended the
    // errand in a hand-back instead of another look. The exact code is right
    // here; only it crosses, and only for a refusal the browser computed one
    // for.
    const std::optional<service_mojom::TaskActionResultCode> refused_code =
        result ? core_service::wire::TaskActionResultCodeFromWire(
                     static_cast<uint32_t>(result->result_code))
               : std::nullopt;
    if (refused_code &&
        status == service_mojom::TaskEffectCompletionStatus::kRefused) {
      auto refusal = service_mojom::EffectResult::New();
      refusal->operation = binding->operation.Clone();
      refusal->effect_id = binding->effect_id;
      refusal->status = service_mojom::EffectStatus::kDenied;
      refusal->kind = service_mojom::EffectKind::kBrowserAction;
      refusal->browser_action = service_mojom::BrowserActionEffectResult::New();
      refusal->browser_action->outcome =
          service_mojom::BrowserActionOutcome::kRefused;
      refusal->browser_action->dispatch_id = binding->action->dispatch_id;
      refusal->browser_action->refused_code =
          service_mojom::TaskActionRefusal::New(*refused_code);
      completion->effect_result = std::move(refusal);
    }
    std::move(callback).Run(std::move(completion));
    return;
  }

  const service_mojom::TaskActionEffect& action = *binding->action;
  const bool opens_tab =
      IsTaskTabOpenOperation(action.executable->operation_kind);
  const bool crosses_origin = action.executable->destination_origin &&
                              (action.document->opaque_origin_id ||
                               action.document->normalized_origin !=
                                   *action.executable->destination_origin);
  // A tab can land somewhere its own proposal never named. A site is free to
  // answer on a sibling host of its own registrable domain — decision 0155
  // already calls that the site answering the request, and the navigate's
  // postcondition is satisfied by it — and a page is free to do it with a
  // client-side route after the commit, which no proposal can predict at all.
  // The consent record still holds the origin the source was issued for, so a
  // move that ends somewhere else leaves the tab bound to a row that names
  // where it used to be. Offering a fresh source for the tab is the mechanism
  // already built for that, and the ledger and the sites cap decide whether to
  // admit it, exactly as they do for a move that named its destination.
  //
  // The comparison is byte equality against the origin the source was issued
  // for, deliberately, and not `IsTaskSourceAuthorized`: that predicate admits
  // a same-site sibling, which is the whole case this is here to notice. The
  // browser authorizing the read is not the same question as the ledger
  // holding a row that still says where the tab is.
  //
  // Only a move that lands its own tab may offer a source for it. The core's
  // terminal decoder admits a discovered source on exactly that set, so
  // offering one on a read, a query or a press produces an envelope it refuses
  // whole — which ends the core, not the action. Nothing is lost by staying
  // silent on those: a tab that moved within its own site is still authorized,
  // because the browser decided that with the registrable-domain table when it
  // admitted the move, and the core matches its own source row by tab.
  const bool may_settle_its_tab =
      !opens_tab && !crosses_origin &&
      TaskActionOperationSettlesItsOwnTab(action.executable->operation_kind);
  const service_mojom::TaskConsentSourcePtr held =
      may_settle_its_tab ? accepted_approvals_.FindTaskSourceForDisplay(
                               binding->task_id, action.executable->tab_id,
                               binding->operation->task_revision,
                               service_generation_)
                         : nullptr;
  const std::optional<TaskPolicyDocumentContext> settled =
      held ? ResolveTaskPolicyDocument(browser_context_.get(),
                                       action.executable->tab_id)
           : std::nullopt;
  const bool moved_from_its_source =
      settled && settled->origin != held->normalized_origin;
  std::optional<service_mojom::TaskConsentSourcePtr> candidate;
  if (opens_tab) {
    candidate =
        IssueDiscoveredTaskSourceForAction(binding->task_id, action.action_id);
  } else if (crosses_origin || moved_from_its_source) {
    candidate = IssueDiscoveredTaskSourceForTab(binding->task_id,
                                                action.executable->tab_id);
  }

  service_mojom::TaskConsentSourcePtr discovered_source;
  // Which rule refused, as a compiled-in name for the log. The ledger answers
  // one `kRefused` for eight structurally different rules, and when no
  // candidate was issued at all it is never asked — so the branch below has
  // no reason to give beyond the one it can read off its own conditions.
  // Naming that much is what the next diagnosis needs; splitting the verdict
  // is a separate change and decision 0165 records why it was not made here.
  const char* refusal_at = "no-source-issued";
  if (candidate && *candidate) {
    const TaskSourceDiscoveryVerdict verdict =
        accepted_approvals_.ValidateDiscoveredTaskSource(
            binding->task_id, *binding->operation, **candidate,
            service_generation_,
            base::BindRepeating(&CoreServiceManager::IsLiveIssuedTaskSource,
                                base::Unretained(this)));
    // An admit list rather than `!= kRefused`, so a refusal member added to
    // the verdict later cannot silently become an admission.
    if (verdict == TaskSourceDiscoveryVerdict::kAdmitted ||
        verdict == TaskSourceDiscoveryVerdict::kAlreadyBound) {
      discovered_source = std::move(*candidate);
    } else {
      refusal_at = "source-refused-by-ledger";
    }
  }
  // A move that left the origin it started from is usable only once the
  // ledger has admitted where it landed. Without that admission the tab is
  // bound to a site it is no longer on — a navigate refused at the sites cap
  // would otherwise complete Verified against the old origin — so the
  // completion is refused, as the first search from the discovery tab always
  // was, and the model reads a refusal rather than a page it may not use.
  if ((crosses_origin || moved_from_its_source) && !discovered_source) {
    // The refusal carries its own code. It used to carry none, and a
    // completion with no `effect_result` reads in the core as the bare policy
    // refusal (`refused_code` in
    // //taffy/services/core/service_bridge_task_effect/action_terminal.rs),
    // whose recovery is do-not-retry and whose sentence is "this move is not
    // allowed for this task". A phone spent a `browser.navigate` here and the
    // journal recorded `Deny(DeniedByPolicy)` for a tab that had simply
    // landed on a site the ledger holds no row for.
    //
    // `kEgressNotAuthorized` is the code the policy path already answers for
    // exactly this fact (`source-not-authorized` in
    // core_service_manager_task_policy.cc), so this states no second rule,
    // and its compiled-in sentence names the three admitted ways to acquire a
    // source. Decision 0165 records the one thing it still gets wrong: every
    // code in the refusal vocabulary that fits this fact is classified
    // `SideEffectCertainty::NotPerformed`, and this navigation did land.
    LOG(WARNING) << "[taffy_task_action_refused] at=" << refusal_at
                 << " crossed=" << (crosses_origin ? 1 : 0)
                 << " moved=" << (moved_from_its_source ? 1 : 0);
    completion->status = service_mojom::TaskEffectCompletionStatus::kRefused;
    auto refusal = service_mojom::EffectResult::New();
    refusal->operation = binding->operation.Clone();
    refusal->effect_id = binding->effect_id;
    refusal->status = service_mojom::EffectStatus::kDenied;
    refusal->kind = service_mojom::EffectKind::kBrowserAction;
    refusal->browser_action = service_mojom::BrowserActionEffectResult::New();
    refusal->browser_action->outcome =
        service_mojom::BrowserActionOutcome::kRefused;
    refusal->browser_action->dispatch_id = action.dispatch_id;
    refusal->browser_action->refused_code =
        service_mojom::TaskActionRefusal::New(
            service_mojom::TaskActionResultCode::kEgressNotAuthorized);
    completion->effect_result = std::move(refusal);
    std::move(callback).Run(std::move(completion));
    return;
  }

  auto effect_result = service_mojom::EffectResult::New();
  effect_result->operation = binding->operation.Clone();
  effect_result->effect_id = binding->effect_id;
  effect_result->status = service_mojom::EffectStatus::kCompleted;
  effect_result->kind = service_mojom::EffectKind::kBrowserAction;
  effect_result->browser_action =
      service_mojom::BrowserActionEffectResult::New();
  effect_result->browser_action->outcome =
      service_mojom::BrowserActionOutcome::kCompleted;
  effect_result->browser_action->dispatch_id = action.dispatch_id;
  effect_result->browser_action->discovered_source =
      std::move(discovered_source);
  completion->effect_result = std::move(effect_result);
  std::move(callback).Run(std::move(completion));
}

}  // namespace taffy
