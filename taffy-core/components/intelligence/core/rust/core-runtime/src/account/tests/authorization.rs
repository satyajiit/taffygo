// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::fixtures::{intent, open_flow};
use crate::account::crypto::ReferenceSha256;
use crate::account::{
    AccountAuthMethod, AccountError, AccountFailure, AccountFailureCode, AccountProtocol,
    AuthFlowId, AuthorizationCodeHandle, AuthorizationEntropy, RedirectBindingId, RedirectOutcome,
    RedirectReceipt, RedirectState, SecretHandle, AUTHORIZATION_ENTROPY_BYTES,
    MAX_AUTHORIZATION_CODE_LIFETIME_MILLIS,
};
use crate::contract::Deadline;

#[test]
fn rust_derives_state_and_s256_before_a_valid_redirect() {
    let mut protocol = AccountProtocol::new();
    let plan = open_flow(&mut protocol, "flow-1");
    assert_eq!(
        plan.state.as_str(),
        "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE"
    );
    assert_eq!(
        plan.pkce_challenge.as_str(),
        "bB1ju9q0N8VDaMu9iIaohqea2XcpfiZe6_H19fAVM7k"
    );
    assert_eq!(plan.redirect_binding.as_str(), "primary_auth_callback");
    assert_eq!(
        plan.endpoint,
        crate::account::AccountEndpointId::SupabaseAuthorize
    );
    let receipt = RedirectReceipt {
        flow_id: AuthFlowId::new("flow-1").unwrap_or_else(|_| unreachable!()),
        redirect_binding: plan.redirect_binding.clone(),
        state: plan.state,
        outcome: RedirectOutcome::AuthorizationCode(
            AuthorizationCodeHandle::new("code-handle").unwrap_or_else(|_| unreachable!()),
        ),
    };
    assert!(matches!(
        protocol.accept_redirect(&receipt, 20),
        Ok(crate::account::AccountEffect::ExchangeCode(_))
    ));
    assert_eq!(protocol.pending_count(), 1);
    assert_eq!(
        protocol.accept_redirect(&receipt, 21),
        Err(AccountError::UnknownFlow)
    );
}

#[test]
fn wrong_state_does_not_consume_the_real_pending_flow() {
    let mut protocol = AccountProtocol::new();
    let _plan = open_flow(&mut protocol, "flow-2");
    let receipt = RedirectReceipt {
        flow_id: AuthFlowId::new("flow-2").unwrap_or_else(|_| unreachable!()),
        redirect_binding: RedirectBindingId::new("primary_auth_callback")
            .unwrap_or_else(|_| unreachable!()),
        state: RedirectState::new("x".repeat(32)).unwrap_or_else(|_| unreachable!()),
        outcome: RedirectOutcome::AuthorizationCode(
            AuthorizationCodeHandle::new("code-handle").unwrap_or_else(|_| unreachable!()),
        ),
    };
    assert_eq!(
        protocol.accept_redirect(&receipt, 20),
        Err(AccountError::StateMismatch)
    );
    assert_eq!(protocol.pending_count(), 1);
}

#[test]
fn wrong_registered_callback_binding_does_not_consume_the_flow() {
    let mut protocol = AccountProtocol::new();
    let plan = open_flow(&mut protocol, "flow-binding");
    let receipt = RedirectReceipt {
        flow_id: AuthFlowId::new("flow-binding").unwrap_or_else(|_| unreachable!()),
        redirect_binding: RedirectBindingId::new("different_registered_callback")
            .unwrap_or_else(|_| unreachable!()),
        state: plan.state,
        outcome: RedirectOutcome::AuthorizationCode(
            AuthorizationCodeHandle::new("code-handle").unwrap_or_else(|_| unreachable!()),
        ),
    };
    assert_eq!(
        protocol.accept_redirect(&receipt, 20),
        Err(AccountError::RedirectBindingMismatch)
    );
    assert_eq!(protocol.pending_count(), 1);
}

#[test]
fn authorization_code_lifetime_is_capped_at_five_minutes() {
    let mut protocol = AccountProtocol::new();
    let mut intent = intent("flow-too-long");
    intent.deadline = Deadline::from_millis(
        intent
            .issued_at_millis
            .saturating_add(MAX_AUTHORIZATION_CODE_LIFETIME_MILLIS)
            .saturating_add(1),
    );
    assert_eq!(
        protocol.begin_authorization(intent, 10),
        Err(AccountError::CodeLifetimeTooLong)
    );
}

#[test]
fn callback_binding_refuses_a_uri_and_stays_platform_neutral() {
    assert_eq!(
        RedirectBindingId::new("com.taffygo.browser://auth"),
        Err(AccountError::InvalidRedirectBinding)
    );
    let binding =
        RedirectBindingId::new("desktop_or_mobile_primary").unwrap_or_else(|_| unreachable!());
    assert_eq!(binding.as_str(), "desktop_or_mobile_primary");
}

#[test]
fn platform_input_cannot_skip_entropy_or_substitute_protocol_values() {
    let mut protocol = AccountProtocol::new();
    let flow_id = AuthFlowId::new("flow-staged").unwrap_or_else(|_| unreachable!());
    assert!(protocol
        .begin_authorization(intent("flow-staged"), 10)
        .is_ok());
    assert_eq!(
        protocol.accept_pkce_verifier_handle(
            &flow_id,
            SecretHandle::new("premature-handle").unwrap_or_else(|_| unreachable!()),
        ),
        Err(AccountError::UnknownFlow)
    );
    assert_eq!(
        protocol.accept_authorization_entropy(
            &flow_id,
            &AuthorizationEntropy::new([0_u8; AUTHORIZATION_ENTROPY_BYTES]),
            &ReferenceSha256,
        ),
        Err(AccountError::InvalidEntropy)
    );
    assert_eq!(protocol.pending_count(), 1);
}

#[test]
fn one_authorization_flow_owns_the_profile_session_mutation_slot() {
    let mut protocol = AccountProtocol::new();
    let first_flow = AuthFlowId::new("flow-serialized-first").unwrap_or_else(|_| unreachable!());
    assert!(protocol
        .begin_authorization(intent("flow-serialized-first"), 10)
        .is_ok());

    assert_eq!(
        protocol.begin_authorization(intent("flow-serialized-second"), 10),
        Err(AccountError::SessionMutationInFlight)
    );
    assert_eq!(
        protocol.begin_native_authorization(
            AuthFlowId::new("flow-native-second").unwrap_or_else(|_| unreachable!()),
            AccountAuthMethod::Google,
            Deadline::from_millis(500),
            10,
        ),
        Err(AccountError::SessionMutationInFlight)
    );
    assert_eq!(
        protocol.begin_authorization(intent("flow-serialized-first"), 10),
        Err(AccountError::DuplicateFlow)
    );

    assert!(protocol.cancel_flow(
        &first_flow,
        AccountFailure::for_method(AccountFailureCode::Cancelled, AccountAuthMethod::Github),
    ));
    assert!(protocol
        .begin_authorization(intent("flow-serialized-second"), 10)
        .is_ok());
}
