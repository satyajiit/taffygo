// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_account_effect_validation.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string &value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bool IsLowercaseSha256Hex(std::string_view value) {
  if (value.size() != mojom::kGoogleNonceHashHexBytes) {
    return false;
  }
  return std::ranges::all_of(value, [](const char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

bool ValidateNetworkEffect(const mojom::NetworkRequestEffect &body,
                           size_t maximum) {
  const size_t body_count =
      static_cast<size_t>(!!body.exchange_authorization_code) +
      static_cast<size_t>(!!body.exchange_native_credential) +
      static_cast<size_t>(!!body.request_email_link) +
      static_cast<size_t>(!!body.refresh_session) +
      static_cast<size_t>(!!body.revoke_session) +
      static_cast<size_t>(!!body.fetch_entitlement);
  if (body_count != 1u || body.max_response_bytes == 0u ||
      body.max_response_bytes > mojom::kMaxAccountResponseBytes) {
    return false;
  }
  switch (body.operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
    return body.exchange_authorization_code &&
           IsIdentifier(body.exchange_authorization_code->flow_id, maximum) &&
           IsIdentifier(
               body.exchange_authorization_code->authorization_code_handle,
               maximum) &&
           IsIdentifier(body.exchange_authorization_code->pkce_verifier_handle,
                        maximum) &&
           IsIdentifier(body.exchange_authorization_code->redirect_binding_id,
                        maximum);
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
    return body.exchange_native_credential &&
           IsIdentifier(body.exchange_native_credential->flow_id, maximum) &&
           IsIdentifier(body.exchange_native_credential->credential_handle,
                        maximum) &&
           IsIdentifier(body.exchange_native_credential->raw_nonce_handle,
                        maximum);
  case mojom::AccountNetworkOperation::kRequestEmailLink:
    return body.request_email_link &&
           IsIdentifier(body.request_email_link->flow_id, maximum) &&
           !body.request_email_link->email.empty() &&
           body.request_email_link->email.size() <=
               mojom::kMaxAccountEmailBytes &&
           IsIdentifier(body.request_email_link->pkce_verifier_handle,
                        maximum) &&
           IsIdentifier(body.request_email_link->redirect_binding_id,
                        maximum) &&
           IsIdentifier(body.request_email_link->pkce_challenge, maximum) &&
           IsIdentifier(body.request_email_link->state, maximum);
  case mojom::AccountNetworkOperation::kRefreshSession:
    return body.refresh_session &&
           IsIdentifier(body.refresh_session->session_handle, maximum) &&
           body.refresh_session->expected_rotation > 0u &&
           IsIdentifier(body.refresh_session->expected_account_subject,
                        maximum);
  case mojom::AccountNetworkOperation::kRevokeSession:
    return body.revoke_session &&
           IsIdentifier(body.revoke_session->session_handle, maximum);
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    // Planned and delivered through the session's dedicated entitlement legs
    // (decision 0082), never through the account-effect channel: this channel
    // dispatches against the account plane's origin, and a mint is spoken to
    // the managed worker's. An envelope claiming it here is refused whole.
    return false;
  }
}

bool ValidateSecureStoreEffect(const mojom::SecureStoreEffect &body,
                               size_t maximum, size_t max_effect_bytes) {
  const size_t body_count = static_cast<size_t>(!!body.generate_entropy) +
                            static_cast<size_t>(!!body.write_transient) +
                            static_cast<size_t>(!!body.delete_handle);
  if (body_count != 1u) {
    return false;
  }
  switch (body.operation_kind) {
  case mojom::SecureStoreOperation::kGenerateEntropy:
    return body.generate_entropy &&
           IsIdentifier(body.generate_entropy->flow_id, maximum) &&
           body.generate_entropy->byte_count > 0u &&
           body.generate_entropy->byte_count <= max_effect_bytes;
  case mojom::SecureStoreOperation::kWriteTransient:
    return body.write_transient &&
           IsIdentifier(body.write_transient->flow_id, maximum) &&
           !body.write_transient->material.empty() &&
           body.write_transient->material.size() <= max_effect_bytes;
  case mojom::SecureStoreOperation::kDeleteHandle:
    return body.delete_handle &&
           IsIdentifier(body.delete_handle->secret_handle, maximum);
  }
}

bool ValidateAuthSurfaceEffect(const mojom::AuthSurfaceEffect &body,
                               size_t maximum) {
  if (static_cast<size_t>(!!body.oauth) +
          static_cast<size_t>(!!body.native_credential) !=
      1u) {
    return false;
  }
  if (body.operation_kind == mojom::AuthSurfaceOperation::kOpenOauth) {
    return body.oauth && IsIdentifier(body.oauth->flow_id, maximum) &&
           IsIdentifier(body.oauth->redirect_binding_id, maximum) &&
           IsIdentifier(body.oauth->pkce_verifier_handle, maximum) &&
           IsIdentifier(body.oauth->pkce_challenge, maximum) &&
           IsIdentifier(body.oauth->state, maximum) &&
           !body.oauth->scopes.empty() &&
           body.oauth->scopes.size() <= mojom::kMaxAccountScopes;
  }
  return body.native_credential &&
         body.native_credential->auth_method ==
             mojom::AccountAuthMethod::kGoogle &&
         IsIdentifier(body.native_credential->flow_id, maximum) &&
         IsIdentifier(body.native_credential->raw_nonce_handle, maximum) &&
         IsLowercaseSha256Hex(body.native_credential->hashed_nonce);
}

bool HasMatchingSecureResult(const mojom::EffectEnvelope &effect,
                             const mojom::EffectResult &result) {
  if (!effect.secure_store || !result.secure_store ||
      effect.secure_store->operation_kind !=
          result.secure_store->operation_kind) {
    return false;
  }
  const size_t body_count =
      static_cast<size_t>(!!result.secure_store->generated_entropy) +
      static_cast<size_t>(!!result.secure_store->transient_write) +
      static_cast<size_t>(!!result.secure_store->deleted_handle);
  if (body_count != 1u) {
    return false;
  }
  switch (result.secure_store->operation_kind) {
  case mojom::SecureStoreOperation::kGenerateEntropy:
    return result.secure_store->generated_entropy &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.secure_store->generate_entropy &&
             result.secure_store->generated_entropy->flow_id ==
                 effect.secure_store->generate_entropy->flow_id &&
             result.secure_store->generated_entropy->entropy.size() ==
                 effect.secure_store->generate_entropy->byte_count));
  case mojom::SecureStoreOperation::kWriteTransient:
    return result.secure_store->transient_write &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.secure_store->write_transient &&
             result.secure_store->transient_write->flow_id ==
                 effect.secure_store->write_transient->flow_id &&
             result.secure_store->transient_write->purpose ==
                 effect.secure_store->write_transient->purpose &&
             !result.secure_store->transient_write->secret_handle.empty()));
  case mojom::SecureStoreOperation::kDeleteHandle:
    return result.secure_store->deleted_handle &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.secure_store->delete_handle &&
             result.secure_store->deleted_handle->secret_handle ==
                 effect.secure_store->delete_handle->secret_handle));
  }
}

bool HasMatchingSurfaceResult(const mojom::EffectEnvelope &effect,
                              const mojom::EffectResult &result) {
  if (!effect.auth_surface || !result.auth_surface ||
      effect.auth_surface->operation_kind !=
          result.auth_surface->operation_kind) {
    return false;
  }
  if (result.auth_surface->operation_kind ==
      mojom::AuthSurfaceOperation::kOpenOauth) {
    return result.auth_surface->oauth &&
           !result.auth_surface->native_credential &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.auth_surface->oauth &&
             result.auth_surface->oauth->flow_id ==
                 effect.auth_surface->oauth->flow_id));
  }
  return !result.auth_surface->oauth &&
         result.auth_surface->native_credential &&
         (result.status != mojom::EffectStatus::kCompleted ||
          (effect.auth_surface->native_credential &&
           result.auth_surface->native_credential->flow_id ==
               effect.auth_surface->native_credential->flow_id));
}

bool HasMatchingNetworkResult(const mojom::EffectEnvelope &effect,
                              const mojom::EffectResult &result) {
  if (!effect.network_request || !result.network ||
      effect.network_request->operation_kind !=
          result.network->operation_kind) {
    return false;
  }
  const size_t body_count =
      static_cast<size_t>(!!result.network->authorization_code_session) +
      static_cast<size_t>(!!result.network->native_credential_session) +
      static_cast<size_t>(!!result.network->email_link) +
      static_cast<size_t>(!!result.network->refreshed_session) +
      static_cast<size_t>(!!result.network->revoked_session) +
      static_cast<size_t>(!!result.network->entitlement_summary);
  if (body_count != 1u) {
    return false;
  }
  switch (result.network->operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
    return result.network->authorization_code_session &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.network_request->exchange_authorization_code &&
             result.network->authorization_code_session->rotation == 0u &&
             result.network->authorization_code_session->auth_method ==
                 effect.network_request->exchange_authorization_code
                     ->auth_method &&
             IsIdentifier(
                 result.network->authorization_code_session->session_handle,
                 mojom::kMaxIdentifierBytes) &&
             IsIdentifier(
                 result.network->authorization_code_session->account_subject,
                 mojom::kMaxIdentifierBytes) &&
             result.network->authorization_code_session
                     ->expires_at_monotonic_ms > 0u));
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
    return result.network->native_credential_session &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.network_request->exchange_native_credential &&
             result.network->native_credential_session->rotation == 0u &&
             result.network->native_credential_session->auth_method ==
                 effect.network_request->exchange_native_credential
                     ->auth_method &&
             IsIdentifier(
                 result.network->native_credential_session->session_handle,
                 mojom::kMaxIdentifierBytes) &&
             IsIdentifier(
                 result.network->native_credential_session->account_subject,
                 mojom::kMaxIdentifierBytes) &&
             result.network->native_credential_session
                     ->expires_at_monotonic_ms > 0u));
  case mojom::AccountNetworkOperation::kRequestEmailLink:
    return result.network->email_link &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.network_request->request_email_link &&
             result.network->email_link->flow_id ==
                 effect.network_request->request_email_link->flow_id));
  case mojom::AccountNetworkOperation::kRefreshSession:
    return result.network->refreshed_session &&
           (result.status != mojom::EffectStatus::kCompleted ||
            (effect.network_request->refresh_session &&
             result.network->refreshed_session->session_handle ==
                 effect.network_request->refresh_session->session_handle &&
             result.network->refreshed_session->rotation ==
                 effect.network_request->refresh_session->expected_rotation &&
             result.network->refreshed_session->account_subject ==
                 effect.network_request->refresh_session
                     ->expected_account_subject &&
             result.network->refreshed_session->auth_method ==
                 effect.network_request->refresh_session
                     ->expected_auth_method &&
             result.network->refreshed_session->expires_at_monotonic_ms > 0u));
  case mojom::AccountNetworkOperation::kRevokeSession:
    return result.network->revoked_session &&
           effect.network_request->revoke_session &&
           result.network->revoked_session->session_handle ==
               effect.network_request->revoke_session->session_handle &&
           ((result.status == mojom::EffectStatus::kCompleted) ==
            result.network->revoked_session->deleted);
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    // The effect side of this operation is refused above, so no result can
    // match one on this channel either.
    return false;
  }
}

} // namespace

bool IsValidCoreAccountEffectBody(const mojom::EffectEnvelope &effect,
                                  size_t max_identifier_bytes,
                                  size_t max_effect_bytes) {
  if (effect.network_request) {
    return ValidateNetworkEffect(*effect.network_request, max_identifier_bytes);
  }
  if (effect.secure_store) {
    return ValidateSecureStoreEffect(*effect.secure_store, max_identifier_bytes,
                                     max_effect_bytes);
  }
  return effect.auth_surface &&
         ValidateAuthSurfaceEffect(*effect.auth_surface, max_identifier_bytes);
}

bool HasMatchingCoreAccountResult(const mojom::EffectEnvelope &effect,
                                  const mojom::EffectResult &result) {
  switch (effect.kind) {
  case mojom::EffectKind::kSecureStore:
    return HasMatchingSecureResult(effect, result);
  case mojom::EffectKind::kOpenAuthSurface:
    return HasMatchingSurfaceResult(effect, result);
  case mojom::EffectKind::kNetworkRequest:
    return HasMatchingNetworkResult(effect, result);
  // Not this plane's, and answered true deliberately: the account plane has
  // nothing to say about an effect it did not compose, and `true` here means
  // "no account-plane objection" rather than "checked and found sound". Who
  // does check them is `IsValidCoreEffectResult` in core_effect_validation.cc,
  // which is the sole caller — it pairs this answer with `HasMatchingResultBody`
  // for every kind, with the typed correlations it makes itself for the tool,
  // permission and delivery results, and with the byte ceiling.
  //
  // Written out rather than left to a `default:` so that the next effect kind
  // added to the contract is a -Wswitch error in this file. A silent `default`
  // arm answered `true` for `kProbeCustomEndpoint` the moment core-service 2.47
  // declared it, which is a new result kind passing account-plane validation
  // unexamined and unremarked — a compile error is the only thing that makes
  // that a decision instead of an accident.
  case mojom::EffectKind::kStorageCommit:
  case mojom::EffectKind::kPageObservation:
  case mojom::EffectKind::kModelRequest:
  case mojom::EffectKind::kBrowserAction:
  case mojom::EffectKind::kToolJob:
  case mojom::EffectKind::kRequestPermission:
  case mojom::EffectKind::kDeliverAsset:
  case mojom::EffectKind::kFetchCatalog:
  case mojom::EffectKind::kFetchProviderListing:
  case mojom::EffectKind::kDeliverComposerCompletion:
  case mojom::EffectKind::kProbeCustomEndpoint:
    return true;
  }
  return true;
}

size_t CoreAccountResultByteSize(const mojom::EffectResult &result) {
  return result.secure_store && result.secure_store->generated_entropy
             ? result.secure_store->generated_entropy->entropy.size()
             : 0u;
}

} // namespace taffy
