// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One subscription sign-in: admitted on the plane's own facts, run in the
//! browser, and closed by the terminal it reports back (decision 0078).

use core_service_types as wire;

use crate::provider::ProviderError;

use super::super::*;
use super::{auth_callback, cancel_auth, command, plane, start_auth, CATALOG_ID};

#[test]
fn a_subscription_sign_in_is_admitted_on_the_planes_own_facts() {
    // The refusal order is the point: a malformed request and a well-formed
    // one must not receive the same answer, or nobody can tell a typo from a
    // vendor that offers no plan.
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &start_auth("Anthropic")),
        Err(ProviderServiceError::Domain(
            ProviderError::InvalidProviderId
        ))
    );
    assert_eq!(
        apply_provider_command(&mut plane, &start_auth(CATALOG_ID)),
        Err(ProviderServiceError::Domain(
            ProviderError::MethodNotOffered
        )),
        "a key-only vendor has no subscription to sign in to"
    );
    let step = apply_provider_command(&mut plane, &start_auth("plan-only"))
        .expect("an offered, enabled vendor admits a sign-in");
    assert!(step.state_changed, "signing_in flips on the roster");
    let signing_in = plane
        .roster()
        .into_iter()
        .find(|row| row.provider_id.as_str() == "plan-only")
        .expect("the row exists");
    assert!(signing_in.signing_in);
}

#[test]
fn a_second_sign_in_for_the_same_provider_is_refused_while_one_is_pending() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &start_auth("plan-only")).expect("admitted");
    let mut second = start_auth("plan-only");
    if let Some(body) = second.start_provider_auth.as_mut() {
        body.flow_id = "flow-2".to_owned();
    }
    assert_eq!(
        apply_provider_command(&mut plane, &second),
        Err(ProviderServiceError::Domain(
            ProviderError::FlowAlreadyRunning
        ))
    );
}

#[test]
fn a_terminal_clears_the_pending_flow_whatever_it_reports() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &start_auth("plan-only")).expect("admitted");
    let step = apply_provider_command(
        &mut plane,
        &auth_callback(
            "flow-1",
            "binding-1",
            wire::AuthCallbackStatus::Denied,
            None,
        ),
    )
    .expect("a denial is a terminal, not an error");
    assert!(step.state_changed);
    let row = plane
        .roster()
        .into_iter()
        .find(|row| row.provider_id.as_str() == "plan-only")
        .expect("the row exists");
    assert!(!row.signing_in, "the pending marker is cleared");
    // The flow is over: the same terminal again names no pending flow.
    assert_eq!(
        apply_provider_command(
            &mut plane,
            &auth_callback(
                "flow-1",
                "binding-1",
                wire::AuthCallbackStatus::Denied,
                None,
            ),
        ),
        Err(ProviderServiceError::Domain(ProviderError::UnknownFlow))
    );
}

#[test]
fn cancel_removes_only_the_exact_live_flow_and_publishes() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &start_auth("plan-only")).expect("admitted");

    assert_eq!(
        apply_provider_command(&mut plane, &cancel_auth("flow-other")),
        Err(ProviderServiceError::Domain(ProviderError::UnknownFlow)),
        "an unrelated identity cannot cancel the live flow"
    );
    assert!(
        plane
            .roster()
            .into_iter()
            .find(|row| row.provider_id.as_str() == "plan-only")
            .expect("row")
            .signing_in,
        "a refused cancellation leaves the live flow intact"
    );

    let step = apply_provider_command(&mut plane, &cancel_auth("flow-1"))
        .expect("the exact live flow is cancelled");
    assert!(step.state_changed);
    assert!(
        !plane
            .roster()
            .into_iter()
            .find(|row| row.provider_id.as_str() == "plan-only")
            .expect("row")
            .signing_in
    );
    assert_eq!(
        apply_provider_command(&mut plane, &cancel_auth("flow-1")),
        Err(ProviderServiceError::Domain(ProviderError::UnknownFlow)),
        "cancellation is terminal and cannot be replayed"
    );
}

#[test]
fn cancel_refuses_empty_and_oversized_identities_before_state_changes() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &start_auth("plan-only")).expect("admitted");

    for flow_id in [
        String::new(),
        "f".repeat(wire::MAX_IDENTIFIER_BYTES.saturating_add(1)),
    ] {
        assert_eq!(
            apply_provider_command(&mut plane, &cancel_auth(&flow_id)),
            Err(ProviderServiceError::MissingBody),
            "a malformed exact identity must not reach the provider plane"
        );
    }
    assert!(
        plane
            .roster()
            .into_iter()
            .find(|row| row.provider_id.as_str() == "plan-only")
            .expect("row")
            .signing_in,
        "malformed cancellations leave the admitted flow intact"
    );
}

#[test]
fn cancel_and_terminal_are_first_terminal_wins() {
    let mut cancelled_first = plane();
    apply_provider_command(&mut cancelled_first, &start_auth("plan-only")).expect("admitted");
    apply_provider_command(&mut cancelled_first, &cancel_auth("flow-1")).expect("cancelled");
    assert_eq!(
        apply_provider_command(
            &mut cancelled_first,
            &auth_callback(
                "flow-1",
                "binding-1",
                wire::AuthCallbackStatus::Denied,
                None,
            ),
        ),
        Err(ProviderServiceError::Domain(ProviderError::UnknownFlow)),
        "a late terminal cannot revive an accepted cancellation"
    );

    let mut terminal_first = plane();
    apply_provider_command(&mut terminal_first, &start_auth("plan-only")).expect("admitted");
    apply_provider_command(
        &mut terminal_first,
        &auth_callback(
            "flow-1",
            "binding-1",
            wire::AuthCallbackStatus::Denied,
            None,
        ),
    )
    .expect("terminal");
    assert_eq!(
        apply_provider_command(&mut terminal_first, &cancel_auth("flow-1")),
        Err(ProviderServiceError::Domain(ProviderError::UnknownFlow)),
        "a cancellation that loses the race cannot cancel a completed flow"
    );
}

#[test]
fn a_callback_for_a_flow_nobody_started_is_refused_by_name() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(
            &mut plane,
            &auth_callback(
                "flow-9",
                "binding-1",
                wire::AuthCallbackStatus::Denied,
                None,
            ),
        ),
        Err(ProviderServiceError::Domain(ProviderError::UnknownFlow))
    );
}

#[test]
fn a_callback_with_someone_elses_binding_is_not_this_flow() {
    // The identity the flow was admitted under is the pair, so a live flow id
    // arriving with the wrong binding is a callback for a flow that was never
    // started — not a live flow with a detail wrong.
    let mut plane = plane();
    apply_provider_command(&mut plane, &start_auth("plan-only")).expect("admitted");
    assert_eq!(
        apply_provider_command(
            &mut plane,
            &auth_callback(
                "flow-1",
                "binding-2",
                wire::AuthCallbackStatus::Denied,
                None,
            ),
        ),
        Err(ProviderServiceError::Domain(ProviderError::UnknownFlow))
    );
    let row = plane
        .roster()
        .into_iter()
        .find(|row| row.provider_id.as_str() == "plan-only")
        .expect("the row exists");
    assert!(row.signing_in, "the real flow is still pending");
}

#[test]
fn a_terminal_whose_outcome_and_code_handle_disagree_is_malformed() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &start_auth("plan-only")).expect("admitted");
    assert_eq!(
        apply_provider_command(
            &mut plane,
            &auth_callback(
                "flow-1",
                "binding-1",
                wire::AuthCallbackStatus::AuthorizationCode,
                None,
            ),
        ),
        Err(ProviderServiceError::Domain(
            ProviderError::MalformedRedirect
        )),
        "success without a code handle is a contradiction, not a success"
    );
    assert_eq!(
        apply_provider_command(
            &mut plane,
            &auth_callback(
                "flow-1",
                "binding-1",
                wire::AuthCallbackStatus::Denied,
                Some("transient-1"),
            ),
        ),
        Err(ProviderServiceError::Domain(
            ProviderError::MalformedRedirect
        )),
        "a denial carrying a code handle is the same contradiction"
    );
    let row = plane
        .roster()
        .into_iter()
        .find(|row| row.provider_id.as_str() == "plan-only")
        .expect("the row exists");
    assert!(row.signing_in, "a malformed redirect ends nothing");
}

#[test]
fn a_completed_sign_in_files_as_an_ordinary_oauth_save() {
    // The exchange runs in the browser (decision 0078); what the plane sees
    // afterwards is a save whose method is OAUTH and whose handle is the
    // provider-id-keyed sealed record.
    let mut plane = plane();
    apply_provider_command(&mut plane, &start_auth("plan-only")).expect("admitted");
    apply_provider_command(
        &mut plane,
        &auth_callback(
            "flow-1",
            "binding-1",
            wire::AuthCallbackStatus::AuthorizationCode,
            Some("transient-1"),
        ),
    )
    .expect("the terminal is accepted");
    let mut save = command(wire::CoreServiceCommandKind::SaveProviderCredential);
    save.save_provider_credential = Some(wire::SaveProviderCredentialCommand {
        provider_id: "plan-only".to_owned(),
        auth_method: wire::ProviderAuthMethod::Oauth,
        credential_handle: "plan-only".to_owned(),
    });
    apply_provider_command(&mut plane, &save).expect("the OAUTH save is offered");
    let row = plane
        .roster()
        .into_iter()
        .find(|row| row.provider_id.as_str() == "plan-only")
        .expect("the row exists");
    let stored = row.stored.expect("a credential is filed");
    assert!(stored.subscription_backed);
    assert!(!row.signing_in);
}
