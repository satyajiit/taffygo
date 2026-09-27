// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Delivery-plane commands, effects and reports across the CXX boundary.
//!
//! Three commands come in — the connection changed, a person asked for an
//! asset, a person removed one — and every one of them ends by asking the
//! plane what should happen next. Planning is pure and answers the same thing
//! twice at the same instant, so re-planning after each command is not a
//! second decision; it is the one decision, taken again with newer facts.

use core_runtime::wire;

use crate::ffi;
use crate::service_bridge_runtime::{response, ChromiumDigest, ServiceBridge};
use crate::service_bridge_status::response_after_change;

/// Answers the delivery effects the plane wants carried out right now.
///
/// The operation each effect carries is the one that caused the plan to be
/// taken again. That is what lets the browser journal an effect against a
/// command, and it is why an effect id is derived from the operation rather
/// than minted here: the core has no entropy of its own and a counter would be
/// a second thing to keep across a process death.
fn planned_effects(
    bridge: &ServiceBridge,
    operation: &wire::OperationEnvelope,
    now_monotonic_ms: u64,
) -> Vec<ffi::BridgeAssetEffect> {
    let Some(runtime) = bridge.runtime.as_ref() else {
        return Vec::new();
    };
    runtime
        .core()
        .plan_asset_delivery(now_monotonic_ms)
        .into_iter()
        .filter_map(|effect| asset_effect_to_ffi(operation, runtime.core(), &effect))
        .collect()
}

fn asset_effect_to_ffi(
    operation: &wire::OperationEnvelope,
    core: &core_runtime::CoreRuntime,
    effect: &wire::AssetDeliveryEffect,
) -> Option<ffi::BridgeAssetEffect> {
    // The identity names content and attempt, not a catalog or plan position.
    // `attempts` counts completed transfers, so adding one stays stable while
    // an exact attempt is in flight and advances even after a zero-byte
    // failure whose offset did not change.
    let (asset_id, asset_revision) = match effect.operation_kind {
        wire::AssetDeliveryOperation::FetchAsset => {
            let fetch = effect.fetch.as_ref()?;
            (fetch.asset_id.as_str(), fetch.asset_revision.as_str())
        }
        wire::AssetDeliveryOperation::RemoveAsset => {
            let remove = effect.remove.as_ref()?;
            (remove.asset_id.as_str(), remove.asset_revision.as_str())
        }
    };
    let attempt = match effect.operation_kind {
        wire::AssetDeliveryOperation::FetchAsset => core
            .asset_completed_attempts(asset_id, asset_revision)?
            .checked_add(1)?,
        wire::AssetDeliveryOperation::RemoveAsset => 1,
    };
    let effect_id = core_runtime::effect_identity::asset_effect_identity(
        &ChromiumDigest,
        &operation.operation_id,
        asset_id,
        asset_revision,
        effect.operation_kind as u32,
        attempt,
    )
    .ok()?;
    let mut projected = ffi::BridgeAssetEffect {
        operation: ffi::BridgeOperation {
            operation_id: operation.operation_id.clone(),
            service_generation: operation.service_generation,
            task_revision: 0,
            deadline_monotonic_ms: operation.deadline_monotonic_ms,
            idempotency_key: operation.idempotency_key.clone(),
        },
        effect_id,
        operation_kind: effect.operation_kind as u8,
        asset_id: String::new(),
        asset_revision: String::new(),
        origin_path: String::new(),
        offset_bytes: 0,
        total_bytes: 0,
        expected_digest: [0u8; 32],
        container: 0,
    };
    match effect.operation_kind {
        wire::AssetDeliveryOperation::FetchAsset => {
            let fetch = effect.fetch.as_ref()?;
            projected.asset_id.clone_from(&fetch.asset_id);
            projected.asset_revision.clone_from(&fetch.asset_revision);
            projected.origin_path.clone_from(&fetch.origin_path);
            projected.offset_bytes = fetch.offset_bytes;
            projected.total_bytes = fetch.total_bytes;
            projected.expected_digest = fetch.expected_digest;
            projected.container = fetch.container as u8;
        }
        wire::AssetDeliveryOperation::RemoveAsset => {
            let remove = effect.remove.as_ref()?;
            projected.asset_id.clone_from(&remove.asset_id);
            projected.asset_revision.clone_from(&remove.asset_revision);
        }
    }
    Some(projected)
}

/// The delivery effects a fresh generation starts with.
///
/// The operation is synthetic because no person or task asked: a start-up
/// fetch is a consequence of the catalog and the disk disagreeing. Both
/// browser session and generation identify the live incarnation that planned
/// it; generation alone restarts at one on every browser launch.
pub(crate) fn initial_effects(
    bridge: &ServiceBridge,
    now_monotonic_ms: u64,
) -> Vec<ffi::BridgeAssetEffect> {
    let generation = bridge.generation.value();
    let Some(runtime) = bridge.runtime.as_ref() else {
        return Vec::new();
    };
    let Ok((operation_id, idempotency_key)) =
        core_runtime::effect_identity::startup_asset_identities(
            &ChromiumDigest,
            runtime.browser_session_id().as_str(),
            generation,
        )
    else {
        return Vec::new();
    };
    let operation = wire::OperationEnvelope {
        operation_id,
        service_generation: generation,
        task_revision: 0,
        deadline_monotonic_ms: 0,
        idempotency_key,
    };
    planned_effects(bridge, &operation, now_monotonic_ms)
}

/// Adopts the browser's scan of the profile's asset store. Bootstrap only.
pub(crate) fn restore_assets(
    runtime: &mut core_runtime::ProfileServiceRuntime,
    platform: u8,
    found: Vec<ffi::BridgeAssetOnDisk>,
) -> bool {
    let Some(platform) = wire::AssetPlatform::from_wire(u32::from(platform)) else {
        return false;
    };
    runtime.core_mut().set_asset_platform(platform);
    let mut records = Vec::with_capacity(found.len());
    for record in found {
        let Some(presence) = wire::AssetPresence::from_wire(u32::from(record.presence)) else {
            return false;
        };
        records.push(wire::AssetOnDisk {
            asset_id: record.asset_id,
            asset_revision: record.asset_revision,
            presence,
            written_bytes: record.written_bytes,
        });
    }
    runtime.core_mut().restore_assets(&records).is_ok()
}

#[allow(non_snake_case)]
pub(crate) fn SubmitAsset(
    bridge: &mut ServiceBridge,
    command: ffi::BridgeAssetCommand,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    let operation = operation_to_wire(command.operation);
    if let Err(status) = validate_operation(&operation, bridge.generation.value(), now_monotonic_ms)
    {
        return response(&operation_id, status, Vec::new());
    }
    let Some(kind) = wire::CoreServiceCommandKind::from_wire(u32::from(command.kind)) else {
        return response(&operation_id, invalid(), Vec::new());
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(&operation_id, unavailable(), Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);

    let mut effects = Vec::new();
    match kind {
        wire::CoreServiceCommandKind::SetAssetDeliveryPolicy => {
            let Some(cost) = wire::AssetNetworkCost::from_wire(u32::from(command.network_cost))
            else {
                return response(&operation_id, invalid(), Vec::new());
            };
            runtime
                .core_mut()
                .set_asset_network(cost, command.metered_permitted);
        }
        wire::CoreServiceCommandKind::RequestAsset => {
            if runtime
                .core_mut()
                .request_asset(&command.asset_id, &command.asset_revision)
                .is_err()
            {
                return response(&operation_id, invalid(), Vec::new());
            }
        }
        wire::CoreServiceCommandKind::RemoveAsset => {
            // A removal is not planned; it is what the person asked for, and
            // it is answered directly rather than waiting for the next plan to
            // rediscover it.
            let Ok(effect) = runtime
                .core_mut()
                .remove_asset(&command.asset_id, &command.asset_revision)
            else {
                return response(&operation_id, invalid(), Vec::new());
            };
            let Some(projected) = asset_effect_to_ffi(&operation, runtime.core(), &effect) else {
                return response(&operation_id, invalid(), Vec::new());
            };
            effects.push(projected);
        }
        _ => return response(&operation_id, invalid(), Vec::new()),
    }
    effects.extend(planned_effects(bridge, &operation, now_monotonic_ms));
    response_after_change(bridge, accepted(operation_id, effects))
}

#[allow(non_snake_case)]
pub(crate) fn DeliverAssetReport(
    bridge: &mut ServiceBridge,
    report: ffi::BridgeAssetReport,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = report.operation.operation_id.clone();
    let operation = operation_to_wire(report.operation);
    let Some(operation_kind) =
        wire::AssetDeliveryOperation::from_wire(u32::from(report.operation_kind))
    else {
        return response(&operation_id, invalid(), Vec::new());
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(&operation_id, unavailable(), Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);

    let recorded = match operation_kind {
        wire::AssetDeliveryOperation::FetchAsset => {
            let Some(outcome) = wire::AssetTransferOutcome::from_wire(u32::from(report.outcome))
            else {
                return response(&operation_id, invalid(), Vec::new());
            };
            runtime.core_mut().record_asset_transfer(
                &wire::AssetTransferReport {
                    asset_id: report.asset_id,
                    asset_revision: report.asset_revision,
                    outcome,
                    written_bytes: report.written_bytes,
                    observed_bytes: report.observed_bytes,
                    observed_digest: report.observed_digest,
                },
                now_monotonic_ms,
            )
        }
        wire::AssetDeliveryOperation::RemoveAsset => {
            runtime
                .core_mut()
                .record_asset_removal(&wire::AssetRemovalReport {
                    asset_id: report.asset_id,
                    asset_revision: report.asset_revision,
                    reclaimed_bytes: report.reclaimed_bytes,
                })
        }
    };
    if recorded.is_err() {
        return response(&operation_id, invalid(), Vec::new());
    }
    let effects = planned_effects(bridge, &operation, now_monotonic_ms);
    response_after_change(bridge, accepted(operation_id, effects))
}

/// An accepted delivery answer, before the state that goes with it.
///
/// Every caller hands this to `response_after_change`, and that is not
/// bookkeeping — it is the only way a surface ever hears that an artifact
/// arrived. The delivery view lives in `CoreStatus`, which reaches a surface
/// only when the core publishes a state; this bridge published none, so a
/// pack could download, verify and install while every screen went on
/// showing what the snapshot said at bootstrap. The country-flag picker drew
/// empty placeholders over a pack that was already on the disk underneath it,
/// and only a restart made it appear.
fn accepted(operation_id: String, effects: Vec<ffi::BridgeAssetEffect>) -> ffi::BridgeResponse {
    let mut answer = response(
        &operation_id,
        wire::AdmissionStatus::Accepted as u8,
        Vec::new(),
    );
    answer.asset_effects = effects;
    answer
}

const fn invalid() -> u8 {
    wire::AdmissionStatus::InvalidCommand as u8
}

const fn unavailable() -> u8 {
    wire::AdmissionStatus::CoreUnavailable as u8
}

fn validate_operation(
    operation: &wire::OperationEnvelope,
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
        return Err(invalid());
    }
    if now_monotonic_ms >= operation.deadline_monotonic_ms {
        return Err(wire::AdmissionStatus::DeadlineExceeded as u8);
    }
    Ok(())
}

/// The delivery plane's own operation envelope, converted once.
///
/// The record is mirrored rather than shared, because two cxx bridge modules
/// cannot name one by-value struct without an include cycle between their
/// generated headers. The conversion is mirrored with it, the same way the
/// provider and composer planes carry their own.
fn operation_to_wire(value: ffi::BridgeAssetOperation) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}
