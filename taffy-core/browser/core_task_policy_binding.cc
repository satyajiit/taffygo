// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>

#include "crypto/sha2.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/common/public/bip_identity.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

}  // namespace

mojom::PolicyEvaluationRequestPtr BindReadOnlyTaskPolicyRequest(
    const mojom::TaskPolicyEffect& effect,
    const TaskPolicyDocumentBinding& document,
    const std::string& browser_profile_id,
    const ActorLeaseResult& lease,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms,
    std::optional<mojom::TaskActionResultCode>* denial_code) {
  if (!effect.operation || !effect.principal ||
      lease.code != ActorLeaseResultCode::kIssued ||
      !lease.lease_id.is_valid() ||
      lease.expires_at_monotonic_ms <= now_monotonic_ms || now_utc_ms == 0u ||
      browser_profile_id.empty() || document.tab_id != effect.tab_id ||
      !IsIdentifier(document.frame_id) || !IsIdentifier(document.page_epoch) ||
      // Which origin shape the scope carries is a fact about the document,
      // not about the kind of tab it sits in. A discovery blank and the error
      // document Chromium writes when an address does not answer are both
      // documents with no site of their own, and both bind an opaque scope
      // (decision 0176). A discovery effect must still be one of them.
      (effect.discovery && !document.opaque_origin_id) ||
      (document.opaque_origin_id
           ? (!document.origin.empty() ||
              !IsIdentifier(*document.opaque_origin_id) ||
              document.graph_revision != 0u)
           : document.origin.empty()) ||
      !GraphRevisionFloorIsWellFormed(effect.node_id.has_value(),
                                      document.graph_revision)) {
    return nullptr;
  }

  auto request = mojom::PolicyEvaluationRequest::New();
  request->operation = effect.operation.Clone();
  request->now_monotonic_ms = now_monotonic_ms;
  request->now_utc_ms = now_utc_ms;
  request->action_id = effect.action_id;
  request->task_id = effect.task_id;
  request->principal = effect.principal.Clone();
  request->action_class = effect.action_class;
  request->operation_kind = effect.operation_kind;
  const auto canonical_intent_digest =
      crypto::SHA256Hash(effect.canonical_intent);
  request->canonical_intent_digest.assign(canonical_intent_digest.begin(),
                                          canonical_intent_digest.end());
  request->proposal_digest = effect.proposal_digest;
  request->scope = mojom::PolicyCapabilityScope::New();
  request->scope->profile_id = browser_profile_id;
  request->scope->tab_id = document.tab_id;
  request->scope->frame_id = document.frame_id;
  request->scope->page_epoch = document.page_epoch;
  request->scope->origin = mojom::PolicyOrigin::New();
  if (document.opaque_origin_id) {
    request->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
    request->scope->origin->opaque_id = *document.opaque_origin_id;
  } else {
    request->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
    request->scope->origin->serialization = document.origin;
  }
  request->scope->required_graph_revision = document.graph_revision;
  if (effect.node_id) {
    request->scope->node_id = *effect.node_id;
  }
  if ((IsTaskNavigationOperation(effect.operation_kind) ||
       IsTaskSearchOperation(effect.operation_kind) ||
       IsTaskTabOpenOperation(effect.operation_kind) ||
       effect.operation_kind ==
           mojom::TaskActionOperationKind::kDownloadStart ||
       IsTaskLinkOpenOperation(effect.operation_kind)) &&
      (!effect.node_id || IsTaskLinkOpenOperation(effect.operation_kind) ||
       effect.operation_kind == mojom::TaskActionOperationKind::kDownloadStart)) {
    const std::optional<std::string>& destination =
        document.destination_address ? document.destination_address
                                     : effect.destination_address;
    const std::optional<std::string>& destination_origin =
        document.destination_origin
            ? document.destination_origin
            : std::optional<std::string>(document.origin);
    if (!destination || !destination_origin) {
      return nullptr;
    }
    if (!HttpAddressOriginEquals(*destination, *destination_origin)) {
      // Understood and refused: the address leads outside the origin this
      // document's authority reaches. The task engine settles the action
      // under this code and the model is told, rather than the core being
      // handed a malformed answer.
      if (denial_code) {
        *denial_code = mojom::TaskActionResultCode::kEgressNotAuthorized;
      }
      return nullptr;
    }
    request->scope->destination_scope = mojom::PolicyOrigin::New();
    request->scope->destination_scope->kind = mojom::PolicyOriginKind::kTuple;
    request->scope->destination_scope->serialization = *destination_origin;
    request->scope->destination_address = *destination;
  }
  request->data_classes = effect.data_classes;
  request->context_risk = effect.context_risk;
  request->expires_at_monotonic_ms = std::min(
      effect.operation->deadline_monotonic_ms,
      std::min(lease.expires_at_monotonic_ms,
               effect.approval ? effect.approval->expires_at_monotonic_ms
                               : lease.expires_at_monotonic_ms));
  request->actor_lease = mojom::ActorLeaseFact::New();
  request->actor_lease->lease_id = lease.lease_id.value;
  request->actor_lease->service_generation = service_generation;
  request->actor_lease->task_id = effect.task_id;
  request->actor_lease->profile_id = browser_profile_id;
  request->actor_lease->tab_id = document.tab_id;
  request->actor_lease->control_mode = effect.control_mode;
  request->actor_lease->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
  request->actor_lease->authority_subject = mojom::AuthoritySubject::New();
  request->actor_lease->authority_subject->kind =
      mojom::AuthoritySubjectKind::kTask;
  request->actor_lease->authority_subject->authority_subject_id =
      effect.task_id;
  request->approval = effect.approval.Clone();
  request->context = effect.discovery
                         ? mojom::PolicyEvaluationContext::kTaskDiscovery
                         : mojom::PolicyEvaluationContext::kTask;
  request->discovery = effect.discovery.Clone();
  request->authority_subject = mojom::AuthoritySubject::New();
  request->authority_subject->kind = mojom::AuthoritySubjectKind::kTask;
  request->authority_subject->authority_subject_id = effect.task_id;
  request->policy_version = effect.policy_version;
  return request;
}

}  // namespace taffy
