// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every state screen SCR-701 can reach, driven through the real protocol.
//!
//! Separated from the task projection beside it because the two share no fact
//! and no fixture: one is a reducer's view of a task, the other is a protocol's
//! view of an account, and reading either meant scrolling past the other.
//!
//! Each case here drives [`ProductionAccount`] through the stages the browser
//! performs rather than installing a field, because the defect these close was
//! never in the projection alone — the facts existed nowhere, so a projection
//! over them could not have been written, and a test that set them directly
//! would pass over the same hole.

use core_api_types::{
    AuthFailureCode, AuthMethodAvailability, AuthPhase, AuthProvider, AuthViewState,
};

use crate::account::crypto::ReferenceSha256;
use crate::account::{
    AccountAuthMethod, AccountDisplayName, AccountEffect, AccountEmail, AccountFailure,
    AccountFailureCode, AccountScope, AccountSession, AccountSubjectId, AuthFlowId,
    AuthorizationEntropy, AuthorizationIntent, GoogleNonceEntropy, NativeCredentialOutcome,
    RedirectBindingId, SecretHandle, SessionHandle, GOOGLE_NONCE_ENTROPY_BYTES,
};
use crate::adapters::account::ProductionAccount;
use crate::contract::Deadline;
use crate::ports::AccountPort;

use super::super::project_account_view;

fn flow_id(value: &str) -> AuthFlowId {
    AuthFlowId::new(value).unwrap_or_else(|_| unreachable!())
}

fn intent(value: &str, method: AccountAuthMethod) -> AuthorizationIntent {
    AuthorizationIntent {
        flow_id: flow_id(value),
        auth_method: method,
        email: (method == AccountAuthMethod::EmailLink)
            .then(|| AccountEmail::new("person@example.com").unwrap_or_else(|_| unreachable!())),
        redirect_binding: RedirectBindingId::new("primary_auth_callback")
            .unwrap_or_else(|_| unreachable!()),
        scopes: vec![AccountScope::OpenId, AccountScope::Email],
        issued_at_millis: 10,
        deadline: Deadline::from_millis(500),
    }
}

/// Opens one redirect flow through every stage the browser performs.
///
/// Google is not accepted here and cannot be: it has no redirect flow at all
/// (`begin_authorization` refuses it), which is why [`open_native_flow`] is a
/// second helper rather than a branch of this one.
fn open_flow(account: &mut ProductionAccount, value: &str, method: AccountAuthMethod) {
    assert!(matches!(
        account.begin_authorization(intent(value, method), 10),
        Ok(AccountEffect::RequestSecureEntropy { bytes: 64, .. })
    ));
    let entropy = AuthorizationEntropy::new(std::array::from_fn(
        |position| if position < 32 { 1 } else { 2 },
    ));
    assert!(matches!(
        account.accept_authorization_entropy(&flow_id(value), &entropy, &ReferenceSha256),
        Ok(AccountEffect::StorePkceVerifier { .. })
    ));
    let opened = account.accept_pkce_verifier_handle(
        &flow_id(value),
        SecretHandle::new(format!("verifier-{value}")).unwrap_or_else(|_| unreachable!()),
    );
    match method {
        AccountAuthMethod::EmailLink => {
            assert!(matches!(opened, Ok(AccountEffect::RequestEmailLink(_))));
        }
        AccountAuthMethod::Github | AccountAuthMethod::Facebook => {
            assert!(matches!(opened, Ok(AccountEffect::OpenAuthorization(_))));
        }
        AccountAuthMethod::Google => unreachable!("Google has no redirect flow"),
    }
}

/// Opens the native Google flow through to the credential request.
fn open_native_flow(account: &mut ProductionAccount, value: &str) {
    assert!(matches!(
        account.begin_native_authorization(
            flow_id(value),
            AccountAuthMethod::Google,
            Deadline::from_millis(500),
            10,
        ),
        Ok(AccountEffect::RequestGoogleNonceEntropy { bytes: 32, .. })
    ));
    let entropy = GoogleNonceEntropy::new([7; GOOGLE_NONCE_ENTROPY_BYTES]);
    assert!(matches!(
        account.accept_google_nonce_entropy(&flow_id(value), &entropy, &ReferenceSha256),
        Ok(AccountEffect::StoreGoogleRawNonce { .. })
    ));
    assert!(matches!(
        account.accept_google_raw_nonce_handle(
            &flow_id(value),
            SecretHandle::new(format!("nonce-{value}")).unwrap_or_else(|_| unreachable!()),
        ),
        Ok(AccountEffect::RequestNativeCredential { .. })
    ));
}

fn availability_of(view: &AuthViewState, provider: AuthProvider) -> AuthMethodAvailability {
    view.methods
        .iter()
        .find(|method| method.provider == provider)
        .unwrap_or_else(|| unreachable!())
        .availability
}

#[test]
fn every_failure_reason_reaches_the_screen_with_its_own_retry_answer() {
    // Six translated failure sentences were unreachable before the protocol
    // recorded why a flow ended. This asserts the whole closed set, not a
    // sample, because a reason the projection dropped would be invisible
    // rather than wrong.
    for code in AccountFailureCode::ALL {
        let mut account =
            ProductionAccount::with_available_methods(vec![AccountAuthMethod::Github]);
        open_flow(&mut account, "flow-failure", AccountAuthMethod::Github);
        assert!(account.cancel_flow(
            &flow_id("flow-failure"),
            AccountFailure::for_method(code, AccountAuthMethod::Github),
        ));

        let view = project_account_view(&account);
        assert_eq!(view.phase, AuthPhase::Failed, "{code:?}");
        let failure = view.failure.unwrap_or_else(|| unreachable!());
        assert_eq!(failure.code, code.to_wire(), "{code:?}");
        assert_eq!(failure.retryable, code.retryable(), "{code:?}");
        assert!(view.account.is_none(), "{code:?}");
    }
}

#[test]
fn a_cancelled_sheet_and_a_refused_provider_are_no_longer_the_same_state() {
    // The pair this projection exists to separate: one is worth offering the
    // same button for again, the other is not.
    let mut cancelled = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Google]);
    open_native_flow(&mut cancelled, "flow-cancel");
    assert!(cancelled
        .accept_native_credential(
            &flow_id("flow-cancel"),
            AccountAuthMethod::Google,
            NativeCredentialOutcome::Cancelled,
            20,
        )
        .is_ok());

    let mut refused = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Github]);
    open_flow(&mut refused, "flow-refuse", AccountAuthMethod::Github);
    assert!(refused.cancel_flow(
        &flow_id("flow-refuse"),
        AccountFailure::for_method(AccountFailureCode::Rejected, AccountAuthMethod::Github),
    ));

    let cancelled = project_account_view(&cancelled);
    let refused = project_account_view(&refused);
    assert_eq!(cancelled.phase, refused.phase);
    assert_ne!(cancelled.failure, refused.failure);
    assert!(
        cancelled
            .failure
            .unwrap_or_else(|| unreachable!())
            .retryable
    );
    assert!(!refused.failure.unwrap_or_else(|| unreachable!()).retryable);
}

#[test]
fn a_sent_link_outranks_the_flow_it_is_still_waiting_on() {
    // The redirect stays pending for exactly as long as "check your email" is
    // true, so reading `pending_count` first would leave LINK_SENT unreachable.
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::EmailLink]);
    open_flow(&mut account, "flow-link", AccountAuthMethod::EmailLink);
    assert_eq!(project_account_view(&account).phase, AuthPhase::InFlight);

    assert_eq!(
        account.accept_email_link_delivery(&flow_id("flow-link"), true),
        Ok(())
    );
    assert_ne!(account.pending_count(), 0);

    let view = project_account_view(&account);
    assert_eq!(view.phase, AuthPhase::LinkSent);
    assert_eq!(view.pending_email.as_deref(), Some("person@example.com"));
    assert!(view.failure.is_none());
}

#[test]
fn a_refused_link_replaces_the_address_with_the_reason() {
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::EmailLink]);
    open_flow(
        &mut account,
        "flow-link-refused",
        AccountAuthMethod::EmailLink,
    );
    assert!(account
        .accept_email_link_delivery(&flow_id("flow-link-refused"), false)
        .is_err());

    let view = project_account_view(&account);
    assert_eq!(view.phase, AuthPhase::Failed);
    assert!(view.pending_email.is_none());
    assert_eq!(
        view.failure.unwrap_or_else(|| unreachable!()).code,
        AuthFailureCode::Rejected
    );
}

#[test]
fn starting_again_stops_describing_the_attempt_that_failed() {
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Github]);
    open_flow(&mut account, "flow-first", AccountAuthMethod::Github);
    assert!(account.cancel_flow(
        &flow_id("flow-first"),
        AccountFailure::for_method(AccountFailureCode::Network, AccountAuthMethod::Github),
    ));
    assert_eq!(project_account_view(&account).phase, AuthPhase::Failed);

    open_flow(&mut account, "flow-second", AccountAuthMethod::Github);
    let view = project_account_view(&account);
    assert_eq!(view.phase, AuthPhase::InFlight);
    assert!(view.failure.is_none());
}

#[test]
fn every_method_is_drawn_and_only_the_reported_ones_are_offered() {
    // Absent from the list is a row that says why, never a row that is missing:
    // a screen cannot explain a button it did not draw.
    let account = ProductionAccount::with_available_methods(vec![
        AccountAuthMethod::EmailLink,
        AccountAuthMethod::Github,
    ]);
    let view = project_account_view(&account);

    assert_eq!(view.methods.len(), 4);
    assert_eq!(
        view.methods
            .iter()
            .map(|method| method.provider)
            .collect::<Vec<_>>(),
        vec![
            AuthProvider::Google,
            AuthProvider::EmailLink,
            AuthProvider::Github,
            AuthProvider::Facebook,
        ]
    );
    assert_eq!(
        availability_of(&view, AuthProvider::EmailLink),
        AuthMethodAvailability::Available
    );
    assert_eq!(
        availability_of(&view, AuthProvider::Github),
        AuthMethodAvailability::Available
    );
    assert_eq!(
        availability_of(&view, AuthProvider::Google),
        AuthMethodAvailability::NotConfigured
    );
    assert_eq!(
        availability_of(&view, AuthProvider::Facebook),
        AuthMethodAvailability::NotConfigured
    );
}

#[test]
fn a_method_that_refused_to_start_stops_being_offered_but_its_neighbours_do_not() {
    let mut account = ProductionAccount::with_available_methods(vec![
        AccountAuthMethod::Google,
        AccountAuthMethod::Github,
    ]);
    // The real path: the browser refuses to open Credential Manager at all,
    // which is what an empty server client id produces on a device.
    open_native_flow(&mut account, "flow-unconfigured");
    assert!(account
        .accept_native_credential(
            &flow_id("flow-unconfigured"),
            AccountAuthMethod::Google,
            NativeCredentialOutcome::Unavailable,
            20,
        )
        .is_ok());

    let view = project_account_view(&account);
    assert_eq!(
        availability_of(&view, AuthProvider::Google),
        AuthMethodAvailability::NotConfigured
    );
    assert_eq!(
        availability_of(&view, AuthProvider::Github),
        AuthMethodAvailability::Available
    );
}

#[test]
fn one_attempt_failing_does_not_withdraw_the_method_that_failed() {
    // Only NOT_CONFIGURED demotes a row. A cancelled sheet says nothing about
    // whether the method exists, and a button withdrawn for it would never
    // come back within the process.
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Google]);
    open_native_flow(&mut account, "flow-cancelled");
    assert!(account
        .accept_native_credential(
            &flow_id("flow-cancelled"),
            AccountAuthMethod::Google,
            NativeCredentialOutcome::Cancelled,
            20,
        )
        .is_ok());

    assert_eq!(
        availability_of(&project_account_view(&account), AuthProvider::Google),
        AuthMethodAvailability::Available
    );
}

#[test]
fn a_signed_in_view_still_carries_the_methods_it_could_offer() {
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Github]);
    account.restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: None,
        display_name: None,
    }));

    let view = project_account_view(&account);
    assert_eq!(view.phase, AuthPhase::SignedIn);
    assert_eq!(view.methods.len(), 4);
    assert_eq!(
        availability_of(&view, AuthProvider::Github),
        AuthMethodAvailability::Available
    );
}

#[test]
fn losing_the_core_owes_the_person_a_reason_rather_than_a_signed_out_screen() {
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Github]);
    open_flow(&mut account, "flow-lost", AccountAuthMethod::Github);
    account.require_reconciliation();

    let view = project_account_view(&account);
    assert_eq!(view.phase, AuthPhase::Failed);
    let failure = view.failure.unwrap_or_else(|| unreachable!());
    assert_eq!(failure.code, AuthFailureCode::CoreUnavailable);
    assert!(failure.retryable);
}

#[test]
fn a_signed_in_screen_says_who_is_signed_in() {
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Github]);
    account.restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: Some(AccountEmail::new("reader@example.test").unwrap_or_else(|_| unreachable!())),
        display_name: Some(AccountDisplayName::new("A Reader").unwrap_or_else(|_| unreachable!())),
    }));

    let view = project_account_view(&account);
    let account = view.account.unwrap_or_else(|| unreachable!());
    assert_eq!(account.account_id, "subject-1");
    assert_eq!(account.email.as_deref(), Some("reader@example.test"));
    assert_eq!(account.display_name.as_deref(), Some("A Reader"));
}

#[test]
fn an_account_the_provider_named_nothing_for_is_still_signed_in() {
    // Absent is not a gap to be filled. The screen has a phrase for an account
    // with no name; a fabricated one would be indistinguishable from a real
    // one and wrong.
    let mut account = ProductionAccount::with_available_methods(vec![AccountAuthMethod::Github]);
    account.restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: None,
        display_name: None,
    }));

    let view = project_account_view(&account);
    assert_eq!(view.phase, AuthPhase::SignedIn);
    let account = view.account.unwrap_or_else(|| unreachable!());
    assert!(account.email.is_none());
    assert!(account.display_name.is_none());
}
