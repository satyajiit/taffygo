// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_provider.h"

#include <stdint.h>

#include <string>
#include <utility>

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

namespace {

bridge::BridgeProviderOperation ToBridgeProviderOperation(
    const mojom::OperationEnvelope& in) {
  bridge::BridgeProviderOperation out;
  out.operation_id = in.operation_id;
  out.service_generation = in.service_generation;
  out.task_revision = in.task_revision;
  out.deadline_monotonic_ms = in.deadline_monotonic_ms;
  out.idempotency_key = in.idempotency_key;
  return out;
}

}  // namespace

std::optional<bridge::BridgeProviderCommand> ToBridgeProviderCommand(
    const mojom::CoreServiceCommand& command) {
  if (!command.operation) {
    return std::nullopt;
  }
  bridge::BridgeProviderCommand out;
  out.operation = ToBridgeProviderOperation(*command.operation);
  out.kind = static_cast<uint8_t>(command.kind);
  switch (command.kind) {
    case mojom::CoreServiceCommandKind::kSaveProviderCredential:
      if (!command.save_provider_credential) {
        return std::nullopt;
      }
      out.provider_id = command.save_provider_credential->provider_id;
      out.auth_method =
          static_cast<uint8_t>(command.save_provider_credential->auth_method);
      out.has_credential_handle = true;
      out.credential_handle =
          command.save_provider_credential->credential_handle;
      break;
    case mojom::CoreServiceCommandKind::kForgetProviderCredential:
      if (!command.forget_provider_credential) {
        return std::nullopt;
      }
      out.provider_id = command.forget_provider_credential->provider_id;
      break;
    case mojom::CoreServiceCommandKind::kSetProviderCredentialState:
      if (!command.set_provider_credential_state) {
        return std::nullopt;
      }
      out.provider_id = command.set_provider_credential_state->provider_id;
      out.credential_state =
          static_cast<uint8_t>(command.set_provider_credential_state->state);
      break;
    case mojom::CoreServiceCommandKind::kProbeProviderCredential:
      if (!command.probe_provider_credential) {
        return std::nullopt;
      }
      out.provider_id = command.probe_provider_credential->provider_id;
      out.has_credential_handle = true;
      out.credential_handle =
          command.probe_provider_credential->credential_handle;
      break;
    case mojom::CoreServiceCommandKind::kStartProviderAuth:
      if (!command.start_provider_auth) {
        return std::nullopt;
      }
      out.provider_id = command.start_provider_auth->provider_id;
      out.flow_id = command.start_provider_auth->flow_id;
      out.redirect_binding_id =
          command.start_provider_auth->redirect_binding_id;
      out.issued_at_monotonic_ms =
          command.start_provider_auth->issued_at_monotonic_ms;
      break;
    case mojom::CoreServiceCommandKind::kCancelProviderAuth:
      if (!command.cancel_provider_auth) {
        return std::nullopt;
      }
      out.flow_id = command.cancel_provider_auth->flow_id;
      break;
    case mojom::CoreServiceCommandKind::kSaveCustomProvider:
      if (!command.save_custom_provider) {
        return std::nullopt;
      }
      out.provider_id = command.save_custom_provider->provider_id;
      out.display_name = command.save_custom_provider->display_name;
      out.endpoint = command.save_custom_provider->endpoint;
      out.wire_api =
          static_cast<uint8_t>(command.save_custom_provider->wire_api);
      // Absence and emptiness are different answers, and the flag is what tells
      // them apart across a bridge with no optional of its own. An endpoint
      // that needs no key is not an endpoint whose key is the empty string.
      out.has_credential_handle =
          command.save_custom_provider->credential_handle.has_value();
      if (out.has_credential_handle) {
        out.credential_handle =
            *command.save_custom_provider->credential_handle;
      }
      // The models travel on the save rather than after it, because a provider
      // filed with nothing behind it is a row a person can select and nothing
      // can route to (decision 0096 section 4). The count is not judged here:
      // the bridge projection refuses a list over the contract's bound, so one
      // rule is written once rather than twice with a chance of disagreeing.
      for (const mojom::CustomModelSpecPtr& model :
           command.save_custom_provider->models) {
        if (!model) {
          return std::nullopt;
        }
        bridge::BridgeCustomModel projected;
        projected.model_id = model->model_id;
        projected.display_name = model->display_name;
        projected.context_window = model->context_window;
        projected.max_output_tokens = model->max_output_tokens;
        projected.reasoning = model->reasoning;
        projected.tool_calling = model->tool_calling;
        out.models.push_back(std::move(projected));
      }
      // The wrapper is the absence. Mojo has no optional enumeration, which is
      // why the contract carries a one-field record here at all, and reading
      // the record's presence rather than substituting a member keeps "nothing
      // was detected" from arriving as a claim about what the server is.
      out.has_detected_server =
          !command.save_custom_provider->detected_server.is_null();
      if (out.has_detected_server) {
        out.detected_server = static_cast<uint8_t>(
            command.save_custom_provider->detected_server->server_kind);
      }
      break;
    case mojom::CoreServiceCommandKind::kRemoveCustomProvider:
      if (!command.remove_custom_provider) {
        return std::nullopt;
      }
      out.provider_id = command.remove_custom_provider->provider_id;
      break;
    // An address a person typed, asked what it is before anything is saved
    // under it (decision 0096). It rides the provider command record because
    // every field it needs is already on it, and it claims the same single
    // flight the key probe claims — one person testing one thing on one sheet.
    case mojom::CoreServiceCommandKind::kSetProviderModelPreference:
      if (!command.set_provider_model_preference) {
        return std::nullopt;
      }
      out.provider_id = command.set_provider_model_preference->provider_id;
      // The choice as it should now stand, so an absent half is a cleared pin
      // or "Taffy decides" rather than a field left alone. Absence crosses as
      // the pair, never as a member standing for absence: `OFF` is a person
      // asking for no thinking phase and is not the same answer (decision
      // 0093 sections 1 and 3).
      out.has_model_id =
          command.set_provider_model_preference->model_id.has_value();
      if (out.has_model_id) {
        out.model_id = *command.set_provider_model_preference->model_id;
      }
      out.has_thinking_level =
          !!command.set_provider_model_preference->thinking;
      if (out.has_thinking_level) {
        out.thinking_level = static_cast<uint8_t>(
            command.set_provider_model_preference->thinking->level);
      }
      break;
    case mojom::CoreServiceCommandKind::kProbeCustomEndpoint:
      if (!command.probe_custom_endpoint) {
        return std::nullopt;
      }
      // The draft identity the surface intends to save under. Carried rather
      // than minted here, because the save that follows reuses it and a verdict
      // travels on a row keyed by a provider (decision 0096 section 5).
      out.provider_id = command.probe_custom_endpoint->provider_id;
      out.endpoint = command.probe_custom_endpoint->endpoint;
      out.wire_api =
          static_cast<uint8_t>(command.probe_custom_endpoint->wire_api);
      // Optional here and required by the key probe, because the two prove
      // different things: an address a person runs themselves may need no
      // credential at all. Absence and emptiness stay apart across a bridge
      // with no optional of its own.
      out.has_credential_handle =
          command.probe_custom_endpoint->credential_handle.has_value();
      if (out.has_credential_handle) {
        out.credential_handle =
            *command.probe_custom_endpoint->credential_handle;
      }
      break;
    // Browser-originated, exactly as `AUTH_CALLBACK` is for the account plane:
    // the sign-in engine correlated the redirect and the broker submits the
    // terminal here. Only the opaque one-shot vault handle crosses; the raw
    // authorization code never enters the core service (decision 0078).
    case mojom::CoreServiceCommandKind::kProviderAuthCallback:
      if (!command.provider_auth_callback) {
        return std::nullopt;
      }
      out.flow_id = command.provider_auth_callback->flow_id;
      out.redirect_binding_id =
          command.provider_auth_callback->redirect_binding_id;
      out.returned_state = command.provider_auth_callback->returned_state;
      out.callback_status =
          static_cast<uint8_t>(command.provider_auth_callback->status);
      out.has_authorization_code_handle =
          command.provider_auth_callback->authorization_code_handle.has_value();
      if (out.has_authorization_code_handle) {
        out.authorization_code_handle =
            *command.provider_auth_callback->authorization_code_handle;
      }
      break;
    case mojom::CoreServiceCommandKind::kStartTask:
    case mojom::CoreServiceCommandKind::kCancelTask:
    case mojom::CoreServiceCommandKind::kUserDecision:
    case mojom::CoreServiceCommandKind::kPermissionResult:
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
    case mojom::CoreServiceCommandKind::kRequestComposerCompletion:
    case mojom::CoreServiceCommandKind::kCancelComposerCompletion:
    case mojom::CoreServiceCommandKind::kPauseTask:
    case mojom::CoreServiceCommandKind::kResumeTask:
    case mojom::CoreServiceCommandKind::kTakeOver:
    case mojom::CoreServiceCommandKind::kSetAssistantConfiguration:
    case mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot:
    case mojom::CoreServiceCommandKind::kMutateSkill:
      return std::nullopt;
  }
  return out;
}

}  // namespace taffy::core_service_internal
