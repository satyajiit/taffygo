// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "content/public/browser/browser_thread.h"
#include "crypto/sha2.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool DigestMatches(const std::array<uint8_t, 32>& local,
                   const std::vector<uint8_t>& wire) {
  return wire.size() == local.size() &&
         std::equal(local.begin(), local.end(), wire.begin());
}

bool ActionTypeMatchesClass(ActionType action_type,
                            mojom::PolicyActionClass action_class) {
  switch (action_class) {
    case mojom::PolicyActionClass::kSyntheticClick:
      return action_type == ActionType::kActivate;
    case mojom::PolicyActionClass::kScrollIntoView:
      return action_type == ActionType::kScrollIntoView;
    case mojom::PolicyActionClass::kFillField:
      return action_type == ActionType::kSetText;
    case mojom::PolicyActionClass::kSelectOption:
      return action_type == ActionType::kSelectOption;
    case mojom::PolicyActionClass::kToggleControl:
      return action_type == ActionType::kToggle;
    case mojom::PolicyActionClass::kSubmitForm:
      return action_type == ActionType::kSubmitForm;
    case mojom::PolicyActionClass::kObservePage:
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kMoveFocus:
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
    case mojom::PolicyActionClass::kExecuteToolJob:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryRead:
    case mojom::PolicyActionClass::kMemoryWrite:
    case mojom::PolicyActionClass::kControlTab:
    case mojom::PolicyActionClass::kProfileStoreRead:
      return false;
  }
  return false;
}

bool ActionTypeMatchesOperation(ActionType action_type,
                                mojom::TaskActionOperationKind operation) {
  return (action_type == ActionType::kActivate &&
          operation == mojom::TaskActionOperationKind::kDomClick) ||
         (action_type == ActionType::kScrollIntoView &&
          operation == mojom::TaskActionOperationKind::kDomScroll) ||
         (action_type == ActionType::kSetText &&
          operation == mojom::TaskActionOperationKind::kFormFill) ||
         (action_type == ActionType::kSelectOption &&
          operation == mojom::TaskActionOperationKind::kFormSelect) ||
         (action_type == ActionType::kToggle &&
          operation == mojom::TaskActionOperationKind::kFormToggle) ||
         (action_type == ActionType::kSubmitForm &&
          operation == mojom::TaskActionOperationKind::kFormSubmit);
}

bool ObservationNodeMatchesGrant(const mojom::TaskActionEffect& action,
                                 const mojom::MintedCapabilityGrant& grant) {
  switch (grant.operation_kind) {
    case mojom::TaskActionOperationKind::kFormInspect:
    case mojom::TaskActionOperationKind::kImageDescribe:
    case mojom::TaskActionOperationKind::kImageReadText:
    case mojom::TaskActionOperationKind::kVideoInspect:
      return action.executable->node_id && grant.scope->node_id &&
             *action.executable->node_id == *grant.scope->node_id;
    case mojom::TaskActionOperationKind::kDomQuery:
    case mojom::TaskActionOperationKind::kDomRead:
    case mojom::TaskActionOperationKind::kSelectionRead:
    case mojom::TaskActionOperationKind::kPdfInspect:
    case mojom::TaskActionOperationKind::kPageScreenshotInspect:
      return !action.executable->node_id && !grant.scope->node_id;
    default:
      return false;
  }
}

bool EnvelopeMatchesGrant(const AuthorizedActionEnvelope& envelope,
                          const mojom::MintedCapabilityGrant& grant) {
  if (!grant.scope || !grant.scope->origin ||
      !grant.scope->origin->serialization ||
      grant.scope->origin->kind != mojom::PolicyOriginKind::kTuple ||
      !grant.scope->node_id ||
      envelope.action_digest.value != grant.proposal_digest ||
      envelope.task_id.value != grant.task_id ||
      envelope.action_id.value != grant.action_id ||
      envelope.idempotency_key != grant.idempotency_key ||
      envelope.capability.capability_reference.value != grant.capability_id ||
      envelope.capability.actor_lease_id.value != grant.actor_lease_id ||
      envelope.capability.policy_version != grant.policy_version ||
      envelope.capability.expires_at_monotonic_ms !=
          grant.expires_at_monotonic_ms ||
      envelope.target_handle.tab_id.value != grant.scope->tab_id ||
      envelope.target_handle.frame_id.value != grant.scope->frame_id ||
      envelope.target_handle.page_epoch.value != grant.scope->page_epoch ||
      envelope.target_handle.node_id.value != *grant.scope->node_id ||
      envelope.target_handle.expected_origin.kind != OriginKind::kTuple ||
      envelope.target_handle.expected_origin.serialization !=
          *grant.scope->origin->serialization ||
      envelope.required_graph_revision !=
          grant.scope->required_graph_revision ||
      envelope.target_handle.graph_revision !=
          grant.scope->required_graph_revision ||
      !DigestMatches(envelope.canonical_intent_digest,
                     grant.canonical_intent_digest) ||
      !ActionTypeMatchesClass(envelope.action_type, grant.action_class) ||
      !ActionTypeMatchesOperation(envelope.action_type, grant.operation_kind)) {
    return false;
  }
  return true;
}

// A typed task-owned action — a tab control or a store read — spends a grant
// whose identity, class, operation, context document and digest all match
// the immutable action. The caller has already checked that the action
// carries the one typed binding its operation needs.
bool TaskOwnedActionMatchesGrant(const mojom::TaskActionEffect& action,
                                 std::string_view task_id,
                                 const mojom::MintedCapabilityGrant& grant) {
  if (!action.document || !action.executable || !grant.scope ||
      !grant.scope->origin ||
      grant.scope->origin->kind != mojom::PolicyOriginKind::kTuple ||
      !grant.scope->origin->serialization || grant.scope->origin->opaque_id ||
      grant.scope->destination_scope || grant.scope->destination_address ||
      grant.scope->node_id) {
    return false;
  }
  return grant.task_id == task_id && grant.action_id == action.action_id &&
         grant.proposal_digest == action.proposal_digest &&
         grant.idempotency_key == action.idempotency_key &&
         grant.capability_id == action.capability_id &&
         grant.action_class == action.executable->action_class &&
         grant.operation_kind == action.executable->operation_kind &&
         grant.scope->tab_id == action.executable->tab_id &&
         grant.scope->frame_id == action.document->frame_id &&
         grant.scope->page_epoch == action.document->page_epoch &&
         grant.scope->required_graph_revision ==
             action.document->graph_revision &&
         *grant.scope->origin->serialization ==
             action.document->normalized_origin &&
         DigestMatches(crypto::SHA256Hash(action.executable->canonical_intent),
                       grant.canonical_intent_digest);
}

bool TaskTabActionMatchesGrant(const mojom::TaskActionEffect& action,
                               std::string_view task_id,
                               const mojom::MintedCapabilityGrant& grant) {
  return action.executable && action.executable->task_tab &&
         TaskOwnedActionMatchesGrant(action, task_id, grant);
}

bool TaskStoreActionMatchesGrant(const mojom::TaskActionEffect& action,
                                 std::string_view task_id,
                                 const mojom::MintedCapabilityGrant& grant) {
  return action.executable && action.executable->task_store &&
         grant.action_class == mojom::PolicyActionClass::kProfileStoreRead &&
         TaskOwnedActionMatchesGrant(action, task_id, grant);
}

}  // namespace

bool CapabilityLedger::CopyRegisteredCapability(
    const CapabilityReference& reference,
    CapabilityGrant& out) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = records_.find(reference);
  if (it == records_.end() || !it->second.grant ||
      it->second.state != State::kRegistered) {
    return false;
  }
  const mojom::MintedCapabilityGrant& grant = *it->second.grant;
  out.capability_reference = reference;
  out.actor_lease_id = ActorLeaseId{grant.actor_lease_id};
  out.expires_at_monotonic_ms = grant.expires_at_monotonic_ms;
  out.policy_version = grant.policy_version;
  if (grant.approval) {
    out.approval_receipt_reference =
        ApprovalReceiptReference{grant.approval->receipt_reference};
  } else {
    out.approval_receipt_reference.reset();
  }
  out.task_discovery.reset();
  if (grant.discovery) {
    if (!grant.scope || !grant.scope->origin ||
        grant.scope->origin->kind != mojom::PolicyOriginKind::kOpaque ||
        !grant.scope->origin->opaque_id) {
      return false;
    }
    out.task_discovery = TaskDiscoveryCapabilityBinding{
        .tab_id = TabId{grant.discovery->discovery_tab_id},
        .frame_id = FrameId{grant.scope->frame_id},
        .page_epoch = PageEpoch{grant.scope->page_epoch},
        .opaque_origin_id = *grant.scope->origin->opaque_id,
        .browser_session_id = grant.discovery->browser_session_id,
        .remaining_new_source_cap = grant.discovery->remaining_new_source_cap,
    };
  }
  return out.capability_reference.is_valid() && out.actor_lease_id.is_valid();
}

bool CapabilityLedger::TaskObservationMatchesRegisteredGrant(
    const mojom::TaskActionEffect& action) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!action.document || !action.executable ||
      action.executable->canonical_intent.empty() ||
      action.executable->canonical_intent.size() >
          mojom::kMaxCanonicalActionIntentBytes) {
    return false;
  }
  auto it = records_.find(CapabilityReference{action.capability_id});
  if (it == records_.end() || !it->second.grant ||
      it->second.state != State::kRegistered) {
    return false;
  }
  const mojom::MintedCapabilityGrant& grant = *it->second.grant;
  return grant.action_class == mojom::PolicyActionClass::kObservePage &&
         action.executable->action_class == grant.action_class &&
         grant.operation_kind == action.executable->operation_kind &&
         DigestMatches(crypto::SHA256Hash(action.executable->canonical_intent),
                       grant.canonical_intent_digest) &&
         grant.action_id == action.action_id &&
         grant.proposal_digest == action.proposal_digest &&
         grant.idempotency_key == action.idempotency_key && grant.scope &&
         grant.scope->tab_id == action.executable->tab_id &&
         grant.scope->frame_id == action.document->frame_id &&
         grant.scope->page_epoch == action.document->page_epoch &&
         grant.scope->required_graph_revision ==
             action.document->graph_revision &&
         ObservationNodeMatchesGrant(action, grant) &&
         !grant.scope->destination_scope && !grant.scope->destination_address;
}

CapabilityAdmission CapabilityLedger::AdmitTaskAction(
    const AuthorizedActionEnvelope& envelope,
    const ActorLeaseRegistry& leases,
    const TabId& tab_id,
    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!envelope.capability.capability_reference.is_valid() ||
      !envelope.action_digest.is_valid()) {
    return CapabilityAdmission::kMalformed;
  }

  auto record_it = records_.find(envelope.capability.capability_reference);
  if (record_it == records_.end() || !record_it->second.grant) {
    return CapabilityAdmission::kMalformed;
  }
  Record& record = record_it->second;
  if (record.state != State::kRegistered) {
    return CapabilityAdmission::kAlreadySpent;
  }
  if (record.expires_at <= now) {
    return CapabilityAdmission::kExpired;
  }
  const mojom::MintedCapabilityGrant& registered = *record.grant;
  if (!EnvelopeMatchesGrant(envelope, registered)) {
    return CapabilityAdmission::kDigestMismatch;
  }
  if (registered.scope->tab_id != tab_id.value) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  if (!leases.IsValidForGrant(envelope.capability.actor_lease_id,
                              mojom::AuthoritySubjectKind::kTask,
                              registered.task_id, tab_id, active_profile_id_,
                              active_generation_, record.expires_at, now)) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  record.state = State::kInFlight;
  return CapabilityAdmission::kAdmitted;
}

CapabilityAdmission CapabilityLedger::AdmitTaskTabAction(
    const mojom::TaskActionEffect& action,
    std::string_view task_id,
    const ActorLeaseRegistry& leases,
    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const CapabilityReference reference{action.capability_id};
  if (!reference.is_valid() || task_id.empty()) {
    return CapabilityAdmission::kMalformed;
  }
  auto record_it = records_.find(reference);
  if (record_it == records_.end() || !record_it->second.grant) {
    return CapabilityAdmission::kMalformed;
  }
  Record& record = record_it->second;
  if (record.state != State::kRegistered) {
    return CapabilityAdmission::kAlreadySpent;
  }
  if (record.expires_at <= now) {
    return CapabilityAdmission::kExpired;
  }
  const mojom::MintedCapabilityGrant& registered = *record.grant;
  if (!TaskTabActionMatchesGrant(action, task_id, registered)) {
    return CapabilityAdmission::kDigestMismatch;
  }
  const TabId tab_id{registered.scope->tab_id};
  if (!tab_id.is_valid() ||
      !leases.IsValidForGrant(ActorLeaseId{registered.actor_lease_id},
                              mojom::AuthoritySubjectKind::kTask,
                              registered.task_id, tab_id, active_profile_id_,
                              active_generation_, record.expires_at, now)) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  record.state = State::kInFlight;
  return CapabilityAdmission::kAdmitted;
}

CapabilityAdmission CapabilityLedger::AdmitTaskStoreAction(
    const mojom::TaskActionEffect& action,
    std::string_view task_id,
    const ActorLeaseRegistry& leases,
    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const CapabilityReference reference{action.capability_id};
  if (!reference.is_valid() || task_id.empty()) {
    return CapabilityAdmission::kMalformed;
  }
  auto record_it = records_.find(reference);
  if (record_it == records_.end() || !record_it->second.grant) {
    return CapabilityAdmission::kMalformed;
  }
  Record& record = record_it->second;
  if (record.state != State::kRegistered) {
    return CapabilityAdmission::kAlreadySpent;
  }
  if (record.expires_at <= now) {
    return CapabilityAdmission::kExpired;
  }
  const mojom::MintedCapabilityGrant& registered = *record.grant;
  if (!TaskStoreActionMatchesGrant(action, task_id, registered)) {
    return CapabilityAdmission::kDigestMismatch;
  }
  const TabId tab_id{registered.scope->tab_id};
  if (!tab_id.is_valid() ||
      !leases.IsValidForGrant(ActorLeaseId{registered.actor_lease_id},
                              mojom::AuthoritySubjectKind::kTask,
                              registered.task_id, tab_id, active_profile_id_,
                              active_generation_, record.expires_at, now)) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  record.state = State::kInFlight;
  return CapabilityAdmission::kAdmitted;
}

}  // namespace taffy
