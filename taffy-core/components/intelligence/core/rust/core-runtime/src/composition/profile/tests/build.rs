// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Constructing one profile generation, and refusing an untruthful one.

use std::rc::Rc;

use super::{all_account_methods, compiled_provider_count, configuration};
use crate::account::crypto::ReferenceSha256;
use crate::composition::profile::{create_profile_service_runtime, ProfileRuntimeBuildError};
use crate::contract::ServiceGeneration;

#[test]
fn canonical_profile_composition_has_no_default_or_missing_port() {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    let runtime = create_profile_service_runtime(
        configuration(
            ServiceGeneration::INITIAL,
            entropy,
            1_000,
            false,
            all_account_methods(),
        ),
        Rc::new(ReferenceSha256),
    );
    assert!(
        runtime.is_ok(),
        "canonical profile construction failed: {:?}",
        runtime.as_ref().err()
    );
}

#[test]
fn initial_profile_status_is_the_generated_ready_projection() {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    let runtime = create_profile_service_runtime(
        configuration(
            ServiceGeneration::INITIAL,
            entropy,
            1_000,
            false,
            all_account_methods(),
        ),
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!());
    let encoded = runtime
        .encode_ready_core_status()
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        encoded.schema_version,
        core_api_types::CORE_STATUS_PAYLOAD_SCHEMA_VERSION
    );
    let decoded = core_api_types::decode_core_status_payload(&encoded.payload)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        decoded.availability,
        core_api_types::CoreAvailability::Ready
    );
    assert_eq!(decoded.generation, ServiceGeneration::INITIAL.value());
    assert!(decoded.active_tasks.is_empty());
    assert!(encoded.task_revisions.is_empty());
    assert!(encoded.pending_approvals.is_empty());
    assert!(encoded.pending_permissions.is_empty());
    assert!(encoded.task_settlements.is_empty());
    // The provider roster rides the same payload, filled by the profile's
    // contributor from the compiled catalog: every catalog provider is a
    // row, nothing is stored yet, and the row's own switch survives
    // projection. Decoding it back is what proves the contributor is
    // registered — a projection the runtime alone produced would be empty
    // here.
    assert_eq!(decoded.provider_roster.len(), compiled_provider_count());
    assert!(decoded
        .provider_roster
        .iter()
        .all(|entry| entry.stored.is_none() && !entry.signing_in));
    let xai = decoded
        .provider_roster
        .iter()
        .find(|entry| entry.provider_id == "xai")
        .unwrap_or_else(|| unreachable!());
    assert_eq!(
        xai.auth_methods,
        vec![
            core_api_types::ProviderAuthMethodView::ApiKey,
            core_api_types::ProviderAuthMethodView::Oauth,
        ]
    );
    // Every compiled row ships open since decision 0125 turned the last one
    // on, so what this pins is that the switch is projected at all rather than
    // defaulted. It is weaker than the assertion it replaces, which read a
    // compiled `false` back out of a decoded payload: carrying `enabled:
    // false` through the projection is proved from here on only against the
    // synthetic row in `provider/tests.rs`, and against nothing that ships.
    assert!(
        decoded.provider_roster.iter().all(|entry| entry.enabled),
        "the row's own switch is a roster fact, and no compiled row ships shut"
    );
}

#[test]
fn unchanged_ready_publications_reuse_the_provider_projection() {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    let runtime = create_profile_service_runtime(
        configuration(
            ServiceGeneration::INITIAL,
            entropy,
            1_000,
            false,
            all_account_methods(),
        ),
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!());
    let revision = runtime.providers.status_projection().revision();
    let expected = runtime
        .encode_ready_core_status()
        .unwrap_or_else(|_| unreachable!())
        .payload;

    // A stream chunk and any other unrelated state publication call this
    // same encoder. Provider facts did not change, so none of these reads
    // may rebuild the roster, recount the catalog, or run the budget deal.
    for _ in 0..64 {
        let encoded = runtime
            .encode_ready_core_status()
            .unwrap_or_else(|_| unreachable!());
        assert_eq!(encoded.payload, expected);
        assert_eq!(runtime.providers.status_projection().revision(), revision);
    }
}

#[test]
fn duplicate_method_configuration_fails_closed() {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    assert!(matches!(
        create_profile_service_runtime(
            configuration(
                ServiceGeneration::INITIAL,
                entropy,
                1_000,
                false,
                vec![
                    crate::account::AccountAuthMethod::Google,
                    crate::account::AccountAuthMethod::Google,
                ],
            ),
            Rc::new(ReferenceSha256),
        ),
        Err(ProfileRuntimeBuildError::InvalidAccountMethods)
    ));
}

#[test]
fn zero_generation_and_degenerate_entropy_fail_closed() {
    assert!(matches!(
        create_profile_service_runtime(
            configuration(
                ServiceGeneration::new(0),
                [0; 32],
                0,
                false,
                all_account_methods()
            ),
            Rc::new(ReferenceSha256),
        ),
        Err(ProfileRuntimeBuildError::InvalidGeneration)
    ));
    assert!(matches!(
        create_profile_service_runtime(
            configuration(
                ServiceGeneration::INITIAL,
                [7; 32],
                0,
                false,
                all_account_methods(),
            ),
            Rc::new(ReferenceSha256),
        ),
        Err(ProfileRuntimeBuildError::InvalidGenerationEntropy)
    ));
}
