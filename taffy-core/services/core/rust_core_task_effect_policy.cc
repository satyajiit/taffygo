// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <string>
#include <utility>

#include "taffy/services/core/rust_core_task_effect_projection.h"
#include "taffy/services/core/rust_core_task_effect_records.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

mojom::OperationEnvelopePtr ToMojoOperation(
    const bridge::BridgeOperation& input) {
  auto out = mojom::OperationEnvelope::New();
  out->operation_id = std::string(input.operation_id);
  out->service_generation = input.service_generation;
  out->task_revision = input.task_revision;
  out->deadline_monotonic_ms = input.deadline_monotonic_ms;
  out->idempotency_key = std::string(input.idempotency_key);
  return out;
}

std::optional<mojom::TaskPolicyEffectPtr> ToMojoPolicyEffect(
    const bridge::BridgeTaskEffect& input) {
  const auto action_class = ClosedEnum(
      input.action_class, mojom::PolicyActionClass::kProfileStoreRead);
  const auto action_operation = ClosedEnum(
      input.action_operation, mojom::TaskActionOperationKind::kOpenTabsList);
  const auto principal =
      ClosedEnum(input.principal, mojom::PolicyPrincipalKind::kSkill);
  const auto risk =
      ClosedEnum(input.context_risk, mojom::PolicyRiskClass::kProhibitedAbuse);
  const auto control =
      ClosedEnum(input.control_mode, mojom::TaskControlMode::kAssistant);
  if (!action_class || !action_operation || !principal || !risk || !control ||
      !ValidIdentifier(input.action_id) ||
      !ValidIdentifier(input.action_idempotency_key) ||
      !ValidIdentifier(input.tab_id) || !ValidDigest(input.proposal_digest) ||
      input.has_node_id != !input.node_id.empty() ||
      input.has_destination_address != !input.destination_address.empty() ||
      input.has_transient_search_query !=
          !input.transient_search_query.empty() ||
      input.has_transient_search_query !=
          (*action_operation == mojom::TaskActionOperationKind::kSearch) ||
      input.transient_search_query.size() >
          mojom::kMaxTransientSearchQueryBytes ||
      input.has_policy_discovery !=
          (!input.policy_discovery_tab_id.empty() &&
           !input.policy_discovery_browser_session_id.empty() &&
           input.policy_discovery_remaining_new_source_cap != 0u) ||
      (!input.has_policy_discovery &&
       (!input.policy_discovery_tab_id.empty() ||
        !input.policy_discovery_browser_session_id.empty() ||
        input.policy_discovery_remaining_new_source_cap != 0u)) ||
      (input.has_policy_discovery &&
       (!ValidIdentifier(input.policy_discovery_tab_id) ||
        !ValidIdentifier(input.policy_discovery_browser_session_id) ||
        input.policy_discovery_tab_id != input.tab_id ||
        (*action_operation != mojom::TaskActionOperationKind::kSearch &&
         *action_operation != mojom::TaskActionOperationKind::kNavigate) ||
        ((*action_operation == mojom::TaskActionOperationKind::kNavigate) !=
         input.has_destination_address) ||
        *action_class != mojom::PolicyActionClass::kOpenLink ||
        *principal != mojom::PolicyPrincipalKind::kAssistant ||
        *risk != mojom::PolicyRiskClass::kReversibleDisclosure ||
        *control != mojom::TaskControlMode::kAssistant || input.has_node_id ||
        input.has_approval || input.data_classes.size() != 1u ||
        input.data_classes[0] !=
            static_cast<uint8_t>(mojom::BipSensitivity::kNotSensitive) ||
        input.policy_discovery_remaining_new_source_cap >
            mojom::kMaxNewSourceCap)) ||
      input.canonical_intent.empty() ||
      input.canonical_intent.size() > mojom::kMaxCanonicalActionIntentBytes ||
      input.policy_version == 0u) {
    return std::nullopt;
  }
  std::optional<mojom::TaskActionInputPtr> action_input =
      ActionInput(input, *action_operation);
  if (!action_input) {
    return std::nullopt;
  }
  auto out = mojom::TaskPolicyEffect::New();
  out->operation = ToMojoOperation(input.operation);
  out->effect_id = std::string(input.effect_id);
  out->task_id = std::string(input.task_id);
  out->action_id = std::string(input.action_id);
  out->action_class = *action_class;
  out->operation_kind = *action_operation;
  out->canonical_intent.assign(input.canonical_intent.begin(),
                               input.canonical_intent.end());
  out->tool_name = std::string(input.tool_name);
  out->input = std::move(*action_input);
  out->proposal_digest = std::string(input.proposal_digest);
  out->idempotency_key = std::string(input.action_idempotency_key);
  out->tab_id = std::string(input.tab_id);
  if (input.has_transient_search_query) {
    out->transient_search_query = std::string(input.transient_search_query);
  }
  if (input.has_policy_discovery) {
    out->discovery = mojom::TaskDiscoveryAuthorityFact::New(
        std::string(input.policy_discovery_tab_id),
        std::string(input.policy_discovery_browser_session_id),
        input.policy_discovery_remaining_new_source_cap);
  }
  if (input.has_destination_address) {
    if (input.destination_address.empty() ||
        input.destination_address.size() > mojom::kMaxDestinationAddressBytes) {
      return std::nullopt;
    }
    out->destination_address = std::string(input.destination_address);
  }
  if (input.has_node_id) {
    if (!ValidIdentifier(input.node_id)) {
      return std::nullopt;
    }
    out->node_id = std::string(input.node_id);
  }
  out->principal = mojom::PolicyPrincipal::New();
  out->principal->kind = *principal;
  if (*principal == mojom::PolicyPrincipalKind::kSkill) {
    if (!ValidIdentifier(input.principal_id)) {
      return std::nullopt;
    }
    out->principal->skill_version_id = std::string(input.principal_id);
  } else if (!input.principal_id.empty()) {
    return std::nullopt;
  }
  for (uint8_t sensitivity : input.data_classes) {
    const auto value =
        // The enum's own last member, not the last one this line was written
        // against: `kOneTimeCode` and `kChallengeResponse` were added after
        // it, so a page carrying a one-time code or a challenge — an OTP box
        // or a captcha, which is every sign-in an errand has to hand over at
        // — failed this projection instead of being described.
        ClosedEnum(sensitivity, mojom::BipSensitivity::kMaxValue);
    if (!value) {
      return std::nullopt;
    }
    out->data_classes.push_back(*value);
  }
  if (out->data_classes.empty()) {
    return std::nullopt;
  }
  out->context_risk = *risk;
  if (input.has_approval) {
    if (!ValidIdentifier(input.approval_receipt_id) ||
        !ValidIdentifier(input.approval_browser_session_id) ||
        input.approval_expires_at_monotonic_ms == 0u ||
        input.approval_expires_at_utc_ms == 0u) {
      return std::nullopt;
    }
    out->approval = mojom::PolicyApprovalFact::New();
    out->approval->receipt_reference = std::string(input.approval_receipt_id);
    out->approval->proposal_digest = std::string(input.proposal_digest);
    out->approval->service_generation = input.operation.service_generation;
    out->approval->expires_at_monotonic_ms =
        input.approval_expires_at_monotonic_ms;
    out->approval->expires_at_utc_ms = input.approval_expires_at_utc_ms;
    out->approval->browser_session_id =
        std::string(input.approval_browser_session_id);
  }
  out->control_mode = *control;
  out->policy_version = input.policy_version;
  return out;
}

}  // namespace taffy::core_service_internal
