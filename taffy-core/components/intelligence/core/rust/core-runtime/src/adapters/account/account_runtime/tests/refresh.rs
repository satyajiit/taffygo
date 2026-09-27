// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Session-refresh latching and unknown-outcome tests.

use super::*;
use crate::account::{
    AccountAuthMethod, AccountEffect, AccountSession, AccountSubjectId, SessionHandle,
};
use crate::contract::Deadline;

fn refresh_account() -> ProductionAccount {
    let mut account = ProductionAccount::new();
    account.restore_session(Some(AccountSession {
        session_handle: SessionHandle::new("session-before").unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        account_subject: AccountSubjectId::new("subject-before").unwrap_or_else(|_| unreachable!()),
        expires_at: Deadline::from_millis(100_000),
        rotation: 0,
        email: None,
        display_name: None,
    }));
    account
}

fn track_refresh(
    runtime: &mut AccountOperationRuntime,
    account: &mut dyn AccountPort,
) -> wire::EffectEnvelope {
    let effect = account
        .begin_refresh(40_000)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!());
    assert!(matches!(effect, AccountEffect::Refresh(_)));
    runtime
        .track_effect(operation(), effect, &TestDigest)
        .unwrap_or_else(|_| unreachable!())
}

fn refresh_terminal(
    effect: &wire::EffectEnvelope,
    status: wire::EffectStatus,
) -> wire::EffectResult {
    wire::EffectResult {
        operation: effect.operation.clone(),
        effect_id: effect.effect_id.clone(),
        status,
        kind: wire::EffectKind::NetworkRequest,
        storage: None,
        observation: None,
        model: None,
        network: Some(wire::NetworkEffectResult {
            operation_kind: wire::AccountNetworkOperation::RefreshSession,
            authorization_code_session: None,
            native_credential_session: None,
            email_link: None,
            refreshed_session: Some(wire::AccountSessionReceipt {
                session_handle: "session-after".to_owned(),
                account_subject: "subject-before".to_owned(),
                expires_at_monotonic_ms: 200_000,
                rotation: 1,
                auth_method: wire::AccountAuthMethod::Github,
                email: None,
                display_name: None,
            }),
            revoked_session: None,
            entitlement_summary: None,
        }),
        browser_action: None,
        tool: None,
        secure_store: None,
        auth_surface: None,
        permission: None,
        asset_delivery: None,
        catalog: None,
        provider_listing: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    }
}

#[test]
fn failed_and_cancelled_refreshes_unlock_an_explicit_retry() {
    for status in [
        wire::EffectStatus::Unavailable,
        wire::EffectStatus::Cancelled,
    ] {
        let mut runtime = AccountOperationRuntime::new();
        let mut account = refresh_account();
        let effect = track_refresh(&mut runtime, &mut account);

        assert!(runtime
            .deliver(
                &mut account,
                ServiceGeneration::INITIAL,
                &refresh_terminal(&effect, status),
                40_001,
                &TestDigest,
            )
            .is_ok());
        assert!(matches!(
            account.begin_refresh(40_002),
            Ok(Some(AccountEffect::Refresh(_)))
        ));
    }
}

#[test]
fn unknown_refresh_outcome_cannot_be_replayed() {
    let mut runtime = AccountOperationRuntime::new();
    let mut account = refresh_account();
    let effect = track_refresh(&mut runtime, &mut account);

    assert!(runtime
        .deliver(
            &mut account,
            ServiceGeneration::INITIAL,
            &refresh_terminal(&effect, wire::EffectStatus::OutcomeUnknown),
            40_001,
            &TestDigest,
        )
        .is_ok());
    assert!(account.session().is_none());
    assert!(account.reconciliation_required());
    assert_eq!(
        account.begin_refresh(40_002),
        Err(crate::account::AccountError::ReconciliationRequired)
    );

    let mut later = start_auth();
    later.operation.operation_id = "account-op-after-unknown".to_owned();
    later.operation.idempotency_key = "account-key-after-unknown".to_owned();
    assert_eq!(
        runtime.submit(
            &mut account,
            ServiceGeneration::INITIAL,
            &later,
            40_002,
            &TestDigest,
        ),
        Err(AccountServiceError::Domain(
            crate::account::AccountError::ReconciliationRequired,
        ))
    );
}

#[test]
fn cancelling_runtime_settles_refresh_latch() {
    let mut runtime = AccountOperationRuntime::new();
    let mut account = refresh_account();
    let _ = track_refresh(&mut runtime, &mut account);

    runtime.cancel_all(&mut account);

    assert!(matches!(
        account.begin_refresh(40_001),
        Ok(Some(AccountEffect::Refresh(_)))
    ));
}
