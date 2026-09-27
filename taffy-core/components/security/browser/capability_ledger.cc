// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <utility>

#include "content/public/browser/browser_thread.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

std::optional<AuthorizedObservationTarget> ObservationTargetForGrant(
    const core_service::mojom::MintedCapabilityGrant& grant) {
  namespace mojom = core_service::mojom;
  if (!grant.scope || grant.scope->destination_scope ||
      grant.scope->destination_address) {
    return std::nullopt;
  }
  AuthorizedObservationTarget target;
  switch (grant.authority_subject->kind) {
    case mojom::AuthoritySubjectKind::kDirectUserIntent:
      if (grant.operation_kind != mojom::TaskActionOperationKind::kDomRead ||
          grant.scope->node_id) {
        return std::nullopt;
      }
      target.scope = ObservationScope::kDocument;
      target.kind = AuthorizedObservationKind::kDocument;
      break;
    case mojom::AuthoritySubjectKind::kTask:
      switch (grant.operation_kind) {
        case mojom::TaskActionOperationKind::kDomQuery:
        case mojom::TaskActionOperationKind::kDomRead:
          if (grant.scope->node_id) {
            return std::nullopt;
          }
          target.scope = ObservationScope::kDocument;
          target.kind = AuthorizedObservationKind::kDocument;
          break;
        case mojom::TaskActionOperationKind::kFormInspect:
          if (!grant.scope->node_id) {
            return std::nullopt;
          }
          target.scope = ObservationScope::kSection;
          target.kind = AuthorizedObservationKind::kForm;
          target.form_root = FormObservationRoot{
              .node_id = SemanticNodeId{*grant.scope->node_id},
              .minimum_graph_revision =
                  grant.scope->required_graph_revision,
          };
          break;
        case mojom::TaskActionOperationKind::kSelectionRead:
          if (grant.scope->node_id) {
            return std::nullopt;
          }
          target.scope = ObservationScope::kSelection;
          target.kind = AuthorizedObservationKind::kSelection;
          break;
        case mojom::TaskActionOperationKind::kImageDescribe:
        case mojom::TaskActionOperationKind::kImageReadText:
        case mojom::TaskActionOperationKind::kVideoInspect:
          if (!grant.scope->node_id) {
            return std::nullopt;
          }
          target.scope = ObservationScope::kDocument;
          target.kind =
              grant.operation_kind ==
                      mojom::TaskActionOperationKind::kImageDescribe
                  ? AuthorizedObservationKind::kImageDescription
              : grant.operation_kind ==
                      mojom::TaskActionOperationKind::kImageReadText
                  ? AuthorizedObservationKind::kImageText
                  : AuthorizedObservationKind::kVideo;
          target.media_root = MediaObservationRoot{
              .node_id = SemanticNodeId{*grant.scope->node_id},
              .minimum_graph_revision =
                  grant.scope->required_graph_revision,
          };
          break;
        case mojom::TaskActionOperationKind::kPdfInspect:
          if (grant.scope->node_id) {
            return std::nullopt;
          }
          target.scope = ObservationScope::kDocument;
          target.kind = AuthorizedObservationKind::kPdf;
          break;
        case mojom::TaskActionOperationKind::kPageScreenshotInspect:
          if (grant.scope->node_id) {
            return std::nullopt;
          }
          target.scope = ObservationScope::kDocument;
          target.kind = AuthorizedObservationKind::kPageScreenshot;
          break;
        default:
          return std::nullopt;
      }
      break;
  }
  return target.is_valid()
             ? std::optional<AuthorizedObservationTarget>(std::move(target))
             : std::nullopt;
}

}  // namespace

CapabilityLedger::CapabilityLedger() = default;
CapabilityLedger::~CapabilityLedger() = default;

void CapabilityLedger::BeginGeneration(std::string profile_id,
                                       uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (generation == 0u || profile_id.empty()) {
    records_.clear();
    active_profile_id_.clear();
    active_generation_ = 0u;
    return;
  }
  if (active_generation_ != generation || active_profile_id_ != profile_id) {
    records_.clear();
  }
  active_profile_id_ = std::move(profile_id);
  active_generation_ = generation;
}

void CapabilityLedger::RevokeGeneration(uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (generation != active_generation_) {
    return;
  }
  records_.clear();
  active_profile_id_.clear();
  active_generation_ = 0u;
}

void CapabilityLedger::RevokeTask(std::string_view task_id,
                                  uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (generation != active_generation_ || task_id.empty()) {
    return;
  }
  std::erase_if(records_, [task_id, generation](const auto& entry) {
    const Record& record = entry.second;
    return record.grant && record.grant->service_generation == generation &&
           record.grant->authority_subject &&
           record.grant->authority_subject->kind ==
               core_service::mojom::AuthoritySubjectKind::kTask &&
           record.grant->authority_subject->authority_subject_id == task_id;
  });
}

CapabilityAdmission CapabilityLedger::AdmitObservation(
    const core_service::mojom::PageObservationEffect& effect,
    std::string_view committed_origin,
    const ActorLeaseRegistry& leases,
    base::TimeTicks now,
    AuthorizedObservationTarget* authorized_target) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const CapabilityReference reference{effect.capability_id};
  auto it = records_.find(reference);
  if (it == records_.end() || !it->second.grant) {
    return CapabilityAdmission::kMalformed;
  }
  Record& record = it->second;
  if (record.state != State::kRegistered) {
    return CapabilityAdmission::kAlreadySpent;
  }
  const core_service::mojom::MintedCapabilityGrant& grant = *record.grant;
  if (record.expires_at <= now) {
    return CapabilityAdmission::kExpired;
  }
  if (grant.action_class !=
          core_service::mojom::PolicyActionClass::kObservePage ||
      grant.service_generation != active_generation_ || !grant.scope ||
      !grant.authority_subject || !effect.authority_subject ||
      grant.scope->profile_id != active_profile_id_ ||
      grant.task_id != effect.task_id || grant.action_id != effect.action_id ||
      grant.proposal_digest != effect.proposal_digest ||
      grant.idempotency_key != effect.idempotency_key ||
      grant.scope->tab_id != effect.tab_id ||
      grant.scope->frame_id != effect.frame_id ||
      grant.scope->page_epoch != effect.page_epoch || !grant.scope->origin ||
      grant.scope->origin->kind !=
          core_service::mojom::PolicyOriginKind::kTuple ||
      !grant.scope->origin->serialization ||
      *grant.scope->origin->serialization != committed_origin) {
    return CapabilityAdmission::kDigestMismatch;
  }
  if (grant.authority_subject->kind != effect.authority_subject->kind ||
      grant.authority_subject->authority_subject_id !=
          effect.authority_subject->authority_subject_id) {
    return CapabilityAdmission::kDigestMismatch;
  }
  if (grant.authority_subject->kind ==
          core_service::mojom::AuthoritySubjectKind::kTask &&
      // The equality is unconditional: whatever the grant was minted against
      // is what the effect must name, and a zero on one side and a number on
      // the other is two different requests. The floor itself is only required
      // where the scope names a node to pin it to.
      (!GraphRevisionFloorIsWellFormed(grant.scope->node_id.has_value(),
                                       effect.expected_graph_revision) ||
       grant.scope->required_graph_revision !=
           effect.expected_graph_revision)) {
    return CapabilityAdmission::kDigestMismatch;
  }
  if (grant.authority_subject->kind ==
          core_service::mojom::AuthoritySubjectKind::kDirectUserIntent &&
      (!effect.task_id.empty() || !effect.action_id.empty() ||
       effect.expected_graph_revision != 0u ||
       effect.scope !=
           core_service::mojom::ObservationScope::kCurrentDocument ||
       effect.max_bytes !=
           core_service::mojom::kMaxDirectObservationTotalBytes ||
       effect.max_nodes != core_service::mojom::kMaxDirectObservationNodes ||
       effect.max_text_bytes !=
           core_service::mojom::kMaxDirectObservationTextBytes ||
       effect.max_frames != core_service::mojom::kMaxDirectObservationFrames ||
       effect.deadline_ms !=
           core_service::mojom::kMaxDirectObservationDeadlineMs)) {
    return CapabilityAdmission::kDigestMismatch;
  }
  const std::optional<AuthorizedObservationTarget> target =
      ObservationTargetForGrant(grant);
  if (!target) {
    return CapabilityAdmission::kDigestMismatch;
  }
  if (!leases.IsValidForGrant(ActorLeaseId{grant.actor_lease_id},
                              grant.authority_subject->kind,
                              grant.authority_subject->authority_subject_id,
                              TabId{grant.scope->tab_id}, active_profile_id_,
                              active_generation_, record.expires_at, now)) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  record.state = State::kInFlight;
  if (authorized_target) {
    *authorized_target = *target;
  }
  return CapabilityAdmission::kAdmitted;
}

CapabilityAdmission CapabilityLedger::Admit(
    const CapabilityGrant& grant,
    const ContentDigest& authorized_digest,
    const ContentDigest& computed_digest,
    const ActorLeaseRegistry& leases,
    const TabId& tab_id,
    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!grant.capability_reference.is_valid() || !authorized_digest.is_valid() ||
      !computed_digest.is_valid()) {
    return CapabilityAdmission::kMalformed;
  }

  auto record_it = records_.find(grant.capability_reference);
  if (record_it == records_.end() || !record_it->second.grant) {
    return CapabilityAdmission::kMalformed;
  }
  Record& record = record_it->second;
  if (record.state != State::kRegistered) {
    return CapabilityAdmission::kAlreadySpent;
  }
  const core_service::mojom::MintedCapabilityGrant& registered = *record.grant;
  if (record.expires_at <= now) {
    return CapabilityAdmission::kExpired;
  }
  if (authorized_digest != computed_digest ||
      authorized_digest.value != registered.proposal_digest) {
    return CapabilityAdmission::kDigestMismatch;
  }
  if (!grant.actor_lease_id.is_valid() ||
      registered.actor_lease_id != grant.actor_lease_id.value ||
      registered.policy_version != grant.policy_version ||
      registered.expires_at_monotonic_ms != grant.expires_at_monotonic_ms ||
      !registered.scope) {
    return CapabilityAdmission::kLeaseMissing;
  }
  if (registered.scope->tab_id != tab_id.value) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  if (!leases.IsValidForGrant(grant.actor_lease_id,
                              core_service::mojom::AuthoritySubjectKind::kTask,
                              registered.task_id, tab_id, active_profile_id_,
                              active_generation_, record.expires_at, now)) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  record.state = State::kInFlight;
  return CapabilityAdmission::kAdmitted;
}

void CapabilityLedger::Settle(const CapabilityReference& capability_reference,
                              ActionResultCode code) {
  auto it = records_.find(capability_reference);
  if (it == records_.end() || it->second.state == State::kRegistered) {
    return;
  }
  it->second.state = State::kSettled;
  it->second.terminal_code = code;
}

bool CapabilityLedger::IsSpent(
    const CapabilityReference& capability_reference) const {
  auto it = records_.find(capability_reference);
  return it != records_.end() && it->second.state != State::kRegistered;
}

void CapabilityLedger::PruneExpired(base::TimeTicks now) {
  std::erase_if(records_, [now](const auto& entry) {
    return entry.second.expires_at <= now;
  });
}

}  // namespace taffy
