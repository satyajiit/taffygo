// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_account_effect_terminal.h"

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool PopulateNetworkTerminal(const mojom::EffectEnvelope &effect,
                             mojom::EffectResult *result) {
  if (!effect.network_request) {
    return false;
  }
  const auto &request = *effect.network_request;
  result->network = mojom::NetworkEffectResult::New();
  result->network->operation_kind = request.operation_kind;
  switch (request.operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
    result->network->authorization_code_session =
        mojom::AccountSessionReceipt::New();
    return true;
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
    result->network->native_credential_session =
        mojom::AccountSessionReceipt::New();
    return true;
  case mojom::AccountNetworkOperation::kRequestEmailLink:
    result->network->email_link = mojom::EmailLinkNetworkResult::New();
    if (request.request_email_link) {
      result->network->email_link->flow_id =
          request.request_email_link->flow_id;
    }
    return true;
  case mojom::AccountNetworkOperation::kRefreshSession:
    result->network->refreshed_session = mojom::AccountSessionReceipt::New();
    return true;
  case mojom::AccountNetworkOperation::kRevokeSession:
    result->network->revoked_session = mojom::RevokedSessionResult::New();
    if (request.revoke_session) {
      result->network->revoked_session->session_handle =
          request.revoke_session->session_handle;
    }
    return true;
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    // Never dispatched on this channel (decision 0082): the mint travels the
    // session's dedicated entitlement legs, so there is no terminal to
    // synthesize for it here.
    return false;
  }
}

bool PopulateSecureStoreTerminal(const mojom::EffectEnvelope &effect,
                                 mojom::EffectResult *result) {
  if (!effect.secure_store) {
    return false;
  }
  const auto &request = *effect.secure_store;
  result->secure_store = mojom::SecureStoreEffectResult::New();
  result->secure_store->operation_kind = request.operation_kind;
  switch (request.operation_kind) {
  case mojom::SecureStoreOperation::kGenerateEntropy:
    result->secure_store->generated_entropy =
        mojom::GeneratedEntropyResult::New();
    if (request.generate_entropy) {
      result->secure_store->generated_entropy->flow_id =
          request.generate_entropy->flow_id;
    }
    return true;
  case mojom::SecureStoreOperation::kWriteTransient:
    result->secure_store->transient_write =
        mojom::TransientSecretWriteResult::New();
    if (request.write_transient) {
      result->secure_store->transient_write->flow_id =
          request.write_transient->flow_id;
      result->secure_store->transient_write->purpose =
          request.write_transient->purpose;
    }
    return true;
  case mojom::SecureStoreOperation::kDeleteHandle:
    result->secure_store->deleted_handle =
        mojom::DeletedSecretHandleResult::New();
    if (request.delete_handle) {
      result->secure_store->deleted_handle->secret_handle =
          request.delete_handle->secret_handle;
    }
    return true;
  }
}

bool PopulateAuthSurfaceTerminal(const mojom::EffectEnvelope &effect,
                                 mojom::EffectResult *result) {
  if (!effect.auth_surface) {
    return false;
  }
  const auto &request = *effect.auth_surface;
  result->auth_surface = mojom::AuthSurfaceEffectResult::New();
  result->auth_surface->operation_kind = request.operation_kind;
  switch (request.operation_kind) {
  case mojom::AuthSurfaceOperation::kOpenOauth:
    result->auth_surface->oauth = mojom::OAuthSurfaceResult::New();
    if (request.oauth) {
      result->auth_surface->oauth->flow_id = request.oauth->flow_id;
    }
    return true;
  case mojom::AuthSurfaceOperation::kRequestNativeCredential:
    result->auth_surface->native_credential =
        mojom::NativeCredentialSurfaceResult::New();
    if (request.native_credential) {
      result->auth_surface->native_credential->flow_id =
          request.native_credential->flow_id;
    }
    return true;
  }
}

} // namespace

CanonicalSessionClearDecision
ResolveCanonicalSessionClear(mojom::EffectStatus requested_terminal,
                             mojom::EffectStatus storage_status, bool cleared) {
  const bool confirmed =
      storage_status == mojom::EffectStatus::kCompleted && cleared;
  return {
      confirmed ? requested_terminal : mojom::EffectStatus::kOutcomeUnknown,
      confirmed && requested_terminal == mojom::EffectStatus::kCompleted,
  };
}

bool RequiresAccountReconciliation(const mojom::EffectResult &result) {
  if (result.status != mojom::EffectStatus::kOutcomeUnknown ||
      result.kind != mojom::EffectKind::kNetworkRequest || !result.network) {
    return false;
  }
  switch (result.network->operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
  case mojom::AccountNetworkOperation::kRefreshSession:
  case mojom::AccountNetworkOperation::kRevokeSession:
    return true;
  case mojom::AccountNetworkOperation::kRequestEmailLink:
    return false;
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    // A mint changes no account-plane state: it reads an entitlement and the
    // token it produced dies with the browser process, so an unknown outcome
    // leaves nothing to reconcile.
    return false;
  }
}

bool PopulateCoreAccountTerminal(const mojom::EffectEnvelope &effect,
                                 mojom::EffectResult *result) {
  if (!result) {
    return false;
  }
  switch (effect.kind) {
  case mojom::EffectKind::kNetworkRequest:
    return PopulateNetworkTerminal(effect, result);
  case mojom::EffectKind::kSecureStore:
    return PopulateSecureStoreTerminal(effect, result);
  case mojom::EffectKind::kOpenAuthSurface:
    return PopulateAuthSurfaceTerminal(effect, result);
  // Not this plane's, so it fills nothing and says so. False is the honest
  // answer rather than a benign one: `CoreEffectBroker::MakeTerminal` calls
  // this only from the three arms above and fills every other kind itself, so
  // an effect that arrived here having no account body is a caller mistake and
  // must not be reported as a body that was written.
  //
  // Enumerated rather than defaulted for the reason the validation file's twin
  // switch gives — a kind added to the contract should stop this file
  // compiling, not slip through an arm nobody revisited.
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
    return false;
  }
  return false;
}

} // namespace taffy
