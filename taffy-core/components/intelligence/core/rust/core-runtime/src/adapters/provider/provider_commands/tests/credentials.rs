// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A key filed, replaced and forgotten, and every save the plane refuses.

use model_router::CredentialState;

use crate::provider::{CredentialHandle, ProviderError, ProviderOrigin};

use super::super::*;
use super::{forget, plane, save, save_custom, CATALOG_ID, OWN_ID};

#[test]
fn a_saved_credential_reaches_the_plane_and_changes_state() {
    let mut plane = plane();
    let step = apply_provider_command(&mut plane, &save(CATALOG_ID, "handle-1")).expect("accepted");
    assert!(step.state_changed);
    assert_eq!(step.released_handle, None, "nothing was displaced");
    let view = plane
        .roster()
        .into_iter()
        .find(|entry| entry.provider_id.as_str() == CATALOG_ID)
        .expect("the catalog row");
    assert_eq!(
        view.stored.as_ref().map(|stored| stored.state),
        Some(CredentialState::Usable)
    );
}

#[test]
fn replacing_a_credential_names_the_handle_it_displaced() {
    let mut plane = plane();
    apply_provider_command(&mut plane, &save(CATALOG_ID, "handle-1")).expect("accepted");
    let step = apply_provider_command(&mut plane, &save(CATALOG_ID, "handle-2")).expect("accepted");
    assert_eq!(
        step.released_handle.as_ref().map(CredentialHandle::as_str),
        Some("handle-1"),
        "the browser owns the material behind the handle it replaced"
    );
}

#[test]
fn saving_the_same_handle_twice_displaces_nothing() {
    // Releasing here would revoke a credential that is still in use, which is
    // why the plane compares before it inserts.
    let mut plane = plane();
    apply_provider_command(&mut plane, &save(CATALOG_ID, "handle-1")).expect("accepted");
    let step = apply_provider_command(&mut plane, &save(CATALOG_ID, "handle-1")).expect("accepted");
    assert_eq!(step.released_handle, None);
}

#[test]
fn a_credential_for_an_unknown_provider_is_refused_by_name() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &save("nobody", "handle-1")),
        Err(ProviderServiceError::Domain(ProviderError::UnknownProvider))
    );
}

#[test]
fn a_credential_for_a_switched_off_provider_is_refused_by_name() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &save("switched-off", "handle-1")),
        Err(ProviderServiceError::Domain(
            ProviderError::ProviderDisabled
        ))
    );
}

#[test]
fn a_key_for_a_plan_only_provider_is_refused_by_name() {
    // Filing it would create a record no route can ever spend, and the person
    // would be told the key was saved.
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &save("plan-only", "handle-1")),
        Err(ProviderServiceError::Domain(
            ProviderError::MethodNotOffered
        ))
    );
}

#[test]
fn an_identity_the_platform_store_cannot_hold_is_refused_before_any_write() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &save("Anthropic", "handle-1")),
        Err(ProviderServiceError::Domain(
            ProviderError::InvalidProviderId
        ))
    );
    assert!(
        plane.credentials().next().is_none(),
        "a refused command must leave the plane untouched"
    );
}

#[test]
fn an_empty_handle_is_refused_rather_than_stored() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &save(CATALOG_ID, "")),
        Err(ProviderServiceError::Domain(
            ProviderError::EmptyCredentialHandle
        ))
    );
}

#[test]
fn forgetting_a_credential_leaves_the_provider_defined() {
    let mut plane = plane();
    apply_provider_command(
        &mut plane,
        &save_custom(OWN_ID, "https://gw.example/v1", None),
    )
    .expect("defined");
    apply_provider_command(&mut plane, &save(OWN_ID, "handle-1")).expect("accepted");
    let step = apply_provider_command(&mut plane, &forget(OWN_ID)).expect("accepted");
    assert_eq!(
        step.released_handle.as_ref().map(CredentialHandle::as_str),
        Some("handle-1")
    );
    let view = plane
        .roster()
        .into_iter()
        .find(|entry| entry.provider_id.as_str() == OWN_ID)
        .expect("the provider is still defined");
    assert_eq!(view.origin, ProviderOrigin::Custom);
    assert_eq!(view.stored, None);
}

#[test]
fn forgetting_a_credential_that_is_not_held_is_refused_by_name() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &forget(CATALOG_ID)),
        Err(ProviderServiceError::Domain(ProviderError::UnknownProvider))
    );
}
