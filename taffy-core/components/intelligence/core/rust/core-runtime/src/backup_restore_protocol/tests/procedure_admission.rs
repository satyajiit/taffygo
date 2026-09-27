// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::super::BackupRestoreStageAuthorization;
use super::*;
use policy_engine::origin::normalize_serialization;
use procedure_engine::{build_source_table, encode};

fn skill() -> wire::SkillRecord {
    let definition =
        build_source_table(&normalize_serialization("https://example.test").unwrap()).unwrap();
    wire::SkillRecord {
        skill_id: definition.id.as_str().to_owned(),
        origin: definition.scope.origin().display(),
        provenance: wire::SkillProvenance::Authored,
        status: wire::SkillStatus::Active,
        active_version: definition.version.0,
        definition: encode(&definition).unwrap(),
        step_count: u32::try_from(definition.steps.len()).unwrap(),
        installed_at_utc_ms: 1,
        updated_at_utc_ms: 2,
    }
}

fn staged() -> (
    BackupRestoreProtocol,
    wire::BackupRestorePlanResult,
    BackupRestoreStageAuthorization,
) {
    let record = skill();
    let prepared = prepare_manifest(
        wire::BackupManifestPrepareRequest {
            operation: operation("prepare", NOW + 50),
            backup_id: "backup-1".to_owned(),
            source_installation_id: "installation-1".to_owned(),
            created_at_utc: "2026-09-05T00:00:00Z".to_owned(),
            selection: vec![wire::BackupRecordKind::UserAuthoredSkill],
            records: vec![wire::BackupRecordDescriptor {
                kind: wire::BackupRecordKind::UserAuthoredSkill,
                stable_id: record.skill_id,
                revision: u64::from(record.active_version),
                schema_version: 1,
                state: wire::BackupRecordState::Active,
                plaintext_bytes: 128,
                plaintext_sha256: [3; 32],
            }],
        },
        GENERATION,
        NOW,
        &TestDigest,
    );
    assert_eq!(prepared.status, wire::BackupPlanningStatus::Succeeded);
    let mut restore = protocol(false);
    let plan = restore.plan_restore(
        wire::BackupRestorePlanRequest {
            operation: operation("plan", NOW + 50),
            manifest_plaintext: prepared.manifest_plaintext,
            staged_records: vec![wire::StagedBackupRecord {
                plaintext_bytes: 128,
                plaintext_sha256: [3; 32],
            }],
            current_records: Vec::new(),
            target: wire::BackupRestoreTarget {
                kind: wire::BackupRestoreTargetKind::NewRegularProfile,
                profile_id: "candidate".to_owned(),
            },
        },
        NOW,
        &TestDigest,
    );
    assert_eq!(plan.status, wire::BackupPlanningStatus::Succeeded);
    let binding = restore.active_binding().unwrap().clone();
    let authorization = restore
        .confirm_plan(
            operation("confirm", NOW + 50),
            &binding,
            plan.confirmation_sha256,
            NOW + 1,
            &TestDigest,
        )
        .unwrap();
    (restore, plan, authorization)
}

#[test]
fn exact_validated_procedure_set_can_receive_commit_authority_only_once() {
    let (mut restore, plan, authorization) = staged();
    assert!(restore
        .report_stage_verified(
            operation("commit", NOW + 50),
            &authorization,
            plan.snapshot_sha256,
            vec![skill()],
            NOW + 2,
        )
        .is_ok());
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::CommitIssued));
    assert_eq!(
        restore.report_stage_verified(
            operation("commit-again", NOW + 50),
            &authorization,
            plan.snapshot_sha256,
            vec![skill()],
            NOW + 3,
        ),
        Err(BackupRestoreProtocolError::WrongPhase)
    );
}

#[test]
fn omission_or_extra_records_withdraws_the_plan_without_commit_authority() {
    for skills in [Vec::new(), vec![skill(), skill()]] {
        let (mut restore, plan, authorization) = staged();
        assert_eq!(
            restore.report_stage_verified(
                operation("commit", NOW + 50),
                &authorization,
                plan.snapshot_sha256,
                skills,
                NOW + 2,
            ),
            Err(BackupRestoreProtocolError::SnapshotMismatch)
        );
        assert_eq!(restore.phase(), None);
        assert_eq!(restore.active_binding(), None);
    }
}

#[test]
fn malformed_definition_cannot_receive_authority_with_an_exact_snapshot_digest() {
    let (mut restore, plan, authorization) = staged();
    let mut invalid = skill();
    invalid.definition.push(0);
    assert_eq!(
        restore.report_stage_verified(
            operation("commit", NOW + 50),
            &authorization,
            plan.snapshot_sha256,
            vec![invalid],
            NOW + 2,
        ),
        Err(BackupRestoreProtocolError::SnapshotMismatch)
    );
    assert_eq!(restore.phase(), None);
    // A corrected second report is not permission to revive the withdrawn
    // plan; another explicit confirmed restore must own any new attempt.
    assert!(restore
        .report_stage_verified(
            operation("corrected", NOW + 50),
            &authorization,
            plan.snapshot_sha256,
            vec![skill()],
            NOW + 3,
        )
        .is_err());
}

#[test]
fn a_valid_definition_with_substituted_plan_metadata_is_refused() {
    let mut variants = vec![skill(); 3];
    variants[0].skill_id = "not-selected".to_owned();
    variants[1].active_version += 1;
    variants[2].provenance = wire::SkillProvenance::RecordedFromTask;
    for changed in variants {
        let (mut restore, plan, authorization) = staged();
        assert_eq!(
            restore.report_stage_verified(
                operation("commit", NOW + 50),
                &authorization,
                plan.snapshot_sha256,
                vec![changed],
                NOW + 2,
            ),
            Err(BackupRestoreProtocolError::SnapshotMismatch)
        );
        assert_eq!(restore.phase(), None);
    }
}
