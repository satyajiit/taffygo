// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_policy.h"

#include <stdint.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

// Every value below arrives as a `uint8_t` from the sandboxed Rust core, and
// every one of them is a member of a closed enumeration the browser's
// authority ledger then trusts. `static_cast` would turn an unknown byte into
// an out-of-range enumerator — undefined behaviour, and a fail-open here,
// because the grant would leave this function carrying an action class or a
// risk class that no later switch can match. The generated decoders refuse
// instead, and a refusal travels the path `RustCore::EvaluatePolicy` already
// has for a request it cannot project: a null result, which
// `CoreServiceImpl::OnPolicyEvaluated` reports as kInvalidRequest.
namespace wire = core_service::wire;

namespace {

bridge::BridgePolicyOperation ToOperation(
    const mojom::OperationEnvelope& input) {
  bridge::BridgePolicyOperation output;
  output.operation_id = input.operation_id;
  output.service_generation = input.service_generation;
  output.task_revision = input.task_revision;
  output.deadline_monotonic_ms = input.deadline_monotonic_ms;
  output.idempotency_key = input.idempotency_key;
  return output;
}

bridge::BridgePrincipal ToPrincipal(const mojom::PolicyPrincipal& input) {
  bridge::BridgePrincipal output;
  output.kind = static_cast<uint8_t>(input.kind);
  output.has_skill_version_id = input.skill_version_id.has_value();
  output.skill_version_id = input.skill_version_id.value_or(std::string());
  return output;
}

bridge::BridgeOrigin ToOrigin(const mojom::PolicyOrigin& input) {
  bridge::BridgeOrigin output;
  output.kind = static_cast<uint8_t>(input.kind);
  output.has_serialization = input.serialization.has_value();
  output.serialization = input.serialization.value_or(std::string());
  output.has_opaque_id = input.opaque_id.has_value();
  output.opaque_id = input.opaque_id.value_or(std::string());
  return output;
}

std::optional<bridge::BridgePolicyScope> ToScope(
    const mojom::PolicyCapabilityScope& input) {
  if (!input.origin) {
    return std::nullopt;
  }
  bridge::BridgePolicyScope output;
  output.profile_id = input.profile_id;
  output.tab_id = input.tab_id;
  output.frame_id = input.frame_id;
  output.page_epoch = input.page_epoch;
  output.origin = ToOrigin(*input.origin);
  output.has_node_id = input.node_id.has_value();
  output.node_id = input.node_id.value_or(std::string());
  output.has_destination_scope = !!input.destination_scope;
  if (input.destination_scope) {
    output.destination_scope = ToOrigin(*input.destination_scope);
  }
  output.has_destination_address = input.destination_address.has_value();
  output.destination_address =
      input.destination_address.value_or(std::string());
  output.required_graph_revision = input.required_graph_revision;
  for (const auto& redirect : input.allowed_redirects) {
    if (!redirect) {
      return std::nullopt;
    }
    output.allowed_redirects.push_back(ToOrigin(*redirect));
  }
  return output;
}

bridge::BridgeApprovalFact ToApproval(const mojom::PolicyApprovalFact& input) {
  bridge::BridgeApprovalFact output;
  output.receipt_reference = input.receipt_reference;
  output.proposal_digest = input.proposal_digest;
  output.service_generation = input.service_generation;
  output.expires_at_monotonic_ms = input.expires_at_monotonic_ms;
  output.expires_at_utc_ms = input.expires_at_utc_ms;
  output.browser_session_id = input.browser_session_id;
  return output;
}

bridge::BridgeAuthoritySubject ToAuthoritySubject(
    const mojom::AuthoritySubject& input) {
  bridge::BridgeAuthoritySubject output;
  output.kind = static_cast<uint8_t>(input.kind);
  output.authority_subject_id = input.authority_subject_id;
  return output;
}

bridge::BridgeTaskDiscoveryAuthority ToDiscoveryAuthority(
    const mojom::TaskDiscoveryAuthorityFact& input) {
  bridge::BridgeTaskDiscoveryAuthority output;
  output.discovery_tab_id = input.discovery_tab_id;
  output.browser_session_id = input.browser_session_id;
  output.remaining_new_source_cap = input.remaining_new_source_cap;
  return output;
}

mojom::PolicyPrincipalPtr ToMojoPrincipal(
    const bridge::BridgePrincipal& input) {
  const std::optional<mojom::PolicyPrincipalKind> kind =
      wire::PolicyPrincipalKindFromWire(input.kind);
  if (!kind) {
    return nullptr;
  }
  auto output = mojom::PolicyPrincipal::New();
  output->kind = *kind;
  if (input.has_skill_version_id) {
    output->skill_version_id = std::string(input.skill_version_id);
  }
  return output;
}

mojom::PolicyOriginPtr ToMojoOrigin(const bridge::BridgeOrigin& input) {
  const std::optional<mojom::PolicyOriginKind> kind =
      wire::PolicyOriginKindFromWire(input.kind);
  if (!kind) {
    return nullptr;
  }
  auto output = mojom::PolicyOrigin::New();
  output->kind = *kind;
  if (input.has_serialization) {
    output->serialization = std::string(input.serialization);
  }
  if (input.has_opaque_id) {
    output->opaque_id = std::string(input.opaque_id);
  }
  return output;
}

mojom::PolicyCapabilityScopePtr ToMojoScope(
    const bridge::BridgePolicyScope& input) {
  if (input.has_destination_address == input.destination_address.empty()) {
    return nullptr;
  }
  auto output = mojom::PolicyCapabilityScope::New();
  output->profile_id = std::string(input.profile_id);
  output->tab_id = std::string(input.tab_id);
  output->frame_id = std::string(input.frame_id);
  output->page_epoch = std::string(input.page_epoch);
  output->origin = ToMojoOrigin(input.origin);
  if (!output->origin) {
    return nullptr;
  }
  if (input.has_node_id) {
    output->node_id = std::string(input.node_id);
  }
  if (input.has_destination_scope) {
    output->destination_scope = ToMojoOrigin(input.destination_scope);
    if (!output->destination_scope) {
      return nullptr;
    }
  }
  if (input.has_destination_address) {
    output->destination_address = std::string(input.destination_address);
  }
  output->required_graph_revision = input.required_graph_revision;
  for (const bridge::BridgeOrigin& redirect : input.allowed_redirects) {
    mojom::PolicyOriginPtr projected = ToMojoOrigin(redirect);
    if (!projected) {
      return nullptr;
    }
    output->allowed_redirects.push_back(std::move(projected));
  }
  return output;
}

mojom::PolicyApprovalFactPtr ToMojoApproval(
    const bridge::BridgeApprovalFact& input) {
  auto output = mojom::PolicyApprovalFact::New();
  output->receipt_reference = std::string(input.receipt_reference);
  output->proposal_digest = std::string(input.proposal_digest);
  output->service_generation = input.service_generation;
  output->expires_at_monotonic_ms = input.expires_at_monotonic_ms;
  output->expires_at_utc_ms = input.expires_at_utc_ms;
  output->browser_session_id = std::string(input.browser_session_id);
  return output;
}

mojom::AuthoritySubjectPtr ToMojoAuthoritySubject(
    const bridge::BridgeAuthoritySubject& input) {
  const std::optional<mojom::AuthoritySubjectKind> kind =
      wire::AuthoritySubjectKindFromWire(input.kind);
  if (!kind) {
    return nullptr;
  }
  auto output = mojom::AuthoritySubject::New();
  output->kind = *kind;
  output->authority_subject_id = std::string(input.authority_subject_id);
  return output;
}

mojom::TaskDiscoveryAuthorityFactPtr ToMojoDiscoveryAuthority(
    const bridge::BridgeTaskDiscoveryAuthority& input) {
  auto output = mojom::TaskDiscoveryAuthorityFact::New();
  output->discovery_tab_id = std::string(input.discovery_tab_id);
  output->browser_session_id = std::string(input.browser_session_id);
  output->remaining_new_source_cap = input.remaining_new_source_cap;
  return output;
}

mojom::MintedCapabilityGrantPtr ToMojoGrant(
    const bridge::BridgeMintedGrant& input) {
  const std::optional<mojom::PolicyActionClass> action_class =
      wire::PolicyActionClassFromWire(input.action_class);
  const std::optional<mojom::PolicyRiskClass> effective_risk =
      wire::PolicyRiskClassFromWire(input.effective_risk);
  const std::optional<mojom::TaskActionOperationKind> operation_kind =
      wire::TaskActionOperationKindFromWire(input.operation_kind);
  if (!action_class || !effective_risk || !operation_kind) {
    return nullptr;
  }
  auto output = mojom::MintedCapabilityGrant::New();
  output->capability_id = std::string(input.capability_id);
  output->service_generation = input.service_generation;
  output->policy_version = input.policy_version;
  output->actor_lease_id = std::string(input.actor_lease_id);
  output->task_id = std::string(input.task_id);
  output->action_id = std::string(input.action_id);
  output->action_class = *action_class;
  output->operation_kind = *operation_kind;
  output->canonical_intent_digest.assign(input.canonical_intent_digest.begin(),
                                         input.canonical_intent_digest.end());
  output->principal = ToMojoPrincipal(input.principal);
  output->proposal_digest = std::string(input.proposal_digest);
  output->idempotency_key = std::string(input.idempotency_key);
  output->scope = ToMojoScope(input.scope);
  for (uint8_t sensitivity : input.data_classes) {
    const std::optional<mojom::BipSensitivity> data_class =
        wire::BipSensitivityFromWire(sensitivity);
    if (!data_class) {
      return nullptr;
    }
    output->data_classes.push_back(*data_class);
  }
  output->effective_risk = *effective_risk;
  if (input.has_approval) {
    output->approval = ToMojoApproval(input.approval);
  }
  output->issued_at_monotonic_ms = input.issued_at_monotonic_ms;
  output->expires_at_monotonic_ms = input.expires_at_monotonic_ms;
  output->authority_subject = ToMojoAuthoritySubject(input.authority_subject);
  if (input.has_discovery) {
    output->discovery = ToMojoDiscoveryAuthority(input.discovery);
  }
  if (!output->principal || !output->scope || !output->authority_subject) {
    return nullptr;
  }
  return output;
}

mojom::EffectEnvelopePtr ToMojoDirectObservationEffect(
    const bridge::BridgeDirectObservationEffect& input) {
  const std::optional<mojom::ObservationScope> scope =
      wire::ObservationScopeFromWire(input.scope);
  if (!scope) {
    return nullptr;
  }
  auto output = mojom::EffectEnvelope::New();
  output->operation = mojom::OperationEnvelope::New();
  output->operation->operation_id = std::string(input.operation.operation_id);
  output->operation->service_generation = input.operation.service_generation;
  output->operation->task_revision = input.operation.task_revision;
  output->operation->deadline_monotonic_ms =
      input.operation.deadline_monotonic_ms;
  output->operation->idempotency_key =
      std::string(input.operation.idempotency_key);
  output->effect_id = std::string(input.effect_id);
  output->kind = mojom::EffectKind::kPageObservation;
  output->retry_class = mojom::RetryClass::kIdempotent;
  output->page_observation = mojom::PageObservationEffect::New();
  output->page_observation->tab_id = std::string(input.tab_id);
  output->page_observation->frame_id = std::string(input.frame_id);
  output->page_observation->page_epoch = std::string(input.page_epoch);
  output->page_observation->scope = *scope;
  output->page_observation->max_bytes = input.max_bytes;
  output->page_observation->task_id = std::string(input.task_id);
  output->page_observation->action_id = std::string(input.action_id);
  output->page_observation->capability_id = std::string(input.capability_id);
  output->page_observation->proposal_digest =
      std::string(input.proposal_digest);
  output->page_observation->idempotency_key =
      std::string(input.idempotency_key);
  output->page_observation->authority_subject =
      ToMojoAuthoritySubject(input.authority_subject);
  if (!output->page_observation->authority_subject) {
    return nullptr;
  }
  output->page_observation->max_nodes = input.max_nodes;
  output->page_observation->max_text_bytes = input.max_text_bytes;
  output->page_observation->max_frames = input.max_frames;
  output->page_observation->deadline_ms = input.deadline_ms;
  output->page_observation->expected_graph_revision =
      input.expected_graph_revision;
  return output;
}

}  // namespace

std::optional<bridge::BridgePolicyRequest> ToBridgePolicyRequest(
    const mojom::PolicyEvaluationRequest& input) {
  if (!input.operation || !input.principal || !input.scope ||
      !input.actor_lease || !input.authority_subject ||
      !input.actor_lease->authority_subject) {
    return std::nullopt;
  }
  std::optional<bridge::BridgePolicyScope> scope = ToScope(*input.scope);
  if (!scope) {
    return std::nullopt;
  }
  bridge::BridgePolicyRequest output;
  output.operation = ToOperation(*input.operation);
  output.now_monotonic_ms = input.now_monotonic_ms;
  output.now_utc_ms = input.now_utc_ms;
  output.action_id = input.action_id;
  output.task_id = input.task_id;
  output.principal = ToPrincipal(*input.principal);
  output.action_class = static_cast<uint8_t>(input.action_class);
  output.operation_kind = static_cast<uint8_t>(input.operation_kind);
  if (input.canonical_intent_digest.size() !=
      output.canonical_intent_digest.size()) {
    return std::nullopt;
  }
  std::copy(input.canonical_intent_digest.begin(),
            input.canonical_intent_digest.end(),
            output.canonical_intent_digest.begin());
  output.proposal_digest = input.proposal_digest;
  output.scope = std::move(*scope);
  for (mojom::BipSensitivity sensitivity : input.data_classes) {
    output.data_classes.push_back(static_cast<uint8_t>(sensitivity));
  }
  output.context_risk = static_cast<uint8_t>(input.context_risk);
  output.expires_at_monotonic_ms = input.expires_at_monotonic_ms;
  output.actor_lease.lease_id = input.actor_lease->lease_id;
  output.actor_lease.service_generation = input.actor_lease->service_generation;
  output.actor_lease.task_id = input.actor_lease->task_id;
  output.actor_lease.authority_subject =
      ToAuthoritySubject(*input.actor_lease->authority_subject);
  output.actor_lease.profile_id = input.actor_lease->profile_id;
  output.actor_lease.tab_id = input.actor_lease->tab_id;
  output.actor_lease.control_mode =
      static_cast<uint8_t>(input.actor_lease->control_mode);
  output.actor_lease.expires_at_monotonic_ms =
      input.actor_lease->expires_at_monotonic_ms;
  output.has_approval = !!input.approval;
  if (input.approval) {
    output.approval = ToApproval(*input.approval);
  }
  output.context = static_cast<uint8_t>(input.context);
  output.authority_subject = ToAuthoritySubject(*input.authority_subject);
  output.policy_version = input.policy_version;
  output.has_discovery = !!input.discovery;
  if (input.discovery) {
    output.discovery = ToDiscoveryAuthority(*input.discovery);
  }
  return output;
}

mojom::PolicyEvaluationResultPtr ToMojoPolicyResult(
    bridge::BridgePolicyResult input) {
  const std::optional<mojom::PolicyEvaluationStatus> status =
      wire::PolicyEvaluationStatusFromWire(input.status);
  if (!status) {
    return nullptr;
  }
  auto output = mojom::PolicyEvaluationResult::New();
  output->operation_id = std::string(input.operation_id);
  output->status = *status;
  if (input.has_denial_code) {
    const std::optional<mojom::TaskActionResultCode> code =
        wire::TaskActionResultCodeFromWire(input.denial_code);
    if (!code) {
      return nullptr;
    }
    output->denial = mojom::PolicyDenial::New(*code);
  }
  if (input.has_minted_grant) {
    output->minted_grant = ToMojoGrant(input.minted_grant);
    if (!output->minted_grant) {
      return nullptr;
    }
  }
  if (input.has_direct_observation_effect) {
    output->direct_observation_effect =
        ToMojoDirectObservationEffect(input.direct_observation_effect);
    if (!output->direct_observation_effect) {
      return nullptr;
    }
  }
  return output;
}

}  // namespace taffy::core_service_internal
