// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The outbound half of the account seam: what the sandboxed Rust core produced,
// projected into the Core Service Mojo types the browser acts on. It is its own
// file because it is the direction that carries authority — the inbound half in
// rust_core_account.cc narrows a browser-owned command down to bytes, while
// everything here becomes an effect the browser will execute.
//
// Every discriminant arrives as a `uint8_t`. Casting one straight into a closed
// Mojo enumeration is undefined behaviour for a value that names no member, and
// it fails open: the effect would leave here naming an auth method or a
// secure-store operation the browser cannot match, and every switch below would
// miss it silently. The generated decoders refuse instead, and a refusal takes
// the null return this seam already has — `ToResponseBatch` turns it into
// kInvalidCommand, and `RustCore::ValidateAccountTokenResponse` already returns
// null for a request it cannot project.

#include <optional>
#include <string>

#include "crypto/secure_util.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/services/core/rust_core_account.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;
namespace wire = core_service::wire;

namespace {

mojom::OperationEnvelopePtr ToMojoOperation(const bridge::BridgeOperation &in) {
  auto out = mojom::OperationEnvelope::New();
  out->operation_id = std::string(in.operation_id);
  out->service_generation = in.service_generation;
  out->task_revision = in.task_revision;
  out->deadline_monotonic_ms = in.deadline_monotonic_ms;
  out->idempotency_key = std::string(in.idempotency_key);
  return out;
}

} // namespace

mojom::AccountTokenValidationResultPtr ToMojoAccountTokenValidationResult(
    bridge::BridgeAccountTokenValidationResult result) {
  const std::optional<mojom::AccountTokenValidationStatus> status =
      wire::AccountTokenValidationStatusFromWire(result.status);
  const std::optional<mojom::AccountNetworkOperation> operation_kind =
      wire::AccountNetworkOperationFromWire(result.operation_kind);
  const std::optional<mojom::AccountAuthMethod> auth_method =
      wire::AccountAuthMethodFromWire(result.auth_method);
  // Absence and emptiness are different answers, and the flag is what tells
  // them apart across a bridge with no optional of its own. A flag set over an
  // empty string is neither: the contract says absent when the provider
  // confirmed none, and an empty string would satisfy the type while meaning
  // nothing. It is tested here, beside the enum projections, so that every
  // refusal in this function leaves through the one path that zeroes the token
  // buffers first — a refusal added below the copy would return with the raw
  // material still resident.
  const bool identity_well_formed =
      (!result.has_email || !result.email.empty()) &&
      (!result.has_display_name || !result.display_name.empty());
  if (!status || !operation_kind || !auth_method || !identity_well_formed) {
    crypto::SecureZeroBuffer(base::span(result.access_token));
    crypto::SecureZeroBuffer(base::span(result.refresh_token));
    result.access_token.clear();
    result.refresh_token.clear();
    return nullptr;
  }
  auto out = mojom::AccountTokenValidationResult::New();
  out->status = *status;
  out->operation_id = std::string(result.operation_id);
  out->operation_kind = *operation_kind;
  out->auth_method = *auth_method;
  out->account_subject = std::string(result.account_subject);
  out->expires_in_seconds = result.expires_in_seconds;
  out->target_rotation = result.target_rotation;
  out->access_token.assign(result.access_token.begin(),
                           result.access_token.end());
  out->refresh_token.assign(result.refresh_token.begin(),
                            result.refresh_token.end());
  if (result.has_email) {
    out->email = std::string(result.email);
  }
  if (result.has_display_name) {
    out->display_name = std::string(result.display_name);
  }
  crypto::SecureZeroBuffer(base::span(result.access_token));
  crypto::SecureZeroBuffer(base::span(result.refresh_token));
  result.access_token.clear();
  result.refresh_token.clear();
  return out;
}

mojom::EffectEnvelopePtr
ToMojoNetworkEffect(const bridge::BridgeAccountEffect &in,
                    mojom::EffectEnvelopePtr out);

mojom::EffectEnvelopePtr
ToMojoAccountEffect(const bridge::BridgeAccountEffect &in) {
  const std::optional<mojom::EffectKind> kind =
      wire::EffectKindFromWire(in.kind);
  const std::optional<mojom::RetryClass> retry_class =
      wire::RetryClassFromWire(in.retry_class);
  if (!kind || !retry_class) {
    return nullptr;
  }
  auto out = mojom::EffectEnvelope::New();
  out->operation = ToMojoOperation(in.operation);
  out->effect_id = std::string(in.effect_id);
  out->kind = *kind;
  out->retry_class = *retry_class;
  if (out->kind == mojom::EffectKind::kSecureStore) {
    const std::optional<mojom::SecureStoreOperation> secure_store_operation =
        wire::SecureStoreOperationFromWire(in.operation_kind);
    if (!secure_store_operation) {
      return nullptr;
    }
    out->secure_store = mojom::SecureStoreEffect::New();
    out->secure_store->operation_kind = *secure_store_operation;
    if (out->secure_store->operation_kind ==
        mojom::SecureStoreOperation::kGenerateEntropy) {
      out->secure_store->generate_entropy =
          mojom::GenerateEntropyRequest::New();
      out->secure_store->generate_entropy->flow_id = std::string(in.flow_id);
      out->secure_store->generate_entropy->byte_count = in.byte_count;
    } else if (out->secure_store->operation_kind ==
               mojom::SecureStoreOperation::kWriteTransient) {
      out->secure_store->write_transient =
          mojom::WriteTransientSecretRequest::New();
      out->secure_store->write_transient->flow_id = std::string(in.flow_id);
      const std::optional<mojom::SecretMaterialPurpose> purpose =
          wire::SecretMaterialPurposeFromWire(in.purpose);
      if (!purpose) {
        return nullptr;
      }
      out->secure_store->write_transient->purpose = *purpose;
      out->secure_store->write_transient->material.assign(in.material.begin(),
                                                          in.material.end());
    } else {
      out->secure_store->delete_handle =
          mojom::DeleteSecretHandleRequest::New();
      out->secure_store->delete_handle->secret_handle =
          std::string(in.secret_handle);
    }
    return out;
  }
  if (out->kind == mojom::EffectKind::kOpenAuthSurface) {
    const std::optional<mojom::AuthSurfaceOperation> surface_operation =
        wire::AuthSurfaceOperationFromWire(in.operation_kind);
    const std::optional<mojom::AccountAuthMethod> auth_method =
        wire::AccountAuthMethodFromWire(in.auth_method);
    if (!surface_operation || !auth_method) {
      return nullptr;
    }
    out->auth_surface = mojom::AuthSurfaceEffect::New();
    out->auth_surface->operation_kind = *surface_operation;
    if (out->auth_surface->operation_kind ==
        mojom::AuthSurfaceOperation::kOpenOauth) {
      out->auth_surface->oauth = mojom::OAuthSurfaceRequest::New();
      out->auth_surface->oauth->flow_id = std::string(in.flow_id);
      out->auth_surface->oauth->auth_method = *auth_method;
      out->auth_surface->oauth->redirect_binding_id =
          std::string(in.redirect_binding_id);
      out->auth_surface->oauth->pkce_challenge = std::string(in.pkce_challenge);
      out->auth_surface->oauth->pkce_verifier_handle =
          std::string(in.pkce_verifier_handle);
      out->auth_surface->oauth->state = std::string(in.state);
      for (uint8_t scope : in.scopes) {
        const std::optional<mojom::AccountScope> projected =
            wire::AccountScopeFromWire(scope);
        if (!projected) {
          return nullptr;
        }
        out->auth_surface->oauth->scopes.push_back(*projected);
      }
    } else {
      out->auth_surface->native_credential =
          mojom::NativeCredentialSurfaceRequest::New();
      out->auth_surface->native_credential->flow_id = std::string(in.flow_id);
      out->auth_surface->native_credential->auth_method = *auth_method;
      out->auth_surface->native_credential->raw_nonce_handle =
          std::string(in.raw_nonce_handle);
      out->auth_surface->native_credential->hashed_nonce =
          std::string(in.hashed_nonce);
    }
    return out;
  }
  return ToMojoNetworkEffect(in, std::move(out));
}

mojom::EffectEnvelopePtr
ToMojoNetworkEffect(const bridge::BridgeAccountEffect &in,
                    mojom::EffectEnvelopePtr out) {
  if (out->kind != mojom::EffectKind::kNetworkRequest) {
    return nullptr;
  }
  const std::optional<mojom::AccountNetworkOperation> operation_kind =
      wire::AccountNetworkOperationFromWire(in.operation_kind);
  const std::optional<mojom::AccountAuthMethod> auth_method =
      wire::AccountAuthMethodFromWire(in.auth_method);
  if (!operation_kind || !auth_method) {
    return nullptr;
  }
  out->network_request = mojom::NetworkRequestEffect::New();
  out->network_request->operation_kind = *operation_kind;
  out->network_request->max_response_bytes = in.max_response_bytes;
  switch (out->network_request->operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
    out->network_request->exchange_authorization_code =
        mojom::ExchangeAuthorizationCodeRequest::New();
    out->network_request->exchange_authorization_code->flow_id =
        std::string(in.flow_id);
    out->network_request->exchange_authorization_code->auth_method =
        *auth_method;
    out->network_request->exchange_authorization_code
        ->authorization_code_handle = std::string(in.authorization_code_handle);
    out->network_request->exchange_authorization_code->pkce_verifier_handle =
        std::string(in.pkce_verifier_handle);
    out->network_request->exchange_authorization_code->redirect_binding_id =
        std::string(in.redirect_binding_id);
    break;
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
    out->network_request->exchange_native_credential =
        mojom::ExchangeNativeCredentialRequest::New();
    out->network_request->exchange_native_credential->flow_id =
        std::string(in.flow_id);
    out->network_request->exchange_native_credential->auth_method =
        *auth_method;
    out->network_request->exchange_native_credential->credential_handle =
        std::string(in.credential_handle);
    out->network_request->exchange_native_credential->raw_nonce_handle =
        std::string(in.raw_nonce_handle);
    break;
  case mojom::AccountNetworkOperation::kRequestEmailLink:
    out->network_request->request_email_link =
        mojom::EmailLinkNetworkRequest::New();
    out->network_request->request_email_link->flow_id = std::string(in.flow_id);
    out->network_request->request_email_link->email = std::string(in.email);
    out->network_request->request_email_link->pkce_verifier_handle =
        std::string(in.pkce_verifier_handle);
    out->network_request->request_email_link->redirect_binding_id =
        std::string(in.redirect_binding_id);
    out->network_request->request_email_link->pkce_challenge =
        std::string(in.pkce_challenge);
    out->network_request->request_email_link->state = std::string(in.state);
    break;
  case mojom::AccountNetworkOperation::kRefreshSession:
    out->network_request->refresh_session = mojom::RefreshSessionRequest::New();
    out->network_request->refresh_session->session_handle =
        std::string(in.session_handle);
    out->network_request->refresh_session->expected_rotation =
        in.expected_rotation;
    out->network_request->refresh_session->expected_account_subject =
        std::string(in.account_subject);
    out->network_request->refresh_session->expected_auth_method = *auth_method;
    break;
  case mojom::AccountNetworkOperation::kRevokeSession:
    out->network_request->revoke_session = mojom::RevokeSessionRequest::New();
    out->network_request->revoke_session->session_handle =
        std::string(in.session_handle);
    break;
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    // The entitlement fetch is planned through the session's dedicated legs
    // (decision 0082) and never rides the account-effect channel; the
    // flattened account effect has no field for its body either. Refused
    // rather than forwarded empty.
    return nullptr;
  }
  return out;
}

} // namespace taffy::core_service_internal
