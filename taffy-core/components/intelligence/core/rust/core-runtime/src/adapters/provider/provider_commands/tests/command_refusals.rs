// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Refusals about the command itself rather than about the plane's state —
//! the half of `ProviderServiceError` that is not `Domain`.

use core_service_types as wire;

use super::super::*;
use super::{command, plane};

#[test]
fn a_kind_naming_no_body_is_refused_without_touching_the_plane() {
    let mut plane = plane();
    for kind in [
        wire::CoreServiceCommandKind::SaveProviderCredential,
        wire::CoreServiceCommandKind::ForgetProviderCredential,
        wire::CoreServiceCommandKind::StartProviderAuth,
        wire::CoreServiceCommandKind::ProviderAuthCallback,
        wire::CoreServiceCommandKind::CancelProviderAuth,
        wire::CoreServiceCommandKind::SaveCustomProvider,
        wire::CoreServiceCommandKind::RemoveCustomProvider,
    ] {
        assert_eq!(
            apply_provider_command(&mut plane, &command(kind)),
            Err(ProviderServiceError::MissingBody),
            "{kind:?} carried no body and must be refused"
        );
    }
    assert!(plane.credentials().next().is_none());
}

#[test]
fn a_kind_this_seam_does_not_serve_is_refused() {
    let mut plane = plane();
    assert_eq!(
        apply_provider_command(
            &mut plane,
            &command(wire::CoreServiceCommandKind::StartTask)
        ),
        Err(ProviderServiceError::UnsupportedKind)
    );
}
