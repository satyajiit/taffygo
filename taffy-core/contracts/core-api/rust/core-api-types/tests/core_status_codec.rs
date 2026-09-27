// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The bounded Core Status codec against its golden, its limits and its closed enums.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use common::{empty_status, golden_payload, status_fixture};
use core_api_types::{
    decode_core_status_payload, encode_core_status_payload, measure_core_status_payload, AuthPhase,
    AuthViewState, CoreStatusPayloadCodecError, CoreStatusProjectionFamily,
    CoreStatusProjectionMode, CoreStatusProjectionOmission, CORE_STATUS_PAYLOAD_MAGIC,
    CORE_STATUS_PAYLOAD_SCHEMA_VERSION, MAX_ACTIVE_TASKS, MAX_EVENT_PAYLOAD_BYTES,
    MAX_TASK_GOAL_BYTES,
};

#[test]
fn golden_payload_round_trips_the_cross_language_fixture() {
    let status = status_fixture();
    let encoded = encode_core_status_payload(&status).unwrap_or_default();
    assert_eq!(encoded, golden_payload());
    assert_eq!(measure_core_status_payload(&status), Ok(encoded.len()));
    assert_eq!(decode_core_status_payload(&encoded), Ok(status.clone()));
    assert_eq!(
        encode_core_status_payload(&status),
        encode_core_status_payload(&status)
    );
}

#[test]
fn closed_enum_version_and_trailing_bytes_fail_exactly() {
    let mut unknown = encode_core_status_payload(&empty_status()).unwrap_or_default();
    let availability = CORE_STATUS_PAYLOAD_MAGIC.len() + 4;
    unknown[availability..availability + 4].copy_from_slice(&u32::MAX.to_le_bytes());
    assert_eq!(
        decode_core_status_payload(&unknown),
        Err(CoreStatusPayloadCodecError::InvalidEnum)
    );

    let mut version = encode_core_status_payload(&empty_status()).unwrap_or_default();
    let version_offset = CORE_STATUS_PAYLOAD_MAGIC.len();
    version[version_offset..version_offset + 4].copy_from_slice(
        &CORE_STATUS_PAYLOAD_SCHEMA_VERSION
            .saturating_add(1)
            .to_le_bytes(),
    );
    assert_eq!(
        decode_core_status_payload(&version),
        Err(CoreStatusPayloadCodecError::UnsupportedVersion)
    );

    let mut trailing = encode_core_status_payload(&empty_status()).unwrap_or_default();
    trailing.push(0);
    assert_eq!(
        decode_core_status_payload(&trailing),
        Err(CoreStatusPayloadCodecError::TrailingBytes)
    );
}

#[test]
fn payload_collection_and_field_limits_fail_closed() {
    assert_eq!(
        decode_core_status_payload(&vec![0; MAX_EVENT_PAYLOAD_BYTES + 1]),
        Err(CoreStatusPayloadCodecError::SizeLimit)
    );

    let mut too_many = encode_core_status_payload(&empty_status()).unwrap_or_default();
    let count_offset = CORE_STATUS_PAYLOAD_MAGIC.len() + 4 + 4 + 8;
    let count = u32::try_from(MAX_ACTIVE_TASKS + 1).unwrap_or(u32::MAX);
    too_many[count_offset..count_offset + 4].copy_from_slice(&count.to_le_bytes());
    assert_eq!(
        decode_core_status_payload(&too_many),
        Err(CoreStatusPayloadCodecError::CollectionLimit)
    );

    let mut oversized_goal = status_fixture();
    oversized_goal.active_tasks[0].goal = "x".repeat(MAX_TASK_GOAL_BYTES + 1);
    assert_eq!(
        encode_core_status_payload(&oversized_goal),
        Err(CoreStatusPayloadCodecError::StringLimit)
    );
}

#[test]
fn malformed_account_projection_is_not_serialized() {
    let mut malformed = empty_status();
    malformed.auth_state = Some(AuthViewState {
        phase: AuthPhase::SignedIn,
        account: None,
        pending_email: None,
        failure: None,
        methods: Vec::new(),
        entitlement: None,
    });
    assert_eq!(
        encode_core_status_payload(&malformed),
        Err(CoreStatusPayloadCodecError::Malformed)
    );
}

#[test]
fn ordered_projection_metadata_fails_closed() {
    let mut malformed = empty_status();
    malformed.builtin_skills.swap(0, 1);
    assert_eq!(
        measure_core_status_payload(&malformed),
        Err(CoreStatusPayloadCodecError::Malformed)
    );

    let mut complete = empty_status();
    complete
        .projection_omissions
        .push(CoreStatusProjectionOmission {
            family: CoreStatusProjectionFamily::ActiveTasks,
            revision: 0,
            item_count: 0,
        });
    assert_eq!(
        encode_core_status_payload(&complete),
        Err(CoreStatusPayloadCodecError::Malformed)
    );

    let mut incomplete = empty_status();
    incomplete.projection_mode = CoreStatusProjectionMode::RecoveryRequired;
    assert_eq!(
        encode_core_status_payload(&incomplete),
        Err(CoreStatusPayloadCodecError::Malformed)
    );
}
