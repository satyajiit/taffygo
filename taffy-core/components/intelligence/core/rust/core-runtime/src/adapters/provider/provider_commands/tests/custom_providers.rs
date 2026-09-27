// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A person's own provider: defined by one write and removed by another.

use model_router::catalog::Endpoint;

use crate::provider::{CredentialHandle, ProviderError};

use super::super::*;
use super::{plane, remove_custom, save_custom, CATALOG_ID, OWN_ID};

#[test]
fn a_persons_own_provider_is_defined_by_one_write() {
    let mut plane = plane();
    let step = apply_provider_command(
        &mut plane,
        &save_custom(OWN_ID, "https://gw.example/v1", Some("handle-1")),
    )
    .expect("accepted");
    assert!(step.state_changed);
    assert_eq!(
        plane.custom_endpoints().first().map(Endpoint::as_str),
        Some("https://gw.example/v1")
    );
    assert_eq!(
        plane
            .credentials()
            .map(|entry| entry.provider_id.as_str())
            .collect::<Vec<_>>(),
        vec![OWN_ID],
        "the credential is filed in the same write that defined the provider"
    );
}

#[test]
fn a_persons_own_provider_may_not_take_a_catalog_identity() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(
            &mut plane,
            &save_custom(CATALOG_ID, "https://gw.example/v1", None)
        ),
        Err(ProviderServiceError::Domain(
            ProviderError::ProviderIdReserved
        ))
    );
}

#[test]
fn an_endpoint_that_is_not_an_address_is_refused_by_name() {
    // Cleartext is not one of these. `http://localhost:11434/v1` is the
    // address decision 0096 was written for, and which addresses plain http
    // may reach is settled in the browser against the register it holds rather
    // than here — see the save's own suite in `custom_provider.rs`.
    let mut plane = plane();
    for endpoint in ["https:///v1", "https://a@b/v1", "ftp://gw.example/v1"] {
        assert_eq!(
            apply_provider_command(&mut plane, &save_custom(OWN_ID, endpoint, None)),
            Err(ProviderServiceError::Domain(ProviderError::InvalidEndpoint)),
            "{endpoint} should be refused"
        );
    }
}

#[test]
fn removing_a_persons_own_provider_discards_its_credential_too() {
    let mut plane = plane();
    apply_provider_command(
        &mut plane,
        &save_custom(OWN_ID, "https://gw.example/v1", Some("handle-1")),
    )
    .expect("defined");
    let step = apply_provider_command(&mut plane, &remove_custom(OWN_ID)).expect("accepted");
    assert_eq!(
        step.released_handle.as_ref().map(CredentialHandle::as_str),
        Some("handle-1")
    );
    assert!(plane.custom_providers().next().is_none());
    assert!(plane.credentials().next().is_none());
}

#[test]
fn removing_a_provider_that_was_never_defined_is_refused_by_name() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(&mut plane, &remove_custom(OWN_ID)),
        Err(ProviderServiceError::Domain(ProviderError::UnknownProvider))
    );
}
