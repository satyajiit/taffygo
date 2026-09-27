// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/policy_response_validation.h"

#include <algorithm>
#include <string>
#include <vector>

#include "taffy/browser/core_task_action.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool SameAuthoritySubject(const mojom::AuthoritySubject* left,
                          const mojom::AuthoritySubject* right) {
  return left && right && left->kind == right->kind &&
         left->authority_subject_id == right->authority_subject_id;
}

bool SamePrincipal(const mojom::PolicyPrincipal* left,
                   const mojom::PolicyPrincipal* right) {
  return left && right && left->kind == right->kind &&
         left->skill_version_id == right->skill_version_id;
}

bool SameOrigin(const mojom::PolicyOrigin* left,
                const mojom::PolicyOrigin* right) {
  return left && right && left->kind == right->kind &&
         left->serialization == right->serialization &&
         left->opaque_id == right->opaque_id;
}

bool SameScope(const mojom::PolicyCapabilityScope* left,
               const mojom::PolicyCapabilityScope* right) {
  if (!left || !right || left->profile_id != right->profile_id ||
      left->tab_id != right->tab_id || left->frame_id != right->frame_id ||
      left->page_epoch != right->page_epoch ||
      !SameOrigin(left->origin.get(), right->origin.get()) ||
      left->node_id != right->node_id ||
      left->destination_address != right->destination_address ||
      left->required_graph_revision != right->required_graph_revision ||
      left->allowed_redirects.size() != right->allowed_redirects.size()) {
    return false;
  }
  if (static_cast<bool>(left->destination_scope) !=
      static_cast<bool>(right->destination_scope)) {
    return false;
  }
  if (left->destination_scope && !SameOrigin(left->destination_scope.get(),
                                             right->destination_scope.get())) {
    return false;
  }
  for (size_t index = 0; index < left->allowed_redirects.size(); ++index) {
    if (!SameOrigin(left->allowed_redirects[index].get(),
                    right->allowed_redirects[index].get())) {
      return false;
    }
  }
  return true;
}

bool SameApproval(const mojom::PolicyApprovalFact* left,
                  const mojom::PolicyApprovalFact* right) {
  if (!left || !right) {
    return !left && !right;
  }
  return left->receipt_reference == right->receipt_reference &&
         left->proposal_digest == right->proposal_digest &&
         left->service_generation == right->service_generation &&
         left->expires_at_monotonic_ms == right->expires_at_monotonic_ms &&
         left->expires_at_utc_ms == right->expires_at_utc_ms &&
         left->browser_session_id == right->browser_session_id;
}

bool SameDiscovery(const mojom::TaskDiscoveryAuthorityFact* left,
                   const mojom::TaskDiscoveryAuthorityFact* right) {
  if (!left || !right) {
    return !left && !right;
  }
  return left->discovery_tab_id == right->discovery_tab_id &&
         left->browser_session_id == right->browser_session_id &&
         left->remaining_new_source_cap == right->remaining_new_source_cap;
}

mojom::PolicyRiskClass BaselineRisk(mojom::PolicyActionClass action_class) {
  switch (action_class) {
    case mojom::PolicyActionClass::kObservePage:
    case mojom::PolicyActionClass::kScrollIntoView:
    // A tool job runs in an isolated, capability-free runtime on material the
    // task already holds; nothing leaves the device, so its floor is the read
    // floor. Mirrors `baseline_risk` in policy-engine, which decides — this
    // copy only refuses a response claiming less.
    case mojom::PolicyActionClass::kExecuteToolJob:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kMemoryRead:
    case mojom::PolicyActionClass::kProfileStoreRead:
      return mojom::PolicyRiskClass::kLocalRead;
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kSyntheticClick:
    case mojom::PolicyActionClass::kMoveFocus:
    case mojom::PolicyActionClass::kControlTab:
      return mojom::PolicyRiskClass::kReversibleDisclosure;
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
    case mojom::PolicyActionClass::kToggleControl:
    case mojom::PolicyActionClass::kSubmitForm:
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryWrite:
      return mojom::PolicyRiskClass::kSensitiveDisclosure;
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
      return mojom::PolicyRiskClass::kExcludedCommitment;
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
      return mojom::PolicyRiskClass::kProhibitedAbuse;
  }
}

mojom::PolicyRiskClass ExpectedRisk(
    const mojom::PolicyEvaluationRequest& request) {
  return static_cast<mojom::PolicyRiskClass>(
      std::max(static_cast<uint8_t>(BaselineRisk(request.action_class)),
               static_cast<uint8_t>(request.context_risk)));
}

bool SameOperation(const mojom::OperationEnvelope* left,
                   const mojom::OperationEnvelope* right) {
  return left && right && left->operation_id == right->operation_id &&
         left->service_generation == right->service_generation &&
         left->task_revision == right->task_revision &&
         left->deadline_monotonic_ms == right->deadline_monotonic_ms &&
         left->idempotency_key == right->idempotency_key;
}

using ActionClass = mojom::PolicyActionClass;
using Operation = mojom::TaskActionOperationKind;
using Refusal = mojom::TaskActionResultCode;

// A destination-bearing operation whose scope does not hold together is
// refused as egress rather than as an unknown shape: the operation is one this
// build serves, and what failed is where it was pointed.
constexpr Refusal kEgress = Refusal::kEgressNotAuthorized;
// An operation whose scope shape this build does not serve at all.
constexpr Refusal kShape = Refusal::kUnsupported;

std::optional<Refusal> Verdict(bool holds, Refusal refusal) {
  return holds ? std::nullopt : std::optional<Refusal>(refusal);
}

bool DestinationTupleHolds(const mojom::PolicyCapabilityScope& scope) {
  return scope.destination_scope &&
         scope.destination_scope->kind == mojom::PolicyOriginKind::kTuple &&
         scope.destination_scope->serialization &&
         !scope.destination_scope->opaque_id && scope.destination_address &&
         HttpAddressOriginEquals(*scope.destination_address,
                                 *scope.destination_scope->serialization);
}

}  // namespace

std::optional<mojom::TaskActionResultCode> TaskScopeRefusalForOperation(
    const mojom::PolicyEvaluationRequest& request) {
  if (!request.scope || !request.scope->origin) {
    return kShape;
  }
  const mojom::PolicyCapabilityScope& scope = *request.scope;
  // A task's scope ordinarily stands on a page, and a page's origin is a
  // tuple. There is one other document it may act from: one with no site of
  // its own, which is what Chromium commits when an address does not answer.
  // The only moves from there are the two whose admissibility is decided by
  // their destination, and the branches below check those two the same way
  // either way, so nothing is inferred from the opaque identity beyond its
  // shape (decision 0176).
  if (scope.origin->kind == mojom::PolicyOriginKind::kOpaque) {
    const bool leaves_a_document_with_no_site =
        IsTaskNavigationOperation(request.operation_kind) ||
        IsTaskSearchOperation(request.operation_kind);
    if (!leaves_a_document_with_no_site || !scope.origin->opaque_id ||
        scope.origin->opaque_id->empty() || scope.origin->serialization ||
        scope.required_graph_revision != 0u) {
      return kShape;
    }
  } else if (scope.origin->kind != mojom::PolicyOriginKind::kTuple ||
             !scope.origin->serialization || scope.origin->opaque_id) {
    return kShape;
  }
  if (IsTaskObservationOperation(request.operation_kind)) {
    const bool form_inspection = request.operation_kind == Operation::kFormInspect;
    return Verdict(request.action_class == ActionClass::kObservePage &&
                       scope.node_id.has_value() == form_inspection &&
                       !scope.destination_scope && !scope.destination_address,
                   kShape);
  }
  if (request.operation_kind == Operation::kDomScroll ||
      request.operation_kind == Operation::kDomClick ||
      request.operation_kind == Operation::kDomFocus) {
    const ActionClass expected_class =
        request.operation_kind == Operation::kDomScroll
            ? ActionClass::kScrollIntoView
        : request.operation_kind == Operation::kDomClick
            ? ActionClass::kSyntheticClick
            : ActionClass::kMoveFocus;
    return Verdict(request.action_class == expected_class && scope.node_id &&
                       !scope.destination_scope && !scope.destination_address,
                   kShape);
  }
  if (request.operation_kind == Operation::kFormFill) {
    // This is the only M5 page-write scope admitted here. The manager has
    // already spent the browser-private exact-value preapproval before it can
    // construct this request; Select/Toggle/Submit remain closed rather than
    // sharing a generic form-write branch.
    return Verdict(request.action_class == ActionClass::kFillField &&
                       scope.node_id && !scope.destination_scope &&
                       !scope.destination_address,
                   kShape);
  }
  const bool lists_downloads = request.operation_kind == Operation::kDownloadList;
  if (lists_downloads || request.operation_kind == Operation::kDownloadCancel) {
    const auto expected_class = lists_downloads ? ActionClass::kObservePage
                                                : ActionClass::kStartDownload;
    return Verdict(request.action_class == expected_class && !scope.node_id &&
                       !scope.destination_scope && !scope.destination_address,
                   kShape);
  }
  if (request.operation_kind == Operation::kDownloadStart) {
    if (request.action_class != ActionClass::kStartDownload) {
      return kShape;
    }
    return Verdict(DestinationTupleHolds(scope) &&
                       GURL(*scope.destination_address).SchemeIs("https"),
                   kEgress);
  }
  // A typed operation (tabs, a store read, Library, Memory) is scoped to the
  // context document alone and names no node and no destination.
  if (const auto typed = TypedTaskOperationClass(request.operation_kind)) {
    return Verdict(request.action_class == *typed && !scope.node_id &&
                       !scope.destination_scope && !scope.destination_address,
                   kShape);
  }
  if (IsTaskNavigationOperation(request.operation_kind)) {
    // The destination is the origin the address itself names, never the one
    // this document happens to be on. Decision 0136 section 3 admits a
    // model-named https origin the way a followed link already is, and the
    // browser resolved that destination from the address before this runs;
    // comparing it against the document's origin instead refused every
    // cross-origin navigate, which reached the journal as `Unsupported` and
    // left the model re-searching the results page it was already on. The
    // search, tab-open and download branches beside this one always had the
    // shape this one now has.
    if (request.action_class != ActionClass::kOpenLink || scope.node_id) {
      return kShape;
    }
    return Verdict(DestinationTupleHolds(scope) &&
                       GURL(*scope.destination_address).SchemeIs("https"),
                   kEgress);
  }
  if (IsTaskTabControlOperation(request.operation_kind)) {
    return Verdict(request.action_class == ActionClass::kControlTab &&
                       !scope.node_id && !scope.destination_scope &&
                       !scope.destination_address,
                   kShape);
  }
  if (IsTaskSearchOperation(request.operation_kind) ||
      IsTaskTabOpenOperation(request.operation_kind)) {
    const ActionClass expected_class = IsTaskSearchOperation(request.operation_kind)
                                           ? ActionClass::kOpenLink
                                           : ActionClass::kCreateTaskTab;
    if (request.action_class != expected_class || scope.node_id) {
      return kShape;
    }
    return Verdict(DestinationTupleHolds(scope), kEgress);
  }
  if (IsTaskLinkOpenOperation(request.operation_kind)) {
    if (request.action_class != ActionClass::kOpenLink || !scope.node_id) {
      return kShape;
    }
    return Verdict(DestinationTupleHolds(scope), kEgress);
  }
  return kShape;
}

bool TaskDiscoveryPolicyRequestHasExactShape(
    const mojom::PolicyEvaluationRequest& request,
    std::string_view browser_session_id) {
  if (request.context != mojom::PolicyEvaluationContext::kTaskDiscovery ||
      !request.discovery || !request.principal || !request.actor_lease ||
      !request.scope || !request.scope->origin ||
      !request.scope->destination_scope ||
      request.discovery->discovery_tab_id != request.scope->tab_id ||
      request.discovery->browser_session_id != browser_session_id ||
      request.discovery->remaining_new_source_cap == 0u ||
      request.discovery->remaining_new_source_cap > mojom::kMaxNewSourceCap ||
      !IsIdentifier(request.scope->tab_id) ||
      !IsIdentifier(request.scope->frame_id) ||
      !IsIdentifier(request.scope->page_epoch) ||
      !request.scope->origin->opaque_id ||
      !IsIdentifier(*request.scope->origin->opaque_id) ||
      request.principal->kind != mojom::PolicyPrincipalKind::kAssistant ||
      request.principal->skill_version_id ||
      request.action_class != mojom::PolicyActionClass::kOpenLink ||
      (request.operation_kind != mojom::TaskActionOperationKind::kSearch &&
       request.operation_kind != mojom::TaskActionOperationKind::kNavigate) ||
      request.context_risk != mojom::PolicyRiskClass::kReversibleDisclosure ||
      request.data_classes.size() != 1u ||
      request.data_classes.front() != mojom::BipSensitivity::kNotSensitive ||
      request.approval ||
      request.actor_lease->control_mode != mojom::TaskControlMode::kAssistant ||
      request.scope->origin->kind != mojom::PolicyOriginKind::kOpaque ||
      request.scope->origin->serialization ||
      request.scope->destination_scope->kind !=
          mojom::PolicyOriginKind::kTuple ||
      !request.scope->destination_scope->serialization ||
      request.scope->destination_scope->opaque_id || request.scope->node_id ||
      !request.scope->destination_address ||
      (request.operation_kind == mojom::TaskActionOperationKind::kNavigate &&
       !GURL(*request.scope->destination_address).SchemeIs("https")) ||
      !HttpAddressOriginEquals(
          *request.scope->destination_address,
          *request.scope->destination_scope->serialization) ||
      request.scope->required_graph_revision != 0u ||
      !request.scope->allowed_redirects.empty()) {
    return false;
  }
  return true;
}

bool PolicyGrantMatchesRequest(const mojom::MintedCapabilityGrant& grant,
                               const mojom::PolicyEvaluationRequest& request) {
  return request.operation && request.actor_lease &&
         request.authority_subject && !grant.capability_id.empty() &&
         request.policy_version != 0u &&
         grant.policy_version == request.policy_version &&
         grant.service_generation == request.operation->service_generation &&
         grant.actor_lease_id == request.actor_lease->lease_id &&
         grant.task_id == request.task_id &&
         grant.action_id == request.action_id &&
         grant.action_class == request.action_class &&
         grant.operation_kind == request.operation_kind &&
         grant.canonical_intent_digest == request.canonical_intent_digest &&
         SamePrincipal(grant.principal.get(), request.principal.get()) &&
         grant.proposal_digest == request.proposal_digest &&
         grant.idempotency_key == request.operation->idempotency_key &&
         SameScope(grant.scope.get(), request.scope.get()) &&
         grant.data_classes == request.data_classes &&
         grant.effective_risk == ExpectedRisk(request) &&
         SameApproval(grant.approval.get(), request.approval.get()) &&
         SameDiscovery(grant.discovery.get(), request.discovery.get()) &&
         grant.issued_at_monotonic_ms == request.now_monotonic_ms &&
         grant.expires_at_monotonic_ms == request.expires_at_monotonic_ms &&
         SameAuthoritySubject(grant.authority_subject.get(),
                              request.authority_subject.get());
}

bool DirectObservationEffectMatchesRequest(
    const mojom::EffectEnvelope& effect,
    const mojom::MintedCapabilityGrant& grant,
    const mojom::PolicyEvaluationRequest& request) {
  if (request.context !=
          mojom::PolicyEvaluationContext::kDirectUserObservation ||
      !request.operation || !request.scope || !effect.page_observation ||
      !SameOperation(effect.operation.get(), request.operation.get()) ||
      effect.effect_id != request.operation->operation_id ||
      effect.kind != mojom::EffectKind::kPageObservation ||
      effect.retry_class != mojom::RetryClass::kIdempotent ||
      effect.storage_commit || effect.model_request || effect.network_request ||
      effect.browser_action || effect.tool_job || effect.secure_store ||
      effect.auth_surface || effect.permission_request) {
    return false;
  }
  const mojom::PageObservationEffect& body = *effect.page_observation;
  return body.tab_id == request.scope->tab_id &&
         body.frame_id == request.scope->frame_id &&
         body.page_epoch == request.scope->page_epoch &&
         body.scope == mojom::ObservationScope::kCurrentDocument &&
         body.max_bytes == mojom::kMaxDirectObservationTotalBytes &&
         body.task_id.empty() && body.action_id.empty() &&
         body.capability_id == grant.capability_id &&
         body.proposal_digest == request.proposal_digest &&
         body.idempotency_key == request.operation->idempotency_key &&
         SameAuthoritySubject(body.authority_subject.get(),
                              request.authority_subject.get()) &&
         body.expected_graph_revision ==
             request.scope->required_graph_revision &&
         body.max_nodes == mojom::kMaxDirectObservationNodes &&
         body.max_text_bytes == mojom::kMaxDirectObservationTextBytes &&
         body.max_frames == mojom::kMaxDirectObservationFrames &&
         body.deadline_ms == mojom::kMaxDirectObservationDeadlineMs;
}

}  // namespace taffy
