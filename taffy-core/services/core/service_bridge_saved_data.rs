// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Admission for bounded Chromium-owned saved-data projections.

use core_runtime::{wire, SavedDataSnapshotError};

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_saved_data_ffi::ffi as saved_ffi;
use crate::service_bridge_status::response_after_change;

#[allow(non_snake_case)]
pub(crate) fn SubmitSavedData(
    bridge: &mut ServiceBridge,
    command: saved_ffi::BridgeSavedDataCommand,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    if let Err(status) = validate_operation(
        &command.operation,
        bridge.generation.value(),
        now_monotonic_ms,
    ) {
        return response(&operation_id, status, Vec::new());
    }
    let Some(snapshot) = to_wire(command) else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    match runtime.replace_saved_data_snapshot(snapshot) {
        Ok(()) => response_after_change(
            bridge,
            response(
                &operation_id,
                wire::AdmissionStatus::Accepted as u8,
                Vec::new(),
            ),
        ),
        Err(SavedDataSnapshotError::StaleRevision) => response(
            &operation_id,
            wire::AdmissionStatus::StaleRevision as u8,
            Vec::new(),
        ),
        Err(
            SavedDataSnapshotError::PrivateProfile
            | SavedDataSnapshotError::TooManyRecords
            | SavedDataSnapshotError::InvalidRecord
            | SavedDataSnapshotError::InvalidAvailability,
        ) => response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        ),
    }
}

fn validate_operation(
    operation: &saved_ffi::BridgeSavedDataOperation,
    generation: u64,
    now_monotonic_ms: u64,
) -> Result<(), u8> {
    if operation.service_generation != generation {
        return Err(wire::AdmissionStatus::StaleGeneration as u8);
    }
    if operation.task_revision != 0
        || operation.operation_id.is_empty()
        || operation.operation_id.len() > wire::MAX_OPERATION_ID_BYTES
        || operation.idempotency_key.is_empty()
        || operation.idempotency_key.len() > wire::MAX_IDEMPOTENCY_KEY_BYTES
    {
        return Err(wire::AdmissionStatus::InvalidCommand as u8);
    }
    if now_monotonic_ms >= operation.deadline_monotonic_ms {
        return Err(wire::AdmissionStatus::DeadlineExceeded as u8);
    }
    Ok(())
}

fn to_wire(
    command: saved_ffi::BridgeSavedDataCommand,
) -> Option<wire::ReplaceSavedDataSnapshotCommand> {
    Some(wire::ReplaceSavedDataSnapshotCommand {
        sign_ins_availability: wire::SavedDataAvailability::from_wire(u32::from(
            command.sign_ins_availability,
        ))?,
        sign_ins_revision: command.sign_ins_revision,
        sign_ins: command
            .sign_ins
            .into_iter()
            .map(|record| wire::SavedSignInMetadata {
                id: record.id,
                site: record.site,
                username: record.username,
                last_used_epoch_ms: record.last_used_epoch_ms,
            })
            .collect(),
        details_availability: wire::SavedDataAvailability::from_wire(u32::from(
            command.details_availability,
        ))?,
        details_revision: command.details_revision,
        details: command
            .details
            .into_iter()
            .map(|record| wire::SavedDetailRecord {
                id: record.id,
                given_name: record.given_name,
                family_name: record.family_name,
                email: record.email,
                phone: record.phone,
                address: record.address,
                postcode: record.postcode,
                country: record.country,
            })
            .collect(),
    })
}
