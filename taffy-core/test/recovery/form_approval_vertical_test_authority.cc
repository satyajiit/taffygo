// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/form_approval_vertical_test_internal.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace taffy::test {
namespace {

constexpr uint64_t kOperationLifetimeMs = 30'000u;
constexpr char kPolicyIdempotency[] = "form-fill-idempotency";

bool Contains(std::string_view candidate, std::string_view value) {
  return candidate.find(value) != std::string_view::npos;
}

service::OperationEnvelopePtr Operation(std::string operation_id,
                                        uint64_t generation,
                                        uint64_t task_revision,
                                        uint64_t deadline,
                                        std::string idempotency_key) {
  return service::OperationEnvelope::New(
      std::move(operation_id), generation, task_revision, deadline,
      std::move(idempotency_key));
}

service::CoreStateBrowserBindingsPtr BaseTaskBindings(
    uint64_t generation,
    uint64_t sequence,
    const std::string& task_id,
    uint64_t task_revision,
    const service::StartTaskCommand& start) {
  auto bindings = service::CoreStateBrowserBindings::New();
  bindings->service_generation = generation;
  bindings->state_sequence = sequence;
  bindings->task_revisions.push_back(service::TaskRevisionBinding::New(
      task_id, generation, task_revision,
      std::vector<service::TaskControlKind>()));

  auto consent = service::AcceptedTaskConsentBinding::New();
  consent->task_id = task_id;
  consent->service_generation = generation;
  consent->current_task_revision = task_revision;
  consent->accepted_revision = 1u;
  consent->browser_session_id = start.browser_session_id;
  consent->receipt_id = start.initial_consent_receipt_id;
  consent->consent_preview = start.consent_preview.Clone();
  bindings->accepted_task_consents.push_back(std::move(consent));
  return bindings;
}

}  // namespace

service::CoreStateBrowserBindingsPtr MakeEmptyFormBindings(
    uint64_t generation,
    uint64_t sequence) {
  auto bindings = service::CoreStateBrowserBindings::New();
  bindings->service_generation = generation;
  bindings->state_sequence = sequence;
  return bindings;
}

service::CoreStateBrowserBindingsPtr MakeTaskFormBindings(
    uint64_t generation,
    uint64_t sequence,
    const std::string& task_id,
    uint64_t task_revision,
    const service::StartTaskCommand& start,
    const std::optional<std::string>& pending_action_id,
    const std::optional<std::string>& pending_proposal_digest,
    const service::UserDecisionCommand* committed_decision) {
  auto bindings = BaseTaskBindings(generation, sequence, task_id,
                                   task_revision, start);
  if (pending_action_id && pending_proposal_digest) {
    bindings->pending_approvals.push_back(
        service::PendingApprovalBinding::New(
            task_id, *pending_action_id, *pending_proposal_digest, generation,
            task_revision));
  }
  if (committed_decision) {
    bindings->committed_action_approvals.push_back(
        service::CommittedActionApprovalBinding::New(
            task_id, committed_decision->action_id, generation, task_revision,
            committed_decision->approval_receipt_id,
            committed_decision->approval_digest,
            committed_decision->approval_expires_at_monotonic_ms,
            committed_decision->approval_expires_at_utc_ms,
            committed_decision->browser_session_id));
  }
  return bindings;
}

service::TaskEffectBindingPtr MakeFieldValueRequestEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& request_id,
    const LiveFormApprovalTarget& target,
    uint64_t now_monotonic_ms) {
  auto effect = service::TaskEffectBinding::New();
  effect->operation = Operation("form-values-operation", generation,
                                task_revision,
                                now_monotonic_ms + kOperationLifetimeMs,
                                "form-values-idempotency");
  effect->effect_id = "form-values-effect";
  effect->task_id = task_id;
  effect->ordinal = 0u;
  effect->kind = service::TaskReducerEffectKind::kRequestFieldValues;
  effect->field_values = service::TaskFieldValuesEffect::New(
      request_id, target.tab_id, target.form_node_id,
      std::vector<std::string>());
  return effect;
}

service::TaskEffectBindingPtr MakeFormApprovalEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& action_id,
    const std::string& proposal_digest,
    service::TaskExecutableActionPtr executable,
    uint64_t now_monotonic_ms) {
  auto effect = service::TaskEffectBinding::New();
  effect->operation = Operation("form-approval-operation", generation,
                                task_revision,
                                now_monotonic_ms + kOperationLifetimeMs,
                                "form-approval-idempotency");
  effect->effect_id = "form-approval-effect";
  effect->task_id = task_id;
  effect->ordinal = 1u;
  effect->kind = service::TaskReducerEffectKind::kRequestApproval;
  effect->approval = service::TaskApprovalEffect::New(
      action_id, proposal_digest, std::move(executable));
  return effect;
}

service::TaskPolicyEffectPtr MakeFormPolicyEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& action_id,
    const std::string& proposal_digest,
    const std::string& request_id,
    uint32_t supplied_value_index,
    const LiveFormApprovalTarget& target,
    const service::UserDecisionCommand& decision,
    uint64_t now_monotonic_ms) {
  auto effect = service::TaskPolicyEffect::New();
  const uint64_t deadline =
      std::min(now_monotonic_ms + kOperationLifetimeMs,
               decision.approval_expires_at_monotonic_ms);
  effect->operation = Operation("form-policy-operation", generation,
                                task_revision, deadline, kPolicyIdempotency);
  effect->effect_id = "form-policy-effect";
  effect->task_id = task_id;
  effect->action_id = action_id;
  effect->action_class = service::PolicyActionClass::kFillField;
  effect->proposal_digest = proposal_digest;
  effect->idempotency_key = kPolicyIdempotency;
  effect->tab_id = target.tab_id;
  effect->node_id = target.field_node_id;
  effect->principal = service::PolicyPrincipal::New(
      service::PolicyPrincipalKind::kAssistant, std::nullopt);
  effect->data_classes = {service::BipSensitivity::kNotSensitive};
  effect->context_risk = service::PolicyRiskClass::kSensitiveDisclosure;
  effect->approval = service::PolicyApprovalFact::New(
      decision.approval_receipt_id, proposal_digest, generation,
      decision.approval_expires_at_monotonic_ms,
      decision.approval_expires_at_utc_ms, decision.browser_session_id);
  effect->control_mode = service::TaskControlMode::kAssistant;
  effect->policy_version = 1u;
  effect->operation_kind = service::TaskActionOperationKind::kFormFill;
  effect->tool_name = "browser.form.fill";
  auto executable = MakeFillExecutable(target, request_id,
                                       supplied_value_index);
  effect->canonical_intent = executable->canonical_intent;
  effect->input = std::move(executable->input);
  return effect;
}

service::TaskEffectBindingPtr MakeFormDispatchEffect(
    uint64_t generation,
    uint64_t task_revision,
    const std::string& task_id,
    const std::string& action_id,
    const std::string& proposal_digest,
    const std::string& request_id,
    uint32_t supplied_value_index,
    const LiveFormApprovalTarget& target,
    const service::MintedCapabilityGrant& grant) {
  auto effect = service::TaskEffectBinding::New();
  effect->operation = Operation("form-dispatch-operation", generation,
                                task_revision,
                                grant.expires_at_monotonic_ms,
                                grant.idempotency_key);
  effect->effect_id = "form-dispatch-effect";
  effect->task_id = task_id;
  effect->ordinal = 2u;
  effect->kind = service::TaskReducerEffectKind::kDispatchAction;

  auto action = service::TaskActionEffect::New();
  action->action_id = action_id;
  action->proposal_digest = proposal_digest;
  action->idempotency_key = grant.idempotency_key;
  action->capability_id = grant.capability_id;
  action->dispatch_id = "form-dispatch-1";
  action->document = service::TaskFrozenDocument::New(
      target.frame_id, target.page_epoch, target.graph_revision,
      target.normalized_origin, std::nullopt);
  action->executable =
      MakeFillExecutable(target, request_id, supplied_value_index);
  action->preconditions = {
      service::TaskActionPrecondition::kDocumentUnchanged,
      service::TaskActionPrecondition::kGraphRevisionAtLeast,
      service::TaskActionPrecondition::kNodePresent,
  };
  action->postcondition = service::TaskActionPostcondition::kNodeStateChanged;
  effect->action = std::move(action);
  return effect;
}

bool FormCommandContains(const service::CoreServiceCommand& command,
                         std::string_view value) {
  if (value.empty()) {
    return false;
  }
  if (command.operation &&
      (Contains(command.operation->operation_id, value) ||
       Contains(command.operation->idempotency_key, value))) {
    return true;
  }
  if (command.start_task) {
    const service::StartTaskCommand& start = *command.start_task;
    if (Contains(start.task_id, value) || Contains(start.goal, value) ||
        Contains(start.browser_profile_id, value) ||
        Contains(start.trace_id, value) ||
        Contains(start.initial_consent_receipt_id, value) ||
        Contains(start.browser_session_id, value)) {
      return true;
    }
    for (const std::string& tool : start.tool_allowlist) {
      if (Contains(tool, value)) {
        return true;
      }
    }
    if (start.consent_preview) {
      for (const auto& source : start.consent_preview->sources) {
        if (source &&
            (Contains(source->source_id, value) ||
             Contains(source->tab_id, value) ||
             Contains(source->normalized_origin, value))) {
          return true;
        }
      }
    }
  }
  if (command.supply_field_values &&
      (Contains(command.supply_field_values->task_id, value) ||
       Contains(command.supply_field_values->request_id, value) ||
       Contains(command.supply_field_values->trace_id, value))) {
    return true;
  }
  return command.user_decision &&
         (Contains(command.user_decision->task_id, value) ||
          Contains(command.user_decision->action_id, value) ||
          Contains(command.user_decision->approval_digest, value) ||
          Contains(command.user_decision->trace_id, value) ||
          Contains(command.user_decision->approval_receipt_id, value) ||
          Contains(command.user_decision->browser_session_id, value));
}

}  // namespace taffy::test
