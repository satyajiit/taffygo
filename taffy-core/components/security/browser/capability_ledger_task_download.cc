// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

#include "content/public/browser/browser_thread.h"
#include "crypto/sha2.h"
#include "taffy/components/security/browser/action_authority.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool DigestMatches(const std::array<uint8_t, 32>& local,
                   const std::vector<uint8_t>& wire) {
  return wire.size() == local.size() &&
         std::equal(local.begin(), local.end(), wire.begin());
}

bool DestinationMatches(const mojom::TaskActionEffect& action,
                        const mojom::MintedCapabilityGrant& grant) {
  const bool starts = action.executable->operation_kind ==
                      mojom::TaskActionOperationKind::kDownloadStart;
  if (!starts) {
    return !action.executable->destination_origin &&
           !action.executable->destination_address &&
           !grant.scope->destination_scope && !grant.scope->destination_address;
  }
  return action.executable->destination_origin &&
         action.executable->destination_address &&
         grant.scope->destination_scope &&
         grant.scope->destination_scope->kind ==
             mojom::PolicyOriginKind::kTuple &&
         grant.scope->destination_scope->serialization &&
         !grant.scope->destination_scope->opaque_id &&
         grant.scope->destination_address &&
         *grant.scope->destination_scope->serialization ==
             *action.executable->destination_origin &&
         *grant.scope->destination_address ==
             *action.executable->destination_address;
}

bool TaskDownloadActionMatchesGrant(const mojom::TaskActionEffect& action,
                                    std::string_view task_id,
                                    const mojom::MintedCapabilityGrant& grant) {
  if (!action.document || !action.executable ||
      !action.executable->task_download || action.executable->task_tab ||
      !grant.scope || !grant.scope->origin ||
      grant.scope->origin->kind != mojom::PolicyOriginKind::kTuple ||
      !grant.scope->origin->serialization || grant.scope->origin->opaque_id ||
      grant.scope->node_id != action.executable->node_id ||
      !DestinationMatches(action, grant)) {
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

}  // namespace

CapabilityAdmission CapabilityLedger::AdmitTaskDownloadAction(
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
  if (!TaskDownloadActionMatchesGrant(action, task_id, registered)) {
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
