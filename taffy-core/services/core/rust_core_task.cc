// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_task.h"

#include "taffy/services/core/rust_core_command_conversions.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;

namespace {

// The task-command plane mirrors the operation envelope under its own name,
// the same way the provider plane does: two cxx bridge modules cannot share a
// by-value struct without an include cycle between their generated headers.
core_bridge::BridgeTaskCommandOperation ToBridgeTaskCommandOperation(
    const mojom::OperationEnvelope& in) {
  core_bridge::BridgeTaskCommandOperation out;
  out.operation_id = in.operation_id;
  out.service_generation = in.service_generation;
  out.task_revision = in.task_revision;
  out.deadline_monotonic_ms = in.deadline_monotonic_ms;
  out.idempotency_key = in.idempotency_key;
  return out;
}

}  // namespace

std::optional<core_bridge::BridgeTaskCommand> ToBridgeTaskCommand(
    const mojom::CoreServiceCommand& command) {
  if (!command.operation) {
    return std::nullopt;
  }
  core_bridge::BridgeTaskCommand projected;
  projected.operation = ToBridgeTaskCommandOperation(*command.operation);
  projected.kind = static_cast<uint8_t>(command.kind);
  switch (command.kind) {
    case mojom::CoreServiceCommandKind::kCancelTask:
      if (!command.cancel_task) {
        return std::nullopt;
      }
      projected.task_id = command.cancel_task->task_id;
      projected.cancel_reason =
          static_cast<uint8_t>(command.cancel_task->reason);
      projected.trace_id = command.cancel_task->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kPauseTask:
      if (!command.pause_task) {
        return std::nullopt;
      }
      projected.task_id = command.pause_task->task_id;
      projected.trace_id = command.pause_task->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kResumeTask:
      if (!command.resume_task) {
        return std::nullopt;
      }
      projected.task_id = command.resume_task->task_id;
      projected.trace_id = command.resume_task->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kTakeOver:
      if (!command.take_over) {
        return std::nullopt;
      }
      projected.task_id = command.take_over->task_id;
      projected.trace_id = command.take_over->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kUserDecision:
      if (!command.user_decision) {
        return std::nullopt;
      }
      projected.task_id = command.user_decision->task_id;
      projected.action_id = command.user_decision->action_id;
      projected.user_decision =
          static_cast<uint8_t>(command.user_decision->decision);
      projected.approval_digest = command.user_decision->approval_digest;
      projected.trace_id = command.user_decision->trace_id;
      projected.approval_receipt_id =
          command.user_decision->approval_receipt_id;
      projected.approval_expires_at_monotonic_ms =
          command.user_decision->approval_expires_at_monotonic_ms;
      projected.approval_expires_at_utc_ms =
          command.user_decision->approval_expires_at_utc_ms;
      projected.browser_session_id = command.user_decision->browser_session_id;
      break;
    case mojom::CoreServiceCommandKind::kPermissionResult:
      if (!command.permission_result) {
        return std::nullopt;
      }
      projected.task_id = command.permission_result->task_id;
      projected.request_id = command.permission_result->request_id;
      projected.permission =
          static_cast<uint8_t>(command.permission_result->permission);
      projected.permission_decision =
          static_cast<uint8_t>(command.permission_result->decision);
      projected.trace_id = command.permission_result->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kCompleteHandover:
      if (!command.complete_handover) {
        return std::nullopt;
      }
      projected.task_id = command.complete_handover->task_id;
      projected.handover_id = command.complete_handover->handover_id;
      projected.lease_before = command.complete_handover->lease_before;
      projected.resumed_with = command.complete_handover->resumed_with;
      projected.person_input = command.complete_handover->person_input;
      projected.trace_id = command.complete_handover->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kExpireHandover:
      if (!command.expire_handover) {
        return std::nullopt;
      }
      projected.task_id = command.expire_handover->task_id;
      projected.handover_id = command.expire_handover->handover_id;
      projected.trace_id = command.expire_handover->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kSupplyUserInput:
      if (!command.supply_user_input) {
        return std::nullopt;
      }
      projected.task_id = command.supply_user_input->task_id;
      projected.answer = command.supply_user_input->answer;
      projected.trace_id = command.supply_user_input->trace_id;
      break;
    // The question rides the answer field: it is bounded and classified the
    // same way on the other side, and the bridge tells the two apart by kind.
    case mojom::CoreServiceCommandKind::kFollowUp:
      if (!command.follow_up) {
        return std::nullopt;
      }
      projected.task_id = command.follow_up->task_id;
      projected.answer = command.follow_up->question;
      projected.trace_id = command.follow_up->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kSupplyFieldValues:
      if (!command.supply_field_values) {
        return std::nullopt;
      }
      projected.task_id = command.supply_field_values->task_id;
      projected.request_id = command.supply_field_values->request_id;
      // The count and nothing else. What a person typed stays in the browser's
      // vault and is spent there (decision 0063); the core is told how many
      // references were filled so the errand can go on, and a count past the
      // bound is refused on the far side rather than clamped here.
      projected.supplied = command.supply_field_values->supplied;
      // Beside the count, never instead of it. A zero count is six different
      // facts on the browser's side and each implies a different next move,
      // so the member travels and the far side refuses a number it does not
      // recognise rather than defaulting to "the person answered"
      // (decision 0215).
      projected.supplied_outcome = static_cast<uint8_t>(
          command.supply_field_values->outcome);
      // Which field each held value was minted for, in position order, so the
      // task can put the person's values into those fields itself rather than
      // waiting for a model turn to name them (decision 0238). Identities the
      // core already holds, never a value; the far side refuses a list whose
      // length is not the count.
      for (const std::string& field :
           command.supply_field_values->field_node_ids) {
        projected.supplied_field_node_ids.push_back(rust::String(field));
      }
      projected.trace_id = command.supply_field_values->trace_id;
      break;
    case mojom::CoreServiceCommandKind::kAcceptTaskArtifact:
      if (!command.accept_task_artifact) {
        return std::nullopt;
      }
      projected.task_id = command.accept_task_artifact->task_id;
      projected.artifact_id = command.accept_task_artifact->artifact_id;
      projected.trace_id = command.operation->operation_id;
      break;
    case mojom::CoreServiceCommandKind::kExportTaskArtifact:
      if (!command.export_task_artifact) {
        return std::nullopt;
      }
      projected.task_id = command.export_task_artifact->task_id;
      projected.artifact_id = command.export_task_artifact->artifact_id;
      projected.artifact_kind =
          static_cast<uint8_t>(command.export_task_artifact->kind);
      projected.trace_id = command.operation->operation_id;
      break;
    case mojom::CoreServiceCommandKind::kStartTask:
    case mojom::CoreServiceCommandKind::kAuthCallback:
    case mojom::CoreServiceCommandKind::kStartAuth:
    case mojom::CoreServiceCommandKind::kRequestEmailLink:
    case mojom::CoreServiceCommandKind::kSignOut:
    case mojom::CoreServiceCommandKind::kAuthCredentialResult:
    case mojom::CoreServiceCommandKind::kCorrectWorkspaceFact:
    case mojom::CoreServiceCommandKind::kExcludeWorkspaceSource:
    case mojom::CoreServiceCommandKind::kRequestWorkspaceExport:
    case mojom::CoreServiceCommandKind::kSaveWorkspace:
    case mojom::CoreServiceCommandKind::kRenameWorkspace:
    case mojom::CoreServiceCommandKind::kDeleteWorkspace:
    case mojom::CoreServiceCommandKind::kDiscardWorkspace:
    case mojom::CoreServiceCommandKind::kSearchLibrary:
    case mojom::CoreServiceCommandKind::kSaveLibraryFact:
    case mojom::CoreServiceCommandKind::kRemoveLibraryEntry:
    case mojom::CoreServiceCommandKind::kRequestLibraryExport:
    case mojom::CoreServiceCommandKind::kSearchMemory:
    case mojom::CoreServiceCommandKind::kUpsertMemory:
    case mojom::CoreServiceCommandKind::kDeleteMemory:
    case mojom::CoreServiceCommandKind::kSetAssetDeliveryPolicy:
    case mojom::CoreServiceCommandKind::kRequestAsset:
    case mojom::CoreServiceCommandKind::kRemoveAsset:
    case mojom::CoreServiceCommandKind::kSaveProviderCredential:
    case mojom::CoreServiceCommandKind::kSetProviderCredentialState:
    case mojom::CoreServiceCommandKind::kProbeProviderCredential:
    case mojom::CoreServiceCommandKind::kForgetProviderCredential:
    case mojom::CoreServiceCommandKind::kStartProviderAuth:
    case mojom::CoreServiceCommandKind::kProviderAuthCallback:
    case mojom::CoreServiceCommandKind::kSaveCustomProvider:
    case mojom::CoreServiceCommandKind::kRemoveCustomProvider:
    // Kinds this projection does not carry. Listed rather than defaulted: a
    // switch with no default is how a new command kind becomes a compile error
    // in the one file that has to decide about it, instead of a silent refusal.
    case mojom::CoreServiceCommandKind::kSetProviderModelPreference:
    case mojom::CoreServiceCommandKind::kProbeCustomEndpoint:
    case mojom::CoreServiceCommandKind::kRequestComposerCompletion:
    case mojom::CoreServiceCommandKind::kCancelComposerCompletion:
    case mojom::CoreServiceCommandKind::kSetAssistantConfiguration:
    case mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot:
    case mojom::CoreServiceCommandKind::kMutateSkill:
    case mojom::CoreServiceCommandKind::kCancelProviderAuth:
      return std::nullopt;
  }
  return projected;
}

core_bridge::BridgeTaskSettlement ToBridgeTaskSettlement(
    const mojom::TaskSettlementBinding& settlement) {
  core_bridge::BridgeTaskSettlement projected;
  projected.task_id = settlement.task_id;
  projected.service_generation = settlement.service_generation;
  projected.task_revision = settlement.task_revision;
  projected.kind = static_cast<uint8_t>(settlement.kind);
  return projected;
}

}  // namespace taffy::core_service_internal
