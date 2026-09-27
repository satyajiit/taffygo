// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The state report: registry fact about a record that exists (decision
//! 0078), and how route selection follows it.

use core_service_types as wire;
use model_router::CredentialState;

use crate::provider::ProviderError;

use super::super::*;
use super::{command, plane, save, set_state, CATALOG_ID};

#[test]
fn a_state_report_flips_the_stored_credential_and_route_selection_with_it() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &save(CATALOG_ID, "handle-1")).expect("accepted");
    let step = apply_provider_command(
        &mut plane,
        &set_state(CATALOG_ID, wire::ProviderCredentialState::NeedsSignIn),
    )
    .expect("accepted");
    assert!(step.state_changed, "a report republishes");
    assert_eq!(
        step.released_handle, None,
        "a report never touches material"
    );
    let view = plane
        .roster()
        .into_iter()
        .find(|entry| entry.provider_id.as_str() == CATALOG_ID)
        .expect("the catalog row");
    assert_eq!(
        view.stored.as_ref().map(|stored| stored.state),
        Some(CredentialState::NeedsSignIn)
    );
    // The kernel's one provider question stops answering, which is what makes
    // route selection refuse with CredentialNeedsAttention and no fallback.
    let router_id = model_router::ProviderId::new(CATALOG_ID).expect("valid");
    assert!(
        loop_kernel::provider::ProviderDirectory::usable_credential(&plane, &router_id).is_none(),
        "a credential needing sign-in must not be spendable"
    );
}

#[test]
fn a_recovered_refresh_reports_usable_again() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &save(CATALOG_ID, "handle-1")).expect("accepted");
    apply_provider_command(
        &mut plane,
        &set_state(CATALOG_ID, wire::ProviderCredentialState::RefreshFailed),
    )
    .expect("accepted");
    apply_provider_command(
        &mut plane,
        &set_state(CATALOG_ID, wire::ProviderCredentialState::Usable),
    )
    .expect("accepted");
    let router_id = model_router::ProviderId::new(CATALOG_ID).expect("valid");
    assert!(
        loop_kernel::provider::ProviderDirectory::usable_credential(&plane, &router_id).is_some(),
        "a recovered credential is spendable again"
    );
}

#[test]
fn a_state_report_about_nothing_is_refused_rather_than_resurrecting_a_record() {
    // The reporter lost a race with a forget. The in-section re-read decision
    // 0078 requires browser-side makes this rare; this refusal is its backstop.
    let mut plane = plane();
    let refused = apply_provider_command(
        &mut plane,
        &set_state(CATALOG_ID, wire::ProviderCredentialState::Usable),
    )
    .expect_err("refused");
    assert_eq!(
        refused,
        ProviderServiceError::Domain(ProviderError::UnknownProvider)
    );
}

#[test]
fn a_state_report_with_no_body_is_refused() {
    let mut plane = plane();
    let refused = apply_provider_command(
        &mut plane,
        &command(wire::CoreServiceCommandKind::SetProviderCredentialState),
    )
    .expect_err("refused");
    assert_eq!(refused, ProviderServiceError::MissingBody);
}
