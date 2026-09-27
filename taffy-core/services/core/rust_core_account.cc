// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_account.h"

#include <optional>
#include <string>

#include "crypto/secure_util.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

namespace {

bridge::BridgeAccountOperation ToBridgeOperation(
    const mojom::OperationEnvelope& in) {
  bridge::BridgeAccountOperation out;
  out.operation_id = in.operation_id;
  out.service_generation = in.service_generation;
  out.task_revision = in.task_revision;
  out.deadline_monotonic_ms = in.deadline_monotonic_ms;
  out.idempotency_key = in.idempotency_key;
  return out;
}

void CopyBytes(const std::vector<uint8_t>& input, rust::Vec<uint8_t>* output) {
  output->reserve(input.size());
  for (uint8_t byte : input) {
    output->push_back(byte);
  }
}

void ProjectSession(const mojom::AccountSessionReceipt& session,
                    bridge::BridgeAccountCompletion* out) {
  out->session_handle = session.session_handle;
  out->account_subject = session.account_subject;
  out->expires_at_monotonic_ms = session.expires_at_monotonic_ms;
  out->rotation = session.rotation;
  out->auth_method = static_cast<uint8_t>(session.auth_method);
  out->has_email = session.email.has_value();
  if (session.email) {
    out->email = *session.email;
  }
  out->has_display_name = session.display_name.has_value();
  if (session.display_name) {
    out->display_name = *session.display_name;
  }
}

}  // namespace

std::optional<bridge::BridgeAccountCommand> ToBridgeAccountCommand(
    const mojom::CoreServiceCommand& command) {
  if (!command.operation) {
    return std::nullopt;
  }
  bridge::BridgeAccountCommand out;
  out.operation = ToBridgeOperation(*command.operation);
  out.kind = static_cast<uint8_t>(command.kind);
  switch (command.kind) {
    case mojom::CoreServiceCommandKind::kStartAuth:
      if (!command.start_auth) {
        return std::nullopt;
      }
      out.flow_id = command.start_auth->flow_id;
      out.auth_method = static_cast<uint8_t>(command.start_auth->method);
      out.redirect_binding_id = command.start_auth->redirect_binding_id;
      out.issued_at_monotonic_ms = command.start_auth->issued_at_monotonic_ms;
      for (mojom::AccountScope scope : command.start_auth->scopes) {
        out.scopes.push_back(static_cast<uint8_t>(scope));
      }
      break;
    case mojom::CoreServiceCommandKind::kRequestEmailLink:
      if (!command.request_email_link) {
        return std::nullopt;
      }
      out.flow_id = command.request_email_link->flow_id;
      out.email = command.request_email_link->email;
      out.redirect_binding_id = command.request_email_link->redirect_binding_id;
      out.issued_at_monotonic_ms =
          command.request_email_link->issued_at_monotonic_ms;
      for (mojom::AccountScope scope : command.request_email_link->scopes) {
        out.scopes.push_back(static_cast<uint8_t>(scope));
      }
      break;
    case mojom::CoreServiceCommandKind::kSignOut:
      if (!command.sign_out) {
        return std::nullopt;
      }
      out.has_account_subject = command.sign_out->account_subject.has_value();
      out.account_subject =
          command.sign_out->account_subject.value_or(std::string());
      break;
    case mojom::CoreServiceCommandKind::kAuthCallback:
      if (!command.auth_callback) {
        return std::nullopt;
      }
      out.flow_id = command.auth_callback->flow_id;
      out.redirect_binding_id = command.auth_callback->redirect_binding_id;
      out.returned_state = command.auth_callback->returned_state;
      out.auth_callback_status =
          static_cast<uint8_t>(command.auth_callback->status);
      out.has_authorization_code_handle =
          command.auth_callback->authorization_code_handle.has_value();
      out.authorization_code_handle =
          command.auth_callback->authorization_code_handle.value_or(
              std::string());
      break;
    case mojom::CoreServiceCommandKind::kAuthCredentialResult:
      if (!command.auth_credential_result) {
        return std::nullopt;
      }
      out.flow_id = command.auth_credential_result->flow_id;
      out.auth_method =
          static_cast<uint8_t>(command.auth_credential_result->method);
      out.auth_credential_status =
          static_cast<uint8_t>(command.auth_credential_result->status);
      out.has_credential_handle =
          command.auth_credential_result->credential_handle.has_value();
      out.credential_handle =
          command.auth_credential_result->credential_handle.value_or(
              std::string());
      break;
    case mojom::CoreServiceCommandKind::kStartTask:
    case mojom::CoreServiceCommandKind::kCancelTask:
    case mojom::CoreServiceCommandKind::kUserDecision:
    case mojom::CoreServiceCommandKind::kPermissionResult:
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
    case mojom::CoreServiceCommandKind::kCompleteHandover:
    case mojom::CoreServiceCommandKind::kExpireHandover:
    case mojom::CoreServiceCommandKind::kSupplyUserInput:
    case mojom::CoreServiceCommandKind::kFollowUp:
    case mojom::CoreServiceCommandKind::kAcceptTaskArtifact:
    case mojom::CoreServiceCommandKind::kExportTaskArtifact:
    // Kinds this projection does not carry. Listed rather than defaulted: a
    // switch with no default is how a new command kind becomes a compile error
    // in the one file that has to decide about it, instead of a silent refusal.
    case mojom::CoreServiceCommandKind::kSupplyFieldValues:
    case mojom::CoreServiceCommandKind::kSetProviderModelPreference:
    case mojom::CoreServiceCommandKind::kProbeCustomEndpoint:
    case mojom::CoreServiceCommandKind::kRequestComposerCompletion:
    case mojom::CoreServiceCommandKind::kCancelComposerCompletion:
    case mojom::CoreServiceCommandKind::kPauseTask:
    case mojom::CoreServiceCommandKind::kResumeTask:
    case mojom::CoreServiceCommandKind::kTakeOver:
    case mojom::CoreServiceCommandKind::kSetAssistantConfiguration:
    case mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot:
    case mojom::CoreServiceCommandKind::kMutateSkill:
    case mojom::CoreServiceCommandKind::kCancelProviderAuth:
      return std::nullopt;
  }
  return out;
}

std::optional<bridge::BridgeAccountCompletion> ToBridgeAccountCompletion(
    mojom::EffectResult* result) {
  if (!result || !result->operation) {
    return std::nullopt;
  }
  bridge::BridgeAccountCompletion out;
  out.operation = ToBridgeOperation(*result->operation);
  out.effect_id = result->effect_id;
  out.status = static_cast<uint8_t>(result->status);
  out.kind = static_cast<uint8_t>(result->kind);
  if (result->kind == mojom::EffectKind::kSecureStore && result->secure_store) {
    out.operation_kind =
        static_cast<uint8_t>(result->secure_store->operation_kind);
    if (result->secure_store->generated_entropy) {
      out.flow_id = result->secure_store->generated_entropy->flow_id;
      std::vector<uint8_t>& entropy =
          result->secure_store->generated_entropy->entropy;
      CopyBytes(entropy, &out.entropy);
      crypto::SecureZeroBuffer(entropy);
      entropy.clear();
    } else if (result->secure_store->transient_write) {
      out.flow_id = result->secure_store->transient_write->flow_id;
      out.purpose =
          static_cast<uint8_t>(result->secure_store->transient_write->purpose);
      out.secret_handle = result->secure_store->transient_write->secret_handle;
    } else if (result->secure_store->deleted_handle) {
      out.secret_handle = result->secure_store->deleted_handle->secret_handle;
      out.deleted = result->secure_store->deleted_handle->deleted;
    } else {
      return std::nullopt;
    }
    return out;
  }
  if (result->kind == mojom::EffectKind::kOpenAuthSurface &&
      result->auth_surface) {
    out.operation_kind =
        static_cast<uint8_t>(result->auth_surface->operation_kind);
    if (result->auth_surface->oauth) {
      out.flow_id = result->auth_surface->oauth->flow_id;
      out.opened = result->auth_surface->oauth->opened;
    } else if (result->auth_surface->native_credential) {
      out.flow_id = result->auth_surface->native_credential->flow_id;
      out.opened = result->auth_surface->native_credential->opened;
    } else {
      return std::nullopt;
    }
    return out;
  }
  if (result->kind != mojom::EffectKind::kNetworkRequest || !result->network) {
    return std::nullopt;
  }
  out.operation_kind = static_cast<uint8_t>(result->network->operation_kind);
  if (result->network->authorization_code_session) {
    ProjectSession(*result->network->authorization_code_session, &out);
  } else if (result->network->native_credential_session) {
    ProjectSession(*result->network->native_credential_session, &out);
  } else if (result->network->refreshed_session) {
    ProjectSession(*result->network->refreshed_session, &out);
  } else if (result->network->email_link) {
    out.flow_id = result->network->email_link->flow_id;
    out.accepted = result->network->email_link->accepted;
  } else if (result->network->revoked_session) {
    out.session_handle = result->network->revoked_session->session_handle;
    out.deleted = result->network->revoked_session->deleted;
  } else {
    return std::nullopt;
  }
  return out;
}

std::optional<bridge::BridgeAccountTokenValidationRequest>
ToBridgeAccountTokenValidationRequest(
    mojom::AccountTokenValidationRequest* request) {
  if (!request || !request->operation) {
    return std::nullopt;
  }
  bridge::BridgeAccountTokenValidationRequest out;
  out.operation_id = request->operation->operation_id;
  out.service_generation = request->operation->service_generation;
  out.deadline_monotonic_ms = request->operation->deadline_monotonic_ms;
  out.idempotency_key = request->operation->idempotency_key;
  out.operation_kind = static_cast<uint8_t>(request->operation_kind);
  out.expected_auth_method =
      static_cast<uint8_t>(request->expected_auth_method);
  out.has_expected_account_subject =
      request->expected_account_subject.has_value();
  out.expected_account_subject =
      request->expected_account_subject.value_or(std::string());
  out.target_rotation = request->target_rotation;
  CopyBytes(request->response_body, &out.response_body);
  crypto::SecureZeroBuffer(request->response_body);
  request->response_body.clear();
  return out;
}

}  // namespace taffy::core_service_internal
