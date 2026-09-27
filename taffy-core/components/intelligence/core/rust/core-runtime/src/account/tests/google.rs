// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::account::crypto::ReferenceSha256;
use crate::account::{
    AccountAuthMethod, AccountEffect, AccountError, AccountProtocol, AuthFlowId,
    GoogleNonceEntropy, NativeCredentialOutcome, SecretHandle, GOOGLE_NONCE_ENTROPY_BYTES,
};
use crate::contract::Deadline;

#[test]
fn google_nonce_hash_and_raw_handle_stay_on_their_typed_paths() {
    let mut protocol = AccountProtocol::new();
    let flow_id = AuthFlowId::new("google-flow-1").unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        protocol.begin_native_authorization(
            flow_id.clone(),
            AccountAuthMethod::Google,
            Deadline::from_millis(500),
            10,
        ),
        Ok(AccountEffect::RequestGoogleNonceEntropy {
            bytes: GOOGLE_NONCE_ENTROPY_BYTES,
            ..
        })
    ));

    let entropy = GoogleNonceEntropy::new([1; GOOGLE_NONCE_ENTROPY_BYTES]);
    let Ok(AccountEffect::StoreGoogleRawNonce {
        raw_nonce,
        hashed_nonce,
        ..
    }) = protocol.accept_google_nonce_entropy(&flow_id, &entropy, &ReferenceSha256)
    else {
        unreachable!()
    };
    assert_eq!(
        String::from_utf8(raw_nonce.into_bytes()).unwrap_or_default(),
        "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE"
    );
    assert_eq!(
        hashed_nonce.as_str(),
        "56d5fa7333f6d747db42c239407e5da4c32f4c79f35d092b134fd35a402d9c5c"
    );

    let nonce_handle =
        SecretHandle::new("google-raw-nonce-handle").unwrap_or_else(|_| unreachable!());
    let Ok(AccountEffect::RequestNativeCredential {
        raw_nonce_handle,
        hashed_nonce,
        ..
    }) = protocol.accept_google_raw_nonce_handle(&flow_id, nonce_handle.clone())
    else {
        unreachable!()
    };
    assert_eq!(raw_nonce_handle, nonce_handle);
    assert_eq!(
        hashed_nonce.as_str(),
        "56d5fa7333f6d747db42c239407e5da4c32f4c79f35d092b134fd35a402d9c5c"
    );

    let credential_handle =
        SecretHandle::new("google-id-token-handle").unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        protocol.accept_native_credential(
            &flow_id,
            AccountAuthMethod::Google,
            NativeCredentialOutcome::Success(credential_handle.clone()),
            20,
        ),
        Ok(AccountEffect::ExchangeNativeCredential(plan))
            if plan.credential_handle == credential_handle
                && plan.raw_nonce_handle == nonce_handle
    ));
}

#[test]
fn terminal_google_surface_failure_spends_the_raw_nonce_handle() {
    let mut protocol = AccountProtocol::new();
    let flow_id = AuthFlowId::new("google-flow-cancelled").unwrap_or_else(|_| unreachable!());
    assert!(protocol
        .begin_native_authorization(
            flow_id.clone(),
            AccountAuthMethod::Google,
            Deadline::from_millis(500),
            10,
        )
        .is_ok());
    let entropy = GoogleNonceEntropy::new([2; GOOGLE_NONCE_ENTROPY_BYTES]);
    assert!(protocol
        .accept_google_nonce_entropy(&flow_id, &entropy, &ReferenceSha256)
        .is_ok());
    let nonce_handle =
        SecretHandle::new("google-cancelled-nonce").unwrap_or_else(|_| unreachable!());
    assert!(protocol
        .accept_google_raw_nonce_handle(&flow_id, nonce_handle.clone())
        .is_ok());
    assert!(matches!(
        protocol.accept_native_credential(
            &flow_id,
            AccountAuthMethod::Google,
            NativeCredentialOutcome::Cancelled,
            20,
        ),
        Ok(AccountEffect::DeleteNativeNonce { raw_nonce_handle, .. })
            if raw_nonce_handle == nonce_handle
    ));
    assert_eq!(protocol.pending_count(), 0);
}

#[test]
fn reconciliation_fence_clears_each_pre_surface_google_nonce_stage() {
    let first_flow = AuthFlowId::new("google-before-entropy").unwrap_or_else(|_| unreachable!());
    let mut awaiting_entropy = AccountProtocol::new();
    assert!(awaiting_entropy
        .begin_native_authorization(
            first_flow,
            AccountAuthMethod::Google,
            Deadline::from_millis(500),
            10,
        )
        .is_ok());
    awaiting_entropy.require_reconciliation();
    assert_eq!(awaiting_entropy.pending_count(), 0);

    let second_flow = AuthFlowId::new("google-before-handle").unwrap_or_else(|_| unreachable!());
    let mut awaiting_handle = AccountProtocol::new();
    assert!(awaiting_handle
        .begin_native_authorization(
            second_flow.clone(),
            AccountAuthMethod::Google,
            Deadline::from_millis(500),
            10,
        )
        .is_ok());
    assert!(awaiting_handle
        .accept_google_nonce_entropy(
            &second_flow,
            &GoogleNonceEntropy::new([3; GOOGLE_NONCE_ENTROPY_BYTES]),
            &ReferenceSha256,
        )
        .is_ok());
    awaiting_handle.require_reconciliation();
    assert_eq!(awaiting_handle.pending_count(), 0);
    assert_eq!(
        awaiting_handle.begin_native_authorization(
            AuthFlowId::new("google-after-fence").unwrap_or_else(|_| unreachable!()),
            AccountAuthMethod::Google,
            Deadline::from_millis(500),
            20,
        ),
        Err(AccountError::ReconciliationRequired)
    );
}
