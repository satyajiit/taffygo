// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_command_validation.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "taffy/browser/core_service_command_validation_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool AddScopes(const std::vector<mojom::AccountScope>& scopes,
               size_t ceiling,
               size_t* total) {
  if (scopes.empty() || scopes.size() > mojom::kMaxAccountScopes ||
      !AddBounded(scopes.size(), ceiling, total)) {
    return false;
  }
  bool open_id = false;
  bool email = false;
  bool profile = false;
  for (mojom::AccountScope scope : scopes) {
    bool* seen = nullptr;
    switch (scope) {
      case mojom::AccountScope::kOpenId:
        seen = &open_id;
        break;
      case mojom::AccountScope::kEmail:
        seen = &email;
        break;
      case mojom::AccountScope::kProfile:
        seen = &profile;
        break;
    }
    if (!seen || *seen) {
      return false;
    }
    *seen = true;
  }
  return true;
}

std::optional<size_t> CommandByteSize(const mojom::CoreServiceCommand& command,
                                      size_t ceiling,
                                      const char** refusal) {
  if (command.start_task) {
    return StartTaskByteSize(*command.start_task, ceiling, refusal);
  }
  size_t total = 0;
  if (command.cancel_task) {
    return AddIdentifier(command.cancel_task->task_id, ceiling, &total) &&
                   AddIdentifier(command.cancel_task->trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.pause_task) {
    return AddIdentifier(command.pause_task->task_id, ceiling, &total) &&
                   AddIdentifier(command.pause_task->trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.resume_task) {
    return AddIdentifier(command.resume_task->task_id, ceiling, &total) &&
                   AddIdentifier(command.resume_task->trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.take_over) {
    return AddIdentifier(command.take_over->task_id, ceiling, &total) &&
                   AddIdentifier(command.take_over->trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.user_decision) {
    return AddIdentifier(command.user_decision->task_id, ceiling, &total) &&
                   AddIdentifier(command.user_decision->action_id, ceiling,
                                 &total) &&
                   AddSha256Digest(command.user_decision->approval_digest,
                                   ceiling, &total) &&
                   AddIdentifier(command.user_decision->trace_id, ceiling,
                                 &total) &&
                   AddIdentifier(command.user_decision->approval_receipt_id,
                                 ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.auth_callback) {
    const auto& auth = *command.auth_callback;
    const bool has_code = auth.authorization_code_handle.has_value();
    const bool expects_code =
        auth.status == mojom::AuthCallbackStatus::kAuthorizationCode;
    return has_code == expects_code &&
                   AddIdentifier(auth.flow_id, ceiling, &total) &&
                   AddIdentifier(auth.redirect_binding_id, ceiling, &total) &&
                   AddIdentifier(auth.returned_state, ceiling, &total) &&
                   AddOptionalIdentifier(auth.authorization_code_handle,
                                         ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.permission_result) {
    return AddIdentifier(command.permission_result->task_id, ceiling, &total) &&
                   AddIdentifier(command.permission_result->request_id, ceiling,
                                 &total) &&
                   AddIdentifier(command.permission_result->trace_id, ceiling,
                                 &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.start_auth) {
    const auto& auth = *command.start_auth;
    const bool valid_method =
        auth.method == mojom::AccountAuthMethod::kGoogle
            ? auth.scopes.empty()
            : (auth.method == mojom::AccountAuthMethod::kGithub ||
               auth.method == mojom::AccountAuthMethod::kFacebook) &&
                  AddScopes(auth.scopes, ceiling, &total);
    return AddIdentifier(auth.flow_id, ceiling, &total) &&
                   AddIdentifier(auth.redirect_binding_id, ceiling, &total) &&
                   valid_method
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.request_email_link) {
    const auto& auth = *command.request_email_link;
    return AddIdentifier(auth.flow_id, ceiling, &total) &&
                   !auth.email.empty() &&
                   auth.email.size() <= mojom::kMaxAccountEmailBytes &&
                   AddBounded(auth.email.size(), ceiling, &total) &&
                   AddIdentifier(auth.redirect_binding_id, ceiling, &total) &&
                   AddScopes(auth.scopes, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.sign_out) {
    return AddOptionalIdentifier(command.sign_out->account_subject, ceiling,
                                 &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.auth_credential_result) {
    const auto& credential = *command.auth_credential_result;
    const bool has_handle = credential.credential_handle.has_value();
    const bool expects_handle =
        credential.status == mojom::AuthCredentialStatus::kSuccess;
    return credential.method == mojom::AccountAuthMethod::kGoogle &&
                   has_handle == expects_handle &&
                   AddIdentifier(credential.flow_id, ceiling, &total) &&
                   AddOptionalIdentifier(credential.credential_handle, ceiling,
                                         &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.correct_workspace_fact || command.exclude_workspace_source ||
      command.request_workspace_export || command.save_workspace ||
      command.rename_workspace || command.delete_workspace ||
      command.discard_workspace) {
    return WorkspaceCommandByteSize(command, ceiling);
  }
  if (command.search_library) {
    const auto& library = *command.search_library;
    const bool has_non_whitespace = std::any_of(
        library.query.begin(), library.query.end(), [](char character) {
          return !std::isspace(static_cast<unsigned char>(character));
        });
    const bool valid_control_bytes = std::none_of(
        library.query.begin(), library.query.end(), [](char character) {
          const auto byte = static_cast<unsigned char>(character);
          return std::iscntrl(byte) && !std::isspace(byte);
        });
    return AddIdentifier(library.request_id, ceiling, &total) &&
                   !library.query.empty() &&
                   library.query.size() <= mojom::kMaxLibraryQueryBytes &&
                   has_non_whitespace && valid_control_bytes &&
                   library.limit != 0u &&
                   library.limit <= mojom::kMaxLibrarySearchResults &&
                   AddBounded(library.query.size(), ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.save_library_fact) {
    const auto& library = *command.save_library_fact;
    return AddIdentifier(library.workspace_id, ceiling, &total) &&
                   AddIdentifier(library.fact_id, ceiling, &total) &&
                   library.expected_workspace_revision != 0u
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.remove_library_entry) {
    const auto& library = *command.remove_library_entry;
    return AddIdentifier(library.entry_id, ceiling, &total) &&
                   library.expected_entry_revision != 0u
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.request_library_export) {
    const auto& library = *command.request_library_export;
    return AddIdentifier(library.request_id, ceiling, &total) &&
                   AddOptionalIdentifier(library.collection_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.search_memory || command.upsert_memory || command.delete_memory) {
    return MemoryCommandByteSize(command, ceiling);
  }
  if (command.set_assistant_configuration ||
      command.replace_saved_data_snapshot) {
    return ProfileCommandByteSize(command, ceiling);
  }
  if (command.set_asset_delivery_policy) {
    // Two enumerations and a flag; the closed-enum decoder has already refused
    // an unknown network cost, and there is nothing here to bound.
    return std::optional<size_t>(total);
  }
  if (command.request_asset) {
    return AddIdentifier(command.request_asset->asset_id, ceiling, &total) &&
                   AddIdentifier(command.request_asset->asset_revision, ceiling,
                                 &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.remove_asset) {
    return AddIdentifier(command.remove_asset->asset_id, ceiling, &total) &&
                   AddIdentifier(command.remove_asset->asset_revision, ceiling,
                                 &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.complete_handover) {
    const auto& handover = *command.complete_handover;
    return AddIdentifier(handover.task_id, ceiling, &total) &&
                   AddIdentifier(handover.handover_id, ceiling, &total) &&
                   AddIdentifier(handover.lease_before, ceiling, &total) &&
                   AddIdentifier(handover.resumed_with, ceiling, &total) &&
                   AddIdentifier(handover.trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.expire_handover) {
    const auto& handover = *command.expire_handover;
    return AddIdentifier(handover.task_id, ceiling, &total) &&
                   AddIdentifier(handover.handover_id, ceiling, &total) &&
                   AddIdentifier(handover.trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.supply_user_input) {
    const auto& input = *command.supply_user_input;
    return AddIdentifier(input.task_id, ceiling, &total) &&
                   !input.answer.empty() &&
                   input.answer.size() <= mojom::kMaxUserInputAnswerBytes &&
                   AddBounded(input.answer.size(), ceiling, &total) &&
                   AddIdentifier(input.trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.follow_up) {
    const auto& input = *command.follow_up;
    return AddIdentifier(input.task_id, ceiling, &total) &&
                   !input.question.empty() &&
                   input.question.size() <= mojom::kMaxUserInputAnswerBytes &&
                   AddBounded(input.question.size(), ceiling, &total) &&
                   AddIdentifier(input.trace_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.accept_task_artifact) {
    return AddIdentifier(command.accept_task_artifact->task_id, ceiling,
                         &total) &&
                   AddIdentifier(command.accept_task_artifact->artifact_id,
                                 ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.export_task_artifact) {
    return AddIdentifier(command.export_task_artifact->task_id, ceiling,
                         &total) &&
                   AddIdentifier(command.export_task_artifact->artifact_id,
                                 ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.supply_field_values) {
    const auto& supplied = *command.supply_field_values;
    // Two identities, a trace, a count and one field identity per value.
    // There is deliberately nothing to measure beyond the identities: the
    // body carries no value, no length, no
    // mask and no digest, because a value that goes into a form field comes
    // from a domain small enough that any function of it is the value
    // (decision 0063 section 4). Zero is admitted — a person who opened the
    // sheet and filled nothing in has answered, and the task needs to learn
    // that rather than wait on a surface that has already closed.
    if (supplied.supplied > kMaxSuppliedFieldValues ||
        supplied.field_node_ids.size() != supplied.supplied ||
        !AddIdentifier(supplied.task_id, ceiling, &total) ||
        !AddIdentifier(supplied.request_id, ceiling, &total) ||
        !AddIdentifier(supplied.trace_id, ceiling, &total)) {
      return std::nullopt;
    }
    // The field each held value was minted for, one per value and in
    // position order (decision 0238). Identities from the observation the
    // core already holds, never anything read out of a value; a repeat would
    // put two of the person's answers into one field.
    for (size_t index = 0; index < supplied.field_node_ids.size(); ++index) {
      const std::string& field = supplied.field_node_ids[index];
      if (!AddIdentifier(field, ceiling, &total) ||
          std::find(supplied.field_node_ids.begin(),
                    supplied.field_node_ids.begin() + index,
                    field) != supplied.field_node_ids.begin() + index) {
        return std::nullopt;
      }
    }
    return std::optional<size_t>(total);
  }
  if (command.request_composer_completion) {
    const auto& composing = *command.request_composer_completion;
    // An empty prefix is refused: there is nothing to continue, and a request
    // that asked a model to continue nothing would spend a turn to be told so.
    // An absent suffix is a caret at the end of what has been typed, which is
    // the ordinary case and not the same as an empty one.
    return AddIdentifier(composing.request_id, ceiling, &total) &&
                   !composing.prefix.empty() &&
                   composing.prefix.size() <= mojom::kMaxComposerPrefixBytes &&
                   AddBounded(composing.prefix.size(), ceiling, &total) &&
                   (!composing.suffix ||
                    (composing.suffix->size() <=
                         mojom::kMaxComposerSuffixBytes &&
                     AddBounded(composing.suffix->size(), ceiling, &total)))
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.cancel_composer_completion) {
    // The withdrawal of decision 0097 section 3: one identity and nothing
    // beside it. It names the request to stop rather than describing it, so
    // there is no prefix, no suffix and no text on this body to measure — and
    // a body that carried any would be a second copy of what the request
    // already sent.
    return AddIdentifier(command.cancel_composer_completion->request_id,
                         ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.mutate_skill) {
    return SkillCommandByteSize(command, ceiling);
  }
  // Provider bodies share a tighter identity bound; they live in
  // core_service_command_validation_provider.cc.
  return ProviderCommandByteSize(command, ceiling);
}

}  // namespace

bool IsStructurallyValidCoreServiceCommand(
    const mojom::CoreServiceCommand& command,
    size_t maximum_bytes) {
  return CoreServiceCommandStructuralRefusal(command, maximum_bytes) == nullptr;
}

const char* CoreServiceCommandStructuralRefusal(
    const mojom::CoreServiceCommand& command,
    size_t maximum_bytes) {
  if (!command.operation) {
    return "operation-missing";
  }
  if (command.operation->operation_id.empty()) {
    return "operation-id-empty";
  }
  if (command.operation->operation_id.size() > mojom::kMaxOperationIdBytes) {
    return "operation-id-too-long";
  }
  if (command.operation->idempotency_key.empty()) {
    return "idempotency-key-empty";
  }
  if (command.operation->idempotency_key.size() >
      mojom::kMaxIdempotencyKeyBytes) {
    return "idempotency-key-too-long";
  }
  if (command.user_decision &&
      (command.user_decision->approval_expires_at_monotonic_ms == 0u ||
       command.user_decision->approval_expires_at_monotonic_ms >
           command.operation->deadline_monotonic_ms)) {
    return "approval-expiry";
  }
  if (!HasMatchingCommandBody(command)) {
    return "command-body";
  }
  // Only the start body names its own clauses so far; every other body still
  // answers with one label, which is the honest thing to print until it does.
  const char* body = "byte-size";
  if (!CommandByteSize(command, maximum_bytes, &body).has_value()) {
    return body;
  }
  return nullptr;
}

}  // namespace taffy
