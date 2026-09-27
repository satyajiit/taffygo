// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "base/types/optional_util.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/browser/core_task_policy_destination.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

constexpr uint64_t kMaximumTaskLeaseDurationMs = 30'000u;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

// A denied answer always says which closed code the action is settled with;
// the bare policy refusal is the code when the caller names none.
service_mojom::PolicyEvaluationResultPtr MakeTaskPolicyResult(
    std::string operation_id,
    service_mojom::PolicyEvaluationStatus status,
    std::optional<service_mojom::TaskActionResultCode> denial_code =
        std::nullopt) {
  auto result = service_mojom::PolicyEvaluationResult::New();
  result->operation_id = std::move(operation_id);
  result->status = status;
  if (status == service_mojom::PolicyEvaluationStatus::kDenied) {
    result->denial = service_mojom::PolicyDenial::New(denial_code.value_or(
        service_mojom::TaskActionResultCode::kDeniedByPolicy));
  }
  return result;
}

}  // namespace

void CoreServiceManager::EvaluateTaskPolicy(
    service_mojom::TaskPolicyEffectPtr effect,
    EvaluateTaskPolicyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string operation_id = effect && effect->operation
                                       ? effect->operation->operation_id
                                       : std::string();
  // `refuse` answers for an effect the browser could not read at all, or a
  // core it cannot reach. A proposal the browser understood and will not
  // allow is a decision about that proposal: `deny` settles it under the
  // closed code the task engine records and the model is told, and the walk
  // goes on. Answering INVALID_REQUEST for a refusal instead ended the core.
  auto refuse = [&callback, &operation_id](
                    service_mojom::PolicyEvaluationStatus status,
                    const char* at) {
    // Decision 0136 section 1: a refusal is a decision the model is told.
    // This path answered one word - the task engine stamps INVALID_REQUEST as
    // Deny{Unsupported} - from six different clauses, so a navigate that was
    // refused and a navigate that was malformed read identically in the
    // journal and cost a decode each time. The name is the whole fix.
    LOG(WARNING) << "[taffy_task_policy_refused] at=" << at
                 << " status=" << static_cast<int>(status);
    std::move(callback).Run(MakeTaskPolicyResult(operation_id, status));
  };
  auto deny = [&callback, &operation_id](
                  service_mojom::TaskActionResultCode code, const char* at) {
    // A denial already carries its code into the journal, so this line exists
    // for the other half of the question: which clause produced it. Seven
    // `deny` sites share four codes between them.
    LOG(WARNING) << "[taffy_task_policy_denied] at=" << at
                 << " code=" << static_cast<int>(code);
    std::move(callback).Run(MakeTaskPolicyResult(
        operation_id, service_mojom::PolicyEvaluationStatus::kDenied, code));
  };

  // The entry line. Its absence is a fact too: a proposal that is settled
  // without one never reached the browser at all.
  LOG(WARNING) << "[taffy_task_policy_ask]"
               << " op="
               << (effect ? static_cast<int>(effect->operation_kind) : -1)
               << " class="
               << (effect ? static_cast<int>(effect->action_class) : -1)
               << " tool=" << (effect ? effect->tool_name : std::string())
               << " discovery=" << (effect && effect->discovery ? 1 : 0);
  if (shutdown_started_ || availability_ != Availability::kReady ||
      !session_.is_bound()) {
    refuse(service_mojom::PolicyEvaluationStatus::kCoreUnavailable,
           "core-unavailable");
    return;
  }
  if (!effect || !effect->operation) {
    refuse(service_mojom::PolicyEvaluationStatus::kInvalidRequest, "no-effect");
    return;
  }

  const std::optional<uint64_t> revision = FindTaskRevision(effect->task_id);
  const uint64_t now = NowMonotonicMillis();
  const uint64_t now_utc = NowUtcMillis();
  if (!revision ||
      !IsValidReadOnlyTaskPolicyEffect(*effect, service_generation_, *revision,
                                       browser_session_id_, now, now_utc)) {
    // The shape check is one boolean over forty clauses, so the name alone
    // cannot say which one. These are the facts that tell the navigate cases
    // apart - every one is a compiled-in enumeration member or a flag, and
    // none of them is a fact about the page.
    LOG(WARNING)
        << "[taffy_task_policy_effect] revision=" << revision.has_value()
        << " op=" << static_cast<int>(effect->operation_kind)
        << " class=" << static_cast<int>(effect->action_class)
        << " tool=" << effect->tool_name
        << " discovery=" << (effect->discovery ? 1 : 0)
        << " has_dest=" << (effect->destination_address ? 1 : 0)
        << " dest_origin="
        << (effect->destination_address &&
            HttpAddressOrigin(*effect->destination_address).has_value())
        << " node=" << (effect->node_id ? 1 : 0)
        << " risk=" << static_cast<int>(effect->context_risk)
        << " control=" << static_cast<int>(effect->control_mode)
        << " classes=" << effect->data_classes.size()
        << " approval=" << (effect->approval ? 1 : 0)
        << " policy_version=" << effect->policy_version;
    refuse(service_mojom::PolicyEvaluationStatus::kInvalidRequest,
           "effect-shape");
    return;
  }

  std::optional<TaskPolicyDocumentContext> live;
  std::optional<TaskDiscoveryDocumentContext> discovery_live;
  std::optional<TaskDepartureDocumentContext> departure;
  if (effect->discovery) {
    const service_mojom::TaskDiscoveryAuthorityFact& discovery =
        *effect->discovery;
    if (discovery.browser_session_id != browser_session_id_ ||
        !accepted_approvals_.HasTaskSourceDiscoveryBootstrapAuthority(
            effect->task_id, *effect->operation, service_generation_,
            browser_session_id_, discovery.remaining_new_source_cap)) {
      deny(service_mojom::TaskActionResultCode::kActorLeaseMissing,
           "discovery-bootstrap");
      return;
    }
    discovery_live =
        ResolveTaskDiscoveryDocument(browser_context_.get(), effect->tab_id);
    TaffyPageIntelligenceHost* host =
        FindPageIntelligenceHost(browser_context_.get(), effect->tab_id);
    content::WebContents* web_contents =
        host ? host->observed_web_contents() : nullptr;
    if (!discovery_live ||
        !IsExactTaskDiscoveryTab(
            effect->task_id, effect->tab_id, browser_session_id_,
            discovery.remaining_new_source_cap, web_contents)) {
      deny(service_mojom::TaskActionResultCode::kTabGone, "tab-gone");
      return;
    }
  } else {
    live = ResolveTaskPolicyDocument(browser_context_.get(), effect->tab_id);
    if (!live) {
      // A tab that is gone and a tab whose document has no site of its own are
      // different facts, and one code for both is terminal for the wrong one:
      // `kTabGone` is `Abandon` on the recovery ladder, so a task whose tab is
      // alive is over with no move left to make. That happened on a phone. The
      // model named a host that does not resolve, Chromium committed its own
      // error document - whose origin is opaque, which is exactly what
      // `BuildDirectObservationContext` answers nothing for - and the next
      // observation of a tab sitting right there came back "the tab is gone"
      // and ended the errand (decision 0171).
      //
      // Naming the document was half the repair. The other half is that a
      // document with no site is not a dead end: a move that leaves it is
      // authorized by where it is going, so the tab being there is enough to
      // bind one. Refusing those too left the errand on Chromium's error page
      // with every move refused, which is the same terminal state under a
      // different name (decision 0176).
      //
      // Navigate and search, and no other move. Those are the two shapes the
      // rest of the path already admits against a document with no origin -
      // `FrozenDocumentOriginIsWellFormed` names exactly them - so they are
      // the two a task may make from one. Opening a task tab, going back and
      // reloading are not refused on principle; they are refused because
      // nothing downstream can bind them here, and a refusal that names
      // itself beats a dispatch nobody can check.
      if (IsTaskNavigationOperation(effect->operation_kind) ||
          IsTaskSearchOperation(effect->operation_kind)) {
        departure =
            ResolveTaskDepartureDocument(browser_context_.get(), effect->tab_id);
      }
      if (!departure) {
        // A read or an act on a document with no site is refused because the
        // task holds no source for it, which is what `kEgressNotAuthorized`
        // says - and its sentence already names the move that recovers, which
        // is to go to an https address. `kDocumentInactive` said "read it
        // again", and reading it again fails the same way, so the errand spent
        // its replies re-reading a page that has no site and then died.
        const bool tab_is_there =
            FindPageIntelligenceHost(browser_context_.get(), effect->tab_id) !=
            nullptr;
        deny(tab_is_there
                 ? service_mojom::TaskActionResultCode::kEgressNotAuthorized
                 : service_mojom::TaskActionResultCode::kTabGone,
             tab_is_there ? "document-has-no-site" : "tab-gone");
        return;
      }
      // Every operation that leaves the document also carries a destination,
      // so the departure context is only ever read by the destination arm
      // below. Stated as a refusal rather than assumed, because the two
      // predicates are separate functions and nothing else holds them
      // together.
      if (!TaskPolicyOperationCarriesDestination(effect->operation_kind)) {
        deny(service_mojom::TaskActionResultCode::kEgressNotAuthorized,
             "departure-carries-no-destination");
        return;
      }
    }
    // A move that leaves the document is authorized by where it is going, not
    // by where it is: policy decides the destination and the sites budget
    // counts it (decision 0106 section 2). Reading or acting on the document
    // still requires the document's own origin to be an admitted source.
    // Gating both on the source deadlocked every task whose page moved under
    // it — one redirect the task never proposed left the tab on an origin the
    // ledger has no record for, every read was refused, correctly, and so was
    // the only move that could have left, so the errand could not recover.
    const std::string document_origin = live ? live->origin : std::string();
    const bool authorized =
        TaskPolicyOperationLeavesDocument(effect->operation_kind)
            ? accepted_approvals_.IsTaskTabAuthorized(*effect,
                                                      service_generation_)
            : accepted_approvals_.IsTaskSourceAuthorized(*effect,
                                                         document_origin,
                                                         service_generation_);
    if (!authorized) {
      // Which half refused, and the facts that tell them apart: a task with
      // no consent record at all, a tab the task holds no source for, and a
      // tab whose document has moved are three different repairs.
      const AcceptedApprovalLedger::TaskTabAuthorityReason reason =
          accepted_approvals_.DescribeTaskTabAuthority(*effect,
                                                       service_generation_);
      LOG(WARNING) << "[taffy_task_source_refused]"
                   << " leaves="
                   << (TaskPolicyOperationLeavesDocument(effect->operation_kind)
                           ? 1
                           : 0)
                   << " record=" << (reason.has_record ? 1 : 0)
                   << " generation=" << (reason.generation_matches ? 1 : 0)
                   << " revision=" << (reason.revision_is_current ? 1 : 0)
                   << " tab_held=" << (reason.tab_is_held ? 1 : 0)
                   << " origin_matches="
                   << (accepted_approvals_.IsTaskSourceAuthorized(
                           *effect, document_origin, service_generation_)
                           ? 1
                           : 0)
                   << " site=" << (live ? 1 : 0) << " live=" << document_origin;
      deny(service_mojom::TaskActionResultCode::kEgressNotAuthorized,
           "source-not-authorized");
      return;
    }
  }

  std::optional<std::string> browser_destination;
  std::optional<std::string> browser_destination_origin;
  // The revision a press or a focus is bound at: the one its handle was read
  // at, never a later one, so the renderer's own check - that the node has
  // not changed since that floor - is asked about the reading the model
  // decided on (decision 0188).
  std::optional<uint64_t> node_revision;
  if (TaskPolicyOperationCarriesDestination(effect->operation_kind)) {
    auto destination = ResolveTaskPolicyDestination(
        browser_context_.get(), *effect, base::OptionalToPtr(live),
        [this](const std::string& tab_id, const std::string& query) {
          return ResolveTaskSearchAddress(tab_id, query);
        });
    if (!destination.has_value()) {
      deny(destination.error(), "destination");
      return;
    }
    browser_destination = std::move(destination->address);
    browser_destination_origin = std::move(destination->origin);
  } else if (effect->operation_kind ==
                 service_mojom::TaskActionOperationKind::kDomClick ||
             effect->operation_kind ==
                 service_mojom::TaskActionOperationKind::kDomFocus) {
    // `live` is present here by construction - a departure context is
    // resolved only for an operation that leaves the document, and every one
    // of those is answered by the destination arm above - but acting on a
    // document this build could not resolve is a refusal, not a dereference.
    //
    // The handle's revision was compared for equality with the live one, so
    // a press was refused whenever anything on the page had changed since the
    // model read it. On a phone that was every press on an identity portal
    // that redraws itself: `deny:STALE_GRAPH` three times running on the one
    // control that leads to the download form, with no navigation between
    // the reading and the press (decision 0188).
    const bool press = effect->operation_kind ==
                       service_mojom::TaskActionOperationKind::kDomClick;
    std::optional<CanonicalObservedNodeHandle> target;
    if (press) {
      std::optional<CanonicalDomActivationIntent> activation =
          ReadCanonicalDomActivationIntent(effect->canonical_intent);
      if (activation) {
        target = std::move(activation->target);
      }
    } else {
      target = ReadCanonicalDomFocusHandle(effect->canonical_intent);
    }
    const char* at = !live              ? "no-live-document"
                     : !effect->node_id ? "no-node"
                     : !target          ? "intent-carries-no-handle"
                                        : ObservedNodeHandleMismatch(
                                     *target, effect->tab_id, *effect->node_id,
                                     live->frame_id, live->page_epoch,
                                     live->graph_revision, live->origin);
    if (at) {
      LOG(WARNING) << "[taffy_task_node_refused] at=" << at
                   << " op=" << (press ? "press" : "focus");
      deny(service_mojom::TaskActionResultCode::kStaleGraph, "stale-graph");
      return;
    }
    if (target->graph_revision < live->graph_revision) {
      // Admitted, and logged so a device run can see this clause fire.
      LOG(WARNING) << "[taffy_task_node_older_than_page]"
                   << " op=" << (press ? "press" : "focus") << " behind="
                   << live->graph_revision - target->graph_revision;
    }
    node_revision = target->graph_revision;
  } else if (effect->action_class ==
                 service_mojom::PolicyActionClass::kFillField ||
             effect->action_class ==
                 service_mojom::PolicyActionClass::kSelectOption ||
             effect->action_class ==
                 service_mojom::PolicyActionClass::kToggleControl ||
             effect->action_class ==
                 service_mojom::PolicyActionClass::kSubmitForm) {
    if (const std::optional<TaskPolicyDenial> denial = DenyTaskFormPolicy(
            *effect, base::OptionalToPtr(live), now, now_utc)) {
      deny(denial->first, denial->second);
      return;
    }
  }

  const uint64_t remaining = effect->operation->deadline_monotonic_ms - now;
  const uint64_t bounded_duration =
      std::min(remaining, kMaximumTaskLeaseDurationMs);
  if (bounded_duration == 0u ||
      bounded_duration > std::numeric_limits<uint32_t>::max()) {
    deny(service_mojom::TaskActionResultCode::kCapabilityExpired,
         "lease-duration");
    return;
  }
  ActorLeaseRequest lease_request;
  lease_request.task_id = TaskId{effect->task_id};
  lease_request.tab_id = TabId{effect->tab_id};
  lease_request.requested_duration_ms = static_cast<uint32_t>(bounded_duration);
  lease_request.mutating = TaskActionClassMutates(effect->action_class);
  const ActorLeaseResult lease =
      actor_leases_.Issue(lease_request, base::TimeTicks::Now());
  if (lease.code != ActorLeaseResultCode::kIssued) {
    // One code reaches the model for three different registry answers, and
    // `kAlreadyHeld` on a mutating request is the one that stops a navigate a
    // read is still holding the tab for. Say which, and whether the request
    // was the mutating kind, so the next journal does not need a decode.
    LOG(WARNING) << "[taffy_actor_lease_refused] code="
                 << static_cast<int>(lease.code)
                 << " mutating=" << (lease_request.mutating ? 1 : 0)
                 << " held="
                 << (actor_leases_.HasMutatingLease(lease_request.tab_id,
                                                    base::TimeTicks::Now())
                         ? 1
                         : 0);
    deny(service_mojom::TaskActionResultCode::kActorLeaseMissing,
         "actor-lease");
    return;
  }

  // Three documents can be bound here and they are told apart by which one
  // was resolved, not by a flag on the effect: a discovery blank, a document
  // with no site that is being left, and an ordinary page. The first two
  // carry an opaque identity and no origin; only the third has a graph.
  TaskPolicyDocumentBinding document;
  if (effect->discovery) {
    document.tab_id = discovery_live->tab_id;
    document.frame_id = discovery_live->frame_id;
    document.page_epoch = discovery_live->page_epoch;
    document.opaque_origin_id = discovery_live->opaque_origin_id;
  } else if (departure) {
    document.tab_id = departure->tab_id;
    document.frame_id = departure->frame_id;
    document.page_epoch = departure->page_epoch;
    document.opaque_origin_id = departure->opaque_origin_id;
  } else {
    document.tab_id = live->tab_id;
    document.frame_id = live->frame_id;
    document.page_epoch = live->page_epoch;
    document.origin = live->origin;
    document.graph_revision = node_revision.value_or(live->graph_revision);
  }
  document.destination_address = std::move(browser_destination);
  document.destination_origin = std::move(browser_destination_origin);
  std::optional<service_mojom::TaskActionResultCode> binding_denial;
  service_mojom::PolicyEvaluationRequestPtr request =
      BindReadOnlyTaskPolicyRequest(*effect, document, browser_profile_id_,
                                    lease, service_generation_, now, now_utc,
                                    &binding_denial);
  if (!request) {
    actor_leases_.Release(lease.lease_id);
    if (binding_denial) {
      deny(*binding_denial, "binding");
    } else {
      refuse(service_mojom::PolicyEvaluationStatus::kInvalidRequest, "binding");
    }
    return;
  }
  if (!accepted_approvals_.ConsumeExactApproval(
          *effect, service_generation_, browser_session_id_, now, now_utc)) {
    actor_leases_.Release(lease.lease_id);
    deny(service_mojom::TaskActionResultCode::kApprovalRequired, "approval");
    return;
  }

  const uint32_t expected_policy_version = effect->policy_version;
  const bool answered_by_asking = TaskPolicyAskIsAnsweredByAsking(*effect);
  EvaluatePolicy(std::move(request),
                 base::BindOnce(&CoreServiceManager::OnTaskPolicyEvaluated,
                                weak_factory_.GetWeakPtr(), lease.lease_id,
                                expected_policy_version, answered_by_asking,
                                std::move(callback)));
}

void CoreServiceManager::OnTaskPolicyEvaluated(
    ActorLeaseId lease_id,
    uint32_t expected_policy_version,
    bool answered_by_asking,
    EvaluateTaskPolicyCallback callback,
    service_mojom::PolicyEvaluationResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (answered_by_asking && result &&
      result->status == service_mojom::PolicyEvaluationStatus::kGranted) {
    // A fill nobody has approved is never granted here, whatever the decider
    // answered. The policy engine says the same thing (decision 0089 section
    // 3); this is the browser saying it on its own (decision 0239).
    LOG(WARNING) << "[taffy_task_policy_refused] at=fill-granted-unapproved";
    result = MakeTaskPolicyResult(
        result->operation_id,
        service_mojom::PolicyEvaluationStatus::kInvalidRequest);
  }
  if (answered_by_asking && result) {
    LOG(WARNING) << "[taffy_task_policy_fill_asks] at=answered status="
                 << static_cast<int>(result->status);
  }
  if (!result ||
      (result->status == service_mojom::PolicyEvaluationStatus::kGranted &&
       (!result->minted_grant ||
        result->minted_grant->policy_version != expected_policy_version))) {
    const std::string operation_id =
        result ? result->operation_id : std::string();
    LOG(WARNING) << "[taffy_task_policy_refused] at=evaluated-grant"
                 << " have_result=" << (result ? 1 : 0);
    result = MakeTaskPolicyResult(
        operation_id, service_mojom::PolicyEvaluationStatus::kInvalidRequest);
  }
  if (result->status != service_mojom::PolicyEvaluationStatus::kGranted) {
    actor_leases_.Release(lease_id);
  }
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy
