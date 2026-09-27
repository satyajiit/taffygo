// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{inspect_manifest, plan_restore, prepare_manifest, valid_operation};
use crate::{wire, DigestError, Sha256Port};

const GENERATION: u64 = 9;
const NOW: u64 = 100;

struct TestDigest;

impl Sha256Port for TestDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        let mut output = [0_u8; 32];
        for (index, byte) in input.iter().copied().enumerate() {
            let position = index % output.len();
            output[position] = output[position]
                .wrapping_add(byte)
                .wrapping_add(u8::try_from(index % 251).unwrap_or_default());
        }
        output[0] |= 1;
        Ok(output)
    }
}

fn operation(id: &str) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: id.to_owned(),
        service_generation: GENERATION,
        task_revision: 0,
        deadline_monotonic_ms: NOW + 1,
        idempotency_key: format!("{id}-once"),
    }
}

fn descriptor(
    kind: wire::BackupRecordKind,
    stable_id: &str,
    revision: u64,
    bytes: u64,
    digest_byte: u8,
) -> wire::BackupRecordDescriptor {
    wire::BackupRecordDescriptor {
        kind,
        stable_id: stable_id.to_owned(),
        revision,
        schema_version: 1,
        state: wire::BackupRecordState::Active,
        plaintext_bytes: bytes,
        plaintext_sha256: [digest_byte; 32],
    }
}

fn prepare_request() -> wire::BackupManifestPrepareRequest {
    wire::BackupManifestPrepareRequest {
        operation: operation("prepare"),
        backup_id: "backup-1".to_owned(),
        source_installation_id: "installation-1".to_owned(),
        created_at_utc: "2026-09-05T00:00:00Z".to_owned(),
        selection: vec![
            wire::BackupRecordKind::MemoryRecord,
            wire::BackupRecordKind::SavedWorkspace,
        ],
        records: vec![
            descriptor(wire::BackupRecordKind::MemoryRecord, "memory-1", 3, 5, 3),
            descriptor(
                wire::BackupRecordKind::SavedWorkspace,
                "workspace-1",
                2,
                7,
                2,
            ),
        ],
    }
}

#[test]
fn generated_prepare_and_inspect_bind_canonical_browser_source_order() {
    let prepared = prepare_manifest(prepare_request(), GENERATION, NOW, &TestDigest);
    assert_eq!(prepared.status, wire::BackupPlanningStatus::Succeeded);
    assert_eq!(prepared.source_order, vec![1, 0]);
    assert_eq!(prepared.payload_plaintext_bytes, 12);
    assert_eq!(prepared.expected_sealed_chunks, 2);
    assert!(!prepared.manifest_plaintext.is_empty());
    assert_ne!(prepared.snapshot_sha256, [0; 32]);

    let inspected = inspect_manifest(
        wire::BackupManifestInspectRequest {
            operation: operation("inspect"),
            manifest_plaintext: prepared.manifest_plaintext,
        },
        GENERATION,
        NOW,
        &TestDigest,
    );
    assert_eq!(inspected.status, wire::BackupPlanningStatus::Succeeded);
    assert_eq!(inspected.backup_id, "backup-1");
    assert_eq!(inspected.source_installation_id, "installation-1");
    assert_eq!(
        inspected.selection,
        vec![
            wire::BackupRecordKind::SavedWorkspace,
            wire::BackupRecordKind::MemoryRecord,
        ]
    );
    assert_eq!(inspected.record_count, 2);
    assert_eq!(inspected.payload_plaintext_bytes, 12);
    assert_eq!(inspected.records.len(), 2);
    assert_eq!(inspected.records[0].plaintext_bytes, 7);
    assert_eq!(inspected.records[1].plaintext_bytes, 5);
}

#[test]
fn restore_plan_refuses_one_wrong_staged_range_before_returning_actions() {
    let prepared = prepare_manifest(prepare_request(), GENERATION, NOW, &TestDigest);
    assert_eq!(prepared.status, wire::BackupPlanningStatus::Succeeded);
    let request = wire::BackupRestorePlanRequest {
        operation: operation("restore"),
        manifest_plaintext: prepared.manifest_plaintext,
        staged_records: vec![
            wire::StagedBackupRecord {
                plaintext_bytes: 7,
                plaintext_sha256: [2; 32],
            },
            wire::StagedBackupRecord {
                plaintext_bytes: 5,
                plaintext_sha256: [3; 32],
            },
        ],
        current_records: Vec::new(),
        target: wire::BackupRestoreTarget {
            kind: wire::BackupRestoreTargetKind::NewRegularProfile,
            profile_id: "target-profile".to_owned(),
        },
    };
    let planned = plan_restore(request.clone(), GENERATION, NOW, &TestDigest);
    assert_eq!(planned.status, wire::BackupPlanningStatus::Succeeded);
    assert!(!planned.has_conflicts);
    assert_eq!(planned.entries.len(), 2);
    assert!(planned
        .entries
        .iter()
        .all(|entry| entry.action == wire::BackupRestoreAction::StageCreate));
    assert_eq!(planned.entries[0].schema_version, 1);
    assert_eq!(planned.entries[0].state, wire::BackupRecordState::Active);
    assert_eq!(planned.entries[0].plaintext_bytes, 7);
    assert_eq!(planned.entries[0].plaintext_sha256, [2; 32]);
    assert_eq!(planned.entries[1].plaintext_bytes, 5);
    assert_eq!(planned.entries[1].plaintext_sha256, [3; 32]);
    assert_ne!(planned.confirmation_sha256, [0; 32]);

    let mut wrong_range = request;
    wrong_range.staged_records[1].plaintext_bytes += 1;
    let refused = plan_restore(wrong_range, GENERATION, NOW, &TestDigest);
    assert_eq!(
        refused.status,
        wire::BackupPlanningStatus::StagedPayloadMismatch
    );
    assert!(refused.backup_id.is_empty());
    assert!(refused.entries.is_empty());
    assert_eq!(refused.snapshot_sha256, [0; 32]);
    assert_eq!(refused.confirmation_sha256, [0; 32]);
}

#[test]
fn duplicate_identity_and_expired_operation_fail_closed() {
    let mut duplicate = prepare_request();
    duplicate.records.push(duplicate.records[0].clone());
    let refused = prepare_manifest(duplicate, GENERATION, NOW, &TestDigest);
    assert_eq!(refused.status, wire::BackupPlanningStatus::InvalidRequest);
    assert!(refused.manifest_plaintext.is_empty());
    assert!(refused.source_order.is_empty());

    let mut expired = prepare_request();
    expired.operation.deadline_monotonic_ms = NOW - 1;
    let refused = prepare_manifest(expired, GENERATION, NOW, &TestDigest);
    assert_eq!(refused.status, wire::BackupPlanningStatus::InvalidRequest);
}

#[test]
fn operation_admission_matches_the_physical_backup_boundary() {
    let valid = operation("valid");
    assert!(valid_operation(&valid, GENERATION, NOW));

    let mut zero_generation = valid.clone();
    zero_generation.service_generation = 0;
    assert!(!valid_operation(&zero_generation, 0, NOW));

    let mut wrong_generation = valid.clone();
    wrong_generation.service_generation += 1;
    assert!(!valid_operation(&wrong_generation, GENERATION, NOW));

    let mut task_bound = valid.clone();
    task_bound.task_revision = 1;
    assert!(!valid_operation(&task_bound, GENERATION, NOW));

    let mut deadline_is_now = valid.clone();
    deadline_is_now.deadline_monotonic_ms = NOW;
    assert!(!valid_operation(&deadline_is_now, GENERATION, NOW));

    let mut controlled_id = valid.clone();
    controlled_id.operation_id = "restore\nsplice".to_owned();
    assert!(!valid_operation(&controlled_id, GENERATION, NOW));

    let mut controlled_key = valid;
    controlled_key.idempotency_key = "restore\u{7f}splice".to_owned();
    assert!(!valid_operation(&controlled_key, GENERATION, NOW));
}

#[test]
fn descriptor_aggregate_is_refused_before_manifest_output_allocation() {
    let mut request = prepare_request();
    request.records = (0_u64..129)
        .map(|index| {
            descriptor(
                wire::BackupRecordKind::MemoryRecord,
                &format!("memory-{index}"),
                index + 1,
                wire::MAX_BACKUP_RECORD_BYTES as u64,
                4,
            )
        })
        .collect();
    let refused = prepare_manifest(request, GENERATION, NOW, &TestDigest);
    assert_eq!(refused.status, wire::BackupPlanningStatus::InvalidRequest);
    assert!(refused.manifest_plaintext.is_empty());
    assert_eq!(refused.payload_plaintext_bytes, 0);
}
