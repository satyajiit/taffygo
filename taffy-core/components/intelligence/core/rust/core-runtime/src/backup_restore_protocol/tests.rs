// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{
    BackupRestoreBinding, BackupRestoreCommitAuthorization, BackupRestoreCommitOutcome,
    BackupRestoreProtocol, BackupRestoreProtocolError, BackupRestoreResolutionChoice,
    BackupRestoreResolutionOutcome,
};
use crate::backup_planning::prepare_manifest;
use crate::{wire, DigestError, Sha256Port};
use taffy_storage::backup::RestoreSessionPhase;

mod lifecycle;
mod procedure_admission;
mod recovery;
mod recovery_resolution;
mod recovery_resolution_adversarial;
mod retirement;

const GENERATION: u64 = 7;
const NOW: u64 = 1_000;
const OWNER: &str = "source-profile";

struct TestDigest;

impl Sha256Port for TestDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        let mut output = [0_u8; 32];
        for (index, byte) in input.iter().copied().enumerate() {
            let slot = index % output.len();
            output[slot] = output[slot]
                .wrapping_add(byte)
                .rotate_left(u32::try_from(index % 7).unwrap_or_default());
        }
        output[0] |= 1;
        Ok(output)
    }
}

fn operation(id: &str, deadline: u64) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: id.to_owned(),
        service_generation: GENERATION,
        task_revision: 0,
        deadline_monotonic_ms: deadline,
        idempotency_key: format!("{id}-once"),
    }
}

fn restore_request(target_profile_id: &str, deadline: u64) -> wire::BackupRestorePlanRequest {
    let descriptor = wire::BackupRecordDescriptor {
        kind: wire::BackupRecordKind::MemoryRecord,
        stable_id: "memory-1".to_owned(),
        revision: 3,
        schema_version: 1,
        state: wire::BackupRecordState::Active,
        plaintext_bytes: 5,
        plaintext_sha256: [3; 32],
    };
    let prepared = prepare_manifest(
        wire::BackupManifestPrepareRequest {
            operation: operation("prepare", deadline),
            backup_id: "backup-1".to_owned(),
            source_installation_id: "installation-1".to_owned(),
            created_at_utc: "2026-09-05T00:00:00Z".to_owned(),
            selection: vec![wire::BackupRecordKind::MemoryRecord],
            records: vec![descriptor],
        },
        GENERATION,
        NOW,
        &TestDigest,
    );
    assert_eq!(prepared.status, wire::BackupPlanningStatus::Succeeded);
    wire::BackupRestorePlanRequest {
        operation: operation("plan", deadline),
        manifest_plaintext: prepared.manifest_plaintext,
        staged_records: vec![wire::StagedBackupRecord {
            plaintext_bytes: 5,
            plaintext_sha256: [3; 32],
        }],
        current_records: Vec::new(),
        target: wire::BackupRestoreTarget {
            kind: wire::BackupRestoreTargetKind::NewRegularProfile,
            profile_id: target_profile_id.to_owned(),
        },
    }
}

fn protocol(private_profile: bool) -> BackupRestoreProtocol {
    BackupRestoreProtocol::new(OWNER.to_owned(), GENERATION, private_profile)
}

fn commit_issued(
    target: &str,
) -> (
    BackupRestoreProtocol,
    BackupRestoreBinding,
    BackupRestoreCommitAuthorization,
) {
    let mut restore = protocol(false);
    let planned = restore.plan_restore(restore_request(target, NOW + 50), NOW, &TestDigest);
    let binding = restore.active_binding().expect("binding").clone();
    let stage = restore
        .confirm_plan(
            operation("confirm", NOW + 50),
            &binding,
            planned.confirmation_sha256,
            NOW + 1,
            &TestDigest,
        )
        .expect("stage authority");
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::Staging));
    assert_eq!(
        restore.confirm_plan(
            operation("confirm-again", NOW + 50),
            &binding,
            planned.confirmation_sha256,
            NOW + 2,
            &TestDigest,
        ),
        Err(BackupRestoreProtocolError::WrongPhase)
    );
    let commit = restore
        .report_stage_verified(
            operation("commit", NOW + 50),
            &stage,
            planned.snapshot_sha256,
            Vec::new(),
            NOW + 2,
        )
        .expect("commit authority");
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::CommitIssued));
    (restore, binding, commit)
}

#[test]
fn source_core_binds_a_distinct_browser_reserved_target() {
    let mut restore = protocol(false);
    let planned = restore.plan_restore(
        restore_request("reserved-target", NOW + 20),
        NOW,
        &TestDigest,
    );
    assert_eq!(planned.status, wire::BackupPlanningStatus::Succeeded);
    let binding = restore.active_binding().expect("retained binding");
    assert_eq!(binding.owner_profile_id(), OWNER);
    assert_eq!(binding.target_profile_id(), "reserved-target");
    assert_eq!(binding.backup_id(), "backup-1");
    assert_eq!(binding.snapshot_sha256(), planned.snapshot_sha256);
    assert_eq!(binding.confirmation_sha256(), planned.confirmation_sha256);
    assert_eq!(
        restore.phase(),
        Some(RestoreSessionPhase::AwaitingPlanConfirmation)
    );

    let mut same_target = protocol(false);
    assert_eq!(
        same_target
            .plan_restore(restore_request(OWNER, NOW + 20), NOW, &TestDigest)
            .status,
        wire::BackupPlanningStatus::InvalidRequest
    );
    let mut existing = restore_request("reserved-existing", NOW + 20);
    existing.target.kind = wire::BackupRestoreTargetKind::ExistingRegularProfile;
    let mut existing_protocol = protocol(false);
    assert_eq!(
        existing_protocol
            .plan_restore(existing, NOW, &TestDigest)
            .status,
        wire::BackupPlanningStatus::InvalidRequest
    );
    let mut private = protocol(true);
    assert_eq!(
        private
            .plan_restore(
                restore_request("private-target", NOW + 20),
                NOW,
                &TestDigest
            )
            .status,
        wire::BackupPlanningStatus::Unavailable
    );
}

#[test]
fn commit_authority_is_consumptive_and_unknown_stays_issued() {
    let (mut restore, binding, commit) = commit_issued("candidate-a");
    assert_eq!(
        restore.cancel_before_commit(&operation("cancel", NOW + 50), &binding, NOW + 3),
        Err(BackupRestoreProtocolError::ReconcileRequired)
    );
    assert_eq!(
        restore.resolve_commit(
            &operation("unknown-commit", NOW + 50),
            &commit,
            BackupRestoreCommitOutcome::OutcomeUnknown,
            NOW + 3,
        ),
        Err(BackupRestoreProtocolError::ReconcileRequired)
    );
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::CommitIssued));
    restore
        .resolve_commit(
            &operation("committed", NOW + 50),
            &commit,
            BackupRestoreCommitOutcome::Committed,
            NOW + 4,
        )
        .expect("durable committed result");
    assert_eq!(
        restore.phase(),
        Some(RestoreSessionPhase::RollbackAvailable)
    );
    assert_eq!(
        restore.resolve_commit(
            &operation("late-unknown", NOW + 50),
            &commit,
            BackupRestoreCommitOutcome::OutcomeUnknown,
            NOW + 4,
        ),
        Err(BackupRestoreProtocolError::WrongPhase)
    );
    assert_eq!(
        restore.resolve_commit(
            &operation("duplicate-committed", NOW + 50),
            &commit,
            BackupRestoreCommitOutcome::Committed,
            NOW + 4,
        ),
        Err(BackupRestoreProtocolError::WrongPhase)
    );
    assert_eq!(
        restore.phase(),
        Some(RestoreSessionPhase::RollbackAvailable)
    );
}

#[test]
fn candidate_resolution_authority_is_consumptive() {
    let (mut restore, binding, commit) = commit_issued("candidate-a");
    restore
        .resolve_commit(
            &operation("committed", NOW + 50),
            &commit,
            BackupRestoreCommitOutcome::Committed,
            NOW + 4,
        )
        .expect("durable committed result");
    let accept = restore
        .choose_resolution(
            operation("accept", NOW + 50),
            &binding,
            BackupRestoreResolutionChoice::AcceptCandidate,
            NOW + 5,
        )
        .expect("publish authority");
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::AcceptIssued));
    assert_eq!(
        restore.resolve_resolution(
            &operation("accept-unknown", NOW + 50),
            &accept,
            BackupRestoreResolutionOutcome::OutcomeUnknown,
            NOW + 6,
        ),
        Err(BackupRestoreProtocolError::ReconcileRequired)
    );
    restore
        .resolve_resolution(
            &operation("accept-not-applied", NOW + 50),
            &accept,
            BackupRestoreResolutionOutcome::DefinitelyNotCompleted,
            NOW + 7,
        )
        .expect("definite refusal returns to review");
    assert_eq!(
        restore.phase(),
        Some(RestoreSessionPhase::RollbackAvailable)
    );

    let discard = restore
        .choose_resolution(
            operation("discard", NOW + 50),
            &binding,
            BackupRestoreResolutionChoice::DiscardCandidate,
            NOW + 8,
        )
        .expect("delete authority");
    restore
        .resolve_resolution(
            &operation("discarded", NOW + 50),
            &discard,
            BackupRestoreResolutionOutcome::Completed,
            NOW + 9,
        )
        .expect("verified delete");
    assert_eq!(restore.active_binding(), None);
}
