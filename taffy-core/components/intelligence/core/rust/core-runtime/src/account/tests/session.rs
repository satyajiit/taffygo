// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::fixtures::intent;
use crate::account::{
    classify_token_status, AccountAuthMethod, AccountDisplayName, AccountEffect, AccountEmail,
    AccountError, AccountProtocol, AccountSession, AccountSubjectId, SessionHandle, SessionReceipt,
    TokenHttpDisposition,
};
use crate::contract::Deadline;

#[test]
fn a_committed_session_cannot_be_replaced_by_a_new_sign_in_flow() {
    let session = AccountSession {
        session_handle: SessionHandle::new("canonical-session").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("canonical-subject")
            .unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(90_000),
        rotation: 3,
        email: None,
        display_name: None,
    };
    let mut protocol = AccountProtocol::restore_session(Some(session.clone()));

    assert_eq!(
        protocol.begin_authorization(intent("flow-cannot-replace"), 10),
        Err(AccountError::SessionAlreadyExists)
    );
    assert_eq!(protocol.session(), Some(&session));
    assert_eq!(protocol.pending_count(), 0);
}

#[test]
fn token_status_accepts_every_success_code() {
    for status in [200_u16, 201, 204, 226, 299] {
        assert_eq!(classify_token_status(status), TokenHttpDisposition::Success);
    }
    assert_eq!(classify_token_status(429), TokenHttpDisposition::Retryable);
}

#[test]
fn refresh_begins_before_expiry_and_serializes_rotation() {
    let mut protocol = AccountProtocol::restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: None,
        display_name: None,
    }));
    assert!(matches!(protocol.begin_refresh(39_999), Ok(None)));
    assert!(matches!(
        protocol.begin_refresh(40_000),
        Ok(Some(AccountEffect::Refresh(plan)))
            if plan.expected_rotation == 1
                && plan.expected_auth_method == AccountAuthMethod::Github
                && plan.expected_account_subject.as_str() == "subject-1"
    ));
    assert_eq!(
        protocol.begin_refresh(40_001),
        Err(AccountError::RefreshAlreadyInFlight)
    );
    let refreshed = protocol.accept_refresh(SessionReceipt {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(200_000),
        rotation: 1,
        email: None,
        display_name: None,
    });
    assert!(matches!(refreshed, Ok(session) if session.rotation == 1));
}

#[test]
fn refresh_receipt_must_preserve_account_identity() {
    let mut protocol = AccountProtocol::restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-before").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-before").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: None,
        display_name: None,
    }));
    assert!(matches!(
        protocol.begin_refresh(40_000),
        Ok(Some(AccountEffect::Refresh(_)))
    ));
    assert_eq!(
        protocol.accept_refresh(SessionReceipt {
            session_handle: SessionHandle::new("session-after").unwrap_or_else(|_| unreachable!()),
            auth_method: AccountAuthMethod::Github,
            account_subject: AccountSubjectId::new("different-subject")
                .unwrap_or_else(|_| unreachable!()),
            expires_at: Deadline::from_millis(200_000),
            rotation: 1,
            email: None,
            display_name: None,
        }),
        Err(AccountError::AccountSubjectMismatch)
    );
    assert_eq!(
        protocol
            .session()
            .map(|session| session.session_handle.opaque_id()),
        Some("session-before")
    );
}

#[test]
fn known_refresh_failure_retries_but_unknown_outcome_fences_the_generation() {
    let session = AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: None,
        display_name: None,
    };
    let mut retryable = AccountProtocol::restore_session(Some(session.clone()));
    assert!(matches!(
        retryable.begin_refresh(40_000),
        Ok(Some(AccountEffect::Refresh(_)))
    ));
    assert!(retryable.settle_refresh_failure(false));
    assert!(matches!(
        retryable.begin_refresh(40_001),
        Ok(Some(AccountEffect::Refresh(_)))
    ));

    let mut ambiguous = AccountProtocol::restore_session(Some(session));
    assert!(matches!(
        ambiguous.begin_refresh(40_000),
        Ok(Some(AccountEffect::Refresh(_)))
    ));
    assert!(ambiguous.settle_refresh_failure(true));
    assert!(ambiguous.session().is_none());
    assert!(ambiguous.reconciliation_required());
    assert_eq!(
        ambiguous.begin_refresh(40_001),
        Err(AccountError::ReconciliationRequired)
    );
    assert_eq!(
        ambiguous.begin_authorization(intent("flow-after-unknown"), 10),
        Err(AccountError::ReconciliationRequired)
    );
    assert_eq!(
        ambiguous.begin_sign_out(),
        Err(AccountError::ReconciliationRequired)
    );
}

#[test]
fn profile_bootstrap_restores_only_handle_based_session_state() {
    let session = AccountSession {
        session_handle: SessionHandle::new("persisted-session").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("persisted-subject")
            .unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(90_000),
        rotation: 4,
        email: None,
        display_name: None,
    };
    let protocol = AccountProtocol::restore_session(Some(session.clone()));
    assert_eq!(protocol.session(), Some(&session));
    assert_eq!(protocol.pending_count(), 0);
}

#[test]
fn a_rotation_that_repeats_no_identity_keeps_the_one_already_held() {
    // The provider is not obliged to repeat the user object on every rotation,
    // and a name that vanished an hour into a session would read as a defect in
    // the screen rather than in the response.
    let mut protocol = AccountProtocol::restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 1,
        email: Some(AccountEmail::new("reader@example.test").unwrap_or_else(|_| unreachable!())),
        display_name: Some(AccountDisplayName::new("A Reader").unwrap_or_else(|_| unreachable!())),
    }));
    assert!(protocol.begin_refresh(40_000).is_ok());

    let refreshed = protocol
        .accept_refresh(SessionReceipt {
            session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
            auth_method: AccountAuthMethod::Github,
            account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
            expires_at: Deadline::from_millis(200_000),
            rotation: 2,
            email: None,
            display_name: None,
        })
        .unwrap_or_else(|_| unreachable!());

    assert_eq!(refreshed.rotation, 2);
    assert_eq!(
        refreshed.email.as_ref().map(AccountEmail::as_str),
        Some("reader@example.test")
    );
    assert_eq!(
        refreshed
            .display_name
            .as_ref()
            .map(AccountDisplayName::as_str),
        Some("A Reader")
    );
}

#[test]
fn a_rotation_that_carries_a_new_identity_replaces_the_old_one() {
    let mut protocol = AccountProtocol::restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 1,
        email: Some(AccountEmail::new("old@example.test").unwrap_or_else(|_| unreachable!())),
        display_name: None,
    }));
    assert!(protocol.begin_refresh(40_000).is_ok());

    let refreshed = protocol
        .accept_refresh(SessionReceipt {
            session_handle: SessionHandle::new("session-handle").unwrap_or_else(|_| unreachable!()),
            auth_method: AccountAuthMethod::Github,
            account_subject: AccountSubjectId::new("subject-1").unwrap_or_else(|_| unreachable!()),
            expires_at: Deadline::from_millis(200_000),
            rotation: 2,
            email: Some(AccountEmail::new("new@example.test").unwrap_or_else(|_| unreachable!())),
            display_name: Some(
                AccountDisplayName::new("A Reader").unwrap_or_else(|_| unreachable!()),
            ),
        })
        .unwrap_or_else(|_| unreachable!());

    assert_eq!(
        refreshed.email.as_ref().map(AccountEmail::as_str),
        Some("new@example.test")
    );
    assert_eq!(
        refreshed
            .display_name
            .as_ref()
            .map(AccountDisplayName::as_str),
        Some("A Reader")
    );
}
