// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_account_effect_validation.h"

#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::EffectEnvelopePtr NativeSurfaceEffect() {
  auto effect = mojom::EffectEnvelope::New();
  effect->kind = mojom::EffectKind::kOpenAuthSurface;
  effect->auth_surface = mojom::AuthSurfaceEffect::New();
  effect->auth_surface->operation_kind =
      mojom::AuthSurfaceOperation::kRequestNativeCredential;
  effect->auth_surface->native_credential =
      mojom::NativeCredentialSurfaceRequest::New();
  effect->auth_surface->native_credential->flow_id = "google-flow";
  effect->auth_surface->native_credential->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  effect->auth_surface->native_credential->raw_nonce_handle =
      "raw-nonce-handle";
  effect->auth_surface->native_credential->hashed_nonce =
      std::string(mojom::kGoogleNonceHashHexBytes, 'a');
  return effect;
}

mojom::EffectEnvelopePtr NativeExchangeEffect() {
  auto effect = mojom::EffectEnvelope::New();
  effect->kind = mojom::EffectKind::kNetworkRequest;
  effect->network_request = mojom::NetworkRequestEffect::New();
  effect->network_request->operation_kind =
      mojom::AccountNetworkOperation::kExchangeNativeCredential;
  effect->network_request->max_response_bytes = mojom::kMaxAccountResponseBytes;
  effect->network_request->exchange_native_credential =
      mojom::ExchangeNativeCredentialRequest::New();
  effect->network_request->exchange_native_credential->flow_id = "google-flow";
  effect->network_request->exchange_native_credential->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  effect->network_request->exchange_native_credential->credential_handle =
      "credential-handle";
  effect->network_request->exchange_native_credential->raw_nonce_handle =
      "raw-nonce-handle";
  return effect;
}

TEST(CoreAccountEffectValidationTest,
     GoogleSurfaceRequiresLowercaseHashAndRawNonceHandle) {
  mojom::EffectEnvelopePtr effect = NativeSurfaceEffect();
  EXPECT_TRUE(IsValidCoreAccountEffectBody(*effect, mojom::kMaxIdentifierBytes,
                                           mojom::kMaxEffectBytes));

  effect->auth_surface->native_credential->hashed_nonce.front() = 'A';
  EXPECT_FALSE(IsValidCoreAccountEffectBody(*effect, mojom::kMaxIdentifierBytes,
                                            mojom::kMaxEffectBytes));
  effect->auth_surface->native_credential->hashed_nonce.front() = 'a';
  effect->auth_surface->native_credential->raw_nonce_handle.clear();
  EXPECT_FALSE(IsValidCoreAccountEffectBody(*effect, mojom::kMaxIdentifierBytes,
                                            mojom::kMaxEffectBytes));
}

TEST(CoreAccountEffectValidationTest,
     GoogleExchangeRequiresBothSingleUseHandles) {
  mojom::EffectEnvelopePtr effect = NativeExchangeEffect();
  EXPECT_TRUE(IsValidCoreAccountEffectBody(*effect, mojom::kMaxIdentifierBytes,
                                           mojom::kMaxEffectBytes));

  effect->network_request->exchange_native_credential->raw_nonce_handle.clear();
  EXPECT_FALSE(IsValidCoreAccountEffectBody(*effect, mojom::kMaxIdentifierBytes,
                                            mojom::kMaxEffectBytes));
}

// The entitlement fetch travels the session's dedicated legs (decision 0082);
// the account-effect channel refuses it even fully formed, because this
// channel dispatches against the account plane's origin and a mint is spoken
// to the managed worker's.
TEST(CoreAccountEffectValidationTest,
     AFetchEntitlementEnvelopeIsRefusedOnTheAccountChannel) {
  auto effect = mojom::EffectEnvelope::New();
  effect->kind = mojom::EffectKind::kNetworkRequest;
  effect->network_request = mojom::NetworkRequestEffect::New();
  effect->network_request->operation_kind =
      mojom::AccountNetworkOperation::kFetchEntitlement;
  effect->network_request->max_response_bytes = mojom::kMaxAccountResponseBytes;
  effect->network_request->fetch_entitlement =
      mojom::FetchEntitlementRequest::New();
  effect->network_request->fetch_entitlement->reason =
      mojom::EntitlementFetchReason::kBootstrap;
  EXPECT_FALSE(IsValidCoreAccountEffectBody(*effect, mojom::kMaxIdentifierBytes,
                                            mojom::kMaxEffectBytes));
}

} // namespace
} // namespace taffy
