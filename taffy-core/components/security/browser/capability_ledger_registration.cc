// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "content/public/browser/browser_thread.h"
#include "crypto/sha2.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

base::TimeTicks FromMonotonicMs(MonotonicMillis monotonic_ms) {
  return base::TimeTicks() + base::Milliseconds(monotonic_ms);
}

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= 256u;
}

bool IsDirectIntentId(std::string_view value) {
  return value.starts_with("direct-intent-") &&
         value.size() <= mojom::kMaxAuthoritySubjectIdBytes;
}

bool IsLowerHexDigest(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsValidOrigin(const mojom::PolicyOrigin& origin) {
  switch (origin.kind) {
    case mojom::PolicyOriginKind::kTuple:
      return origin.serialization.has_value() &&
             !origin.serialization->empty() && !origin.opaque_id.has_value();
    case mojom::PolicyOriginKind::kOpaque:
      return origin.opaque_id.has_value() && !origin.opaque_id->empty() &&
             !origin.serialization.has_value();
  }
  return false;
}

// Whether `value` spells `parsed` the way GURL does, allowing the one
// difference a person or a model produces by hand: an address naming an origin
// and nothing else. This is the same rule `ParseHttpAddressOrigin` in
// //taffy/browser/core_task_action.cc states, and it has to be stated twice
// because the two live in components that may not include each other. Both
// copies are load-bearing: policy admitted the address here and the ledger
// then refused to register the grant policy had already minted, which the
// utility turned into INVALID_REQUEST with nothing written down and the task
// engine recorded as `Deny(Unsupported)`. A model that typed
// `https://myaadhaar.uidai.gov.in` was refused its first move for a missing
// trailing slash.
bool SpellingIsCanonical(const GURL& parsed, const std::string& value) {
  const std::string& canonical = parsed.spec();
  return canonical == value ||
         (canonical.size() == value.size() + 1u && canonical.back() == '/' &&
          parsed.path() == "/" && !parsed.has_query() && !parsed.has_ref() &&
          canonical.compare(0, value.size(), value) == 0);
}

bool AddressMatchesTupleOrigin(const std::string& address,
                               const mojom::PolicyOrigin& origin) {
  if (origin.kind != mojom::PolicyOriginKind::kTuple || !origin.serialization) {
    return false;
  }
  const GURL parsed(address);
  if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS() ||
      parsed.has_username() || parsed.has_password() ||
      !SpellingIsCanonical(parsed, address)) {
    return false;
  }
  const url::Origin parsed_origin = url::Origin::Create(parsed);
  return !parsed_origin.opaque() &&
         parsed_origin.Serialize() == *origin.serialization;
}

bool OperationMatchesActionClass(mojom::TaskActionOperationKind operation,
                                 mojom::PolicyActionClass action_class) {
  using ActionClass = mojom::PolicyActionClass;
  using Operation = mojom::TaskActionOperationKind;
  switch (operation) {
    case Operation::kNavigate:
    case Operation::kSearch:
    case Operation::kLinkOpen:
      return action_class == ActionClass::kOpenLink;
    case Operation::kHistoryBack:
    case Operation::kHistoryForward:
    case Operation::kReload:
    case Operation::kStopLoading:
      return action_class == ActionClass::kControlTab;
    case Operation::kTabsOpen:
    case Operation::kTabsClose:
      return action_class == ActionClass::kCreateTaskTab;
    case Operation::kTabsActivate:
    case Operation::kDomFocus:
      return action_class == ActionClass::kMoveFocus;
    case Operation::kDomClick:
      return action_class == ActionClass::kSyntheticClick;
    case Operation::kDomScroll:
      return action_class == ActionClass::kScrollIntoView;
    case Operation::kFormFill:
      return action_class == ActionClass::kFillField;
    case Operation::kFormSelect:
      return action_class == ActionClass::kSelectOption;
    case Operation::kFormToggle:
      return action_class == ActionClass::kToggleControl;
    case Operation::kFormSubmit:
      return action_class == ActionClass::kSubmitForm;
    case Operation::kDownloadStart:
    case Operation::kDownloadCancel:
      return action_class == ActionClass::kStartDownload;
    case Operation::kLibrarySearch:
      return action_class == ActionClass::kLibraryRead;
    case Operation::kLibrarySave:
    case Operation::kLibraryRemove:
      return action_class == ActionClass::kLibraryWrite;
    case Operation::kMemorySearch:
      return action_class == ActionClass::kMemoryRead;
    case Operation::kMemorySave:
    case Operation::kMemoryUpdate:
    case Operation::kMemoryDelete:
      return action_class == ActionClass::kMemoryWrite;
    case Operation::kToolJob:
      return action_class == ActionClass::kExecuteToolJob;
    case Operation::kHistorySearch:
    case Operation::kHistoryRecent:
    case Operation::kBookmarksSearch:
    case Operation::kBookmarksList:
    case Operation::kOpenTabsList:
      return action_class == ActionClass::kProfileStoreRead;
    case Operation::kTabsList:
    case Operation::kDomQuery:
    case Operation::kDomRead:
    case Operation::kFormInspect:
    case Operation::kDownloadList:
    case Operation::kSelectionRead:
    case Operation::kImageDescribe:
    case Operation::kImageReadText:
    case Operation::kVideoInspect:
    case Operation::kPdfInspect:
    case Operation::kPageScreenshotInspect:
      return action_class == ActionClass::kObservePage;
  }
  return false;
}

bool OperationAdmitsDestinationAddress(
    mojom::TaskActionOperationKind operation) {
  using Operation = mojom::TaskActionOperationKind;
  return operation == Operation::kNavigate || operation == Operation::kSearch ||
         operation == Operation::kTabsOpen ||
         operation == Operation::kDownloadStart ||
         operation == Operation::kLinkOpen;
}

bool OperationRequiresDestinationAddress(
    mojom::TaskActionOperationKind operation) {
  using Operation = mojom::TaskActionOperationKind;
  return operation == Operation::kNavigate || operation == Operation::kSearch ||
         operation == Operation::kTabsOpen ||
         operation == Operation::kDownloadStart ||
         operation == Operation::kLinkOpen;
}

bool OperationNodeShapeIsValid(mojom::TaskActionOperationKind operation,
                               bool has_node) {
  using Operation = mojom::TaskActionOperationKind;
  switch (operation) {
    case Operation::kFormInspect:
    case Operation::kLinkOpen:
    case Operation::kFormFill:
    case Operation::kFormSelect:
    case Operation::kFormToggle:
    case Operation::kFormSubmit:
      return has_node;
    case Operation::kDomRead:
    case Operation::kDomQuery:
    case Operation::kSelectionRead:
    case Operation::kPageScreenshotInspect:
      return !has_node;
    default:
      // Other operation families retain their existing validation here. Their
      // executors still fail closed independently.
      return true;
  }
}

bool HasCanonicalIntentDigest(
    const std::vector<uint8_t>& canonical_intent_digest) {
  return canonical_intent_digest.size() == crypto::kSHA256Length &&
         std::any_of(canonical_intent_digest.begin(),
                     canonical_intent_digest.end(),
                     [](uint8_t byte) { return byte != 0u; });
}

bool DigestMatches(const std::vector<uint8_t>& wire, std::string_view local) {
  return wire.size() == crypto::kSHA256Length &&
         local.size() == crypto::kSHA256Length &&
         std::equal(wire.begin(), wire.end(), local.begin(),
                    [](uint8_t wire_byte, char local_byte) {
                      return wire_byte == static_cast<uint8_t>(local_byte);
                    });
}

// Whether this grant is the one other shape a task may hold against a
// document with no site of its own.
//
// A grant with no discovery authority ordinarily stands on a page, and a
// page's origin is a tuple. The exception is a move that leaves the error
// document Chromium writes when an address does not answer: there is no tuple
// to carry, the destination is what policy decided, and nothing about the
// document authorizes the move (decision 0176). Everything checked here is
// shape - the opaque identity itself grants nothing.
bool IsDepartureFromDocumentWithNoSite(
    const mojom::MintedCapabilityGrant& grant) {
  if (grant.discovery || !grant.scope || !grant.scope->origin) {
    return false;
  }
  return (grant.operation_kind == mojom::TaskActionOperationKind::kNavigate ||
          grant.operation_kind == mojom::TaskActionOperationKind::kSearch) &&
         grant.action_class == mojom::PolicyActionClass::kOpenLink &&
         grant.scope->origin->kind == mojom::PolicyOriginKind::kOpaque &&
         grant.scope->origin->opaque_id.has_value() &&
         IsIdentifier(*grant.scope->origin->opaque_id) &&
         !grant.scope->origin->serialization.has_value() &&
         grant.scope->destination_address.has_value() &&
         grant.scope->destination_scope &&
         grant.scope->destination_scope->kind ==
             mojom::PolicyOriginKind::kTuple &&
         !grant.scope->node_id && grant.scope->required_graph_revision == 0u &&
         grant.scope->allowed_redirects.empty();
}

bool IsExactTaskDiscoveryGrant(const mojom::MintedCapabilityGrant& grant) {
  if (!grant.discovery || !grant.scope || !grant.scope->origin ||
      !grant.scope->destination_scope || !grant.scope->destination_address ||
      !grant.principal) {
    return false;
  }
  const mojom::TaskDiscoveryAuthorityFact& discovery = *grant.discovery;
  return (grant.operation_kind == mojom::TaskActionOperationKind::kSearch ||
          (grant.operation_kind == mojom::TaskActionOperationKind::kNavigate &&
           GURL(*grant.scope->destination_address).SchemeIs("https"))) &&
         grant.action_class == mojom::PolicyActionClass::kOpenLink &&
         grant.scope->tab_id == discovery.discovery_tab_id &&
         IsIdentifier(discovery.browser_session_id) &&
         discovery.remaining_new_source_cap > 0u &&
         discovery.remaining_new_source_cap <= mojom::kMaxNewSourceCap &&
         grant.scope->origin->kind == mojom::PolicyOriginKind::kOpaque &&
         grant.scope->origin->opaque_id.has_value() &&
         !grant.scope->origin->serialization.has_value() &&
         grant.scope->destination_scope->kind ==
             mojom::PolicyOriginKind::kTuple &&
         grant.scope->destination_scope->serialization.has_value() &&
         !grant.scope->destination_scope->opaque_id.has_value() &&
         !grant.scope->node_id && grant.scope->required_graph_revision == 0u &&
         grant.scope->allowed_redirects.empty() &&
         grant.principal->kind == mojom::PolicyPrincipalKind::kAssistant &&
         !grant.principal->skill_version_id && !grant.approval &&
         grant.effective_risk ==
             mojom::PolicyRiskClass::kReversibleDisclosure &&
         grant.data_classes.size() == 1u &&
         grant.data_classes.front() == mojom::BipSensitivity::kNotSensitive;
}

}  // namespace

mojom::CapabilityRegistrationStatus CapabilityLedger::Register(
    const mojom::MintedCapabilityGrant& grant,
    const ActorLeaseRegistry& leases,
    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  using RegistrationStatus = mojom::CapabilityRegistrationStatus;
  PruneExpired(now);

  if (active_generation_ == 0u ||
      grant.service_generation != active_generation_) {
    return RegistrationStatus::kStaleGeneration;
  }
  const CapabilityReference reference{grant.capability_id};
  if (records_.contains(reference)) {
    // Identity collision is never success. The service cannot prove an
    // existing browser record is byte-for-byte identical after the round
    // trip, so accepting DUPLICATE could preserve a narrower or unrelated
    // grant under an ID Rust now believes it registered.
    return RegistrationStatus::kInvalidGrant;
  }
  if (!IsIdentifier(grant.capability_id) ||
      !IsIdentifier(grant.actor_lease_id) || !grant.authority_subject ||
      !IsIdentifier(grant.authority_subject->authority_subject_id) ||
      grant.authority_subject->authority_subject_id.size() >
          mojom::kMaxAuthoritySubjectIdBytes ||
      !IsLowerHexDigest(grant.proposal_digest) ||
      !IsIdentifier(grant.idempotency_key) || grant.policy_version == 0u ||
      !grant.principal || !grant.scope || !grant.scope->origin ||
      grant.scope->profile_id != active_profile_id_ ||
      !IsIdentifier(grant.scope->tab_id) ||
      !IsIdentifier(grant.scope->frame_id) ||
      !IsIdentifier(grant.scope->page_epoch) ||
      !IsValidOrigin(*grant.scope->origin) ||
      (grant.scope->destination_address &&
       (grant.scope->destination_address->empty() ||
        grant.scope->destination_address->size() >
            mojom::kMaxDestinationAddressBytes)) ||
      (grant.scope->destination_address.has_value() !=
       static_cast<bool>(grant.scope->destination_scope)) ||
      (grant.scope->destination_address &&
       (!grant.scope->destination_scope ||
        !AddressMatchesTupleOrigin(*grant.scope->destination_address,
                                   *grant.scope->destination_scope))) ||
      (grant.principal->kind == mojom::PolicyPrincipalKind::kSkill) !=
          grant.principal->skill_version_id.has_value()) {
    return RegistrationStatus::kInvalidGrant;
  }
  const bool direct = grant.authority_subject->kind ==
                      mojom::AuthoritySubjectKind::kDirectUserIntent;
  switch (grant.authority_subject->kind) {
    case mojom::AuthoritySubjectKind::kTask:
      if (!IsIdentifier(grant.task_id) || IsDirectIntentId(grant.task_id) ||
          grant.task_id != grant.authority_subject->authority_subject_id ||
          !IsIdentifier(grant.action_id) ||
          !HasCanonicalIntentDigest(grant.canonical_intent_digest) ||
          !OperationMatchesActionClass(grant.operation_kind,
                                       grant.action_class) ||
          (OperationRequiresDestinationAddress(grant.operation_kind) &&
           !grant.scope->destination_address) ||
          (!OperationAdmitsDestinationAddress(grant.operation_kind) &&
           grant.scope->destination_address) ||
          !OperationNodeShapeIsValid(grant.operation_kind,
                                     grant.scope->node_id.has_value()) ||
          !GraphRevisionFloorIsWellFormed(
              grant.scope->node_id.has_value(),
              grant.scope->required_graph_revision) ||
          (grant.discovery
               ? !IsExactTaskDiscoveryGrant(grant)
               : (grant.scope->origin->kind !=
                      mojom::PolicyOriginKind::kTuple &&
                  !IsDepartureFromDocumentWithNoSite(grant)))) {
        return RegistrationStatus::kInvalidGrant;
      }
      break;
    case mojom::AuthoritySubjectKind::kDirectUserIntent:
      if (!IsDirectIntentId(grant.authority_subject->authority_subject_id) ||
          !grant.task_id.empty() || !grant.action_id.empty() ||
          grant.action_class != mojom::PolicyActionClass::kObservePage ||
          grant.principal->kind != mojom::PolicyPrincipalKind::kAssistant ||
          grant.principal->skill_version_id.has_value() ||
          grant.effective_risk != mojom::PolicyRiskClass::kLocalRead ||
          grant.data_classes.size() != 1u ||
          grant.data_classes.front() != mojom::BipSensitivity::kNotSensitive ||
          grant.approval || grant.scope->node_id ||
          grant.scope->destination_scope || grant.scope->destination_address ||
          grant.operation_kind != mojom::TaskActionOperationKind::kDomRead ||
          grant.discovery ||
          !DigestMatches(grant.canonical_intent_digest,
                         crypto::SHA256HashString(grant.proposal_digest)) ||
          !grant.scope->allowed_redirects.empty() ||
          grant.scope->required_graph_revision != 0u ||
          grant.scope->origin->kind != mojom::PolicyOriginKind::kTuple) {
        return RegistrationStatus::kInvalidGrant;
      }
      break;
  }
  for (const auto& redirect : grant.scope->allowed_redirects) {
    if (!redirect || !IsValidOrigin(*redirect)) {
      return RegistrationStatus::kInvalidGrant;
    }
  }
  if (grant.scope->destination_scope &&
      !IsValidOrigin(*grant.scope->destination_scope)) {
    return RegistrationStatus::kInvalidGrant;
  }

  const base::TimeTicks issued_at =
      FromMonotonicMs(grant.issued_at_monotonic_ms);
  const base::TimeTicks expires_at =
      FromMonotonicMs(grant.expires_at_monotonic_ms);
  if (issued_at > now || expires_at <= now || expires_at <= issued_at) {
    return RegistrationStatus::kInvalidGrant;
  }
  if (direct && expires_at - issued_at >
                    base::Milliseconds(mojom::kMaxDirectObservationLeaseMs)) {
    return RegistrationStatus::kInvalidGrant;
  }
  if (grant.approval &&
      (grant.approval->service_generation != active_generation_ ||
       !IsIdentifier(grant.approval->receipt_reference) ||
       grant.approval->proposal_digest != grant.proposal_digest ||
       grant.approval->expires_at_utc_ms == 0u ||
       !IsIdentifier(grant.approval->browser_session_id) ||
       grant.approval->expires_at_monotonic_ms <
           grant.expires_at_monotonic_ms)) {
    return RegistrationStatus::kInvalidGrant;
  }
  if (!leases.IsValidForGrant(ActorLeaseId{grant.actor_lease_id},
                              grant.authority_subject->kind,
                              grant.authority_subject->authority_subject_id,
                              TabId{grant.scope->tab_id}, active_profile_id_,
                              active_generation_, expires_at, now)) {
    return RegistrationStatus::kLeaseMissing;
  }

  Record record;
  record.state = State::kRegistered;
  record.expires_at = expires_at;
  record.grant = grant.Clone();
  records_.emplace(reference, std::move(record));
  return RegistrationStatus::kRegistered;
}

}  // namespace taffy
