// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use policy_engine::origin::normalize_serialization;
use procedure_engine::{build_source_table, encode, ProcedureProvenance};

fn selected(learned: bool) -> (RestoreEntry, wire::SkillRecord) {
    let mut procedure =
        build_source_table(&normalize_serialization("https://example.test").unwrap()).unwrap();
    let (kind, provenance) = if learned {
        procedure.provenance = ProcedureProvenance::RecordedFromTask;
        (
            BackupRecordKind::LearnedProcedure,
            wire::SkillProvenance::RecordedFromTask,
        )
    } else {
        (
            BackupRecordKind::UserAuthoredSkill,
            wire::SkillProvenance::Authored,
        )
    };
    let skill = wire::SkillRecord {
        skill_id: procedure.id.as_str().to_owned(),
        origin: procedure.scope.origin().display(),
        provenance,
        status: wire::SkillStatus::Active,
        active_version: procedure.version.0,
        definition: encode(&procedure).unwrap(),
        step_count: u32::try_from(procedure.steps.len()).unwrap(),
        installed_at_utc_ms: 1,
        updated_at_utc_ms: 2,
    };
    let entry = RestoreEntry {
        kind,
        stable_id: skill.skill_id.clone(),
        archive_revision: u64::from(skill.active_version),
        action: RestoreAction::StageCreate,
        schema_version: 1,
        state: BackupRecordState::Active,
        plaintext_bytes: 128,
        plaintext_sha256: [1; 32],
    };
    (entry, skill)
}

#[test]
fn complete_authored_or_learned_set_is_admitted() {
    for learned in [false, true] {
        let (entry, skill) = selected(learned);
        assert_eq!(validate_plan_procedures(&[entry], vec![skill]), Ok(()));
    }
    assert_eq!(validate_plan_procedures(&[], Vec::new()), Ok(()));
}

#[test]
fn omission_addition_and_identity_substitution_are_refused() {
    let (entry, skill) = selected(false);
    let mut substituted = skill.clone();
    substituted.skill_id = "not-selected".to_owned();
    for (entries, skills) in [
        (vec![entry.clone()], Vec::new()),
        (Vec::new(), vec![skill.clone()]),
        (vec![entry.clone()], vec![skill.clone(), skill]),
        (vec![entry], vec![substituted]),
    ] {
        assert_eq!(
            validate_plan_procedures(&entries, skills),
            Err(BackupRestoreProtocolError::SnapshotMismatch)
        );
    }
}

#[test]
fn revision_and_provenance_must_match_the_confirmed_plan() {
    let (entry, skill) = selected(false);
    let mut other_revision = entry.clone();
    other_revision.archive_revision += 1;
    let mut other_provenance = entry;
    other_provenance.kind = BackupRecordKind::LearnedProcedure;
    for changed in [other_revision, other_provenance] {
        assert_eq!(
            validate_plan_procedures(&[changed], vec![skill.clone()]),
            Err(BackupRestoreProtocolError::SnapshotMismatch)
        );
    }
}

#[test]
fn duplicate_identity_across_two_record_kinds_is_refused() {
    let (authored, authored_skill) = selected(false);
    let (learned, learned_skill) = selected(true);
    assert_eq!(
        validate_plan_procedures(&[authored, learned], vec![authored_skill, learned_skill]),
        Err(BackupRestoreProtocolError::SnapshotMismatch)
    );
}

#[test]
fn tombstones_future_schemas_and_non_create_actions_are_refused() {
    let (entry, skill) = selected(false);
    let mut tombstone = entry.clone();
    tombstone.state = BackupRecordState::Tombstone;
    let mut future = entry.clone();
    future.schema_version = 2;
    let mut already_present = entry;
    already_present.action = RestoreAction::AlreadyPresent;
    for changed in [tombstone, future, already_present] {
        assert_eq!(
            validate_plan_procedures(&[changed], vec![skill.clone()]),
            Err(BackupRestoreProtocolError::SnapshotMismatch)
        );
    }
}

#[test]
fn matching_envelopes_cannot_hide_a_malformed_portable_definition() {
    let (entry, mut skill) = selected(false);
    skill.definition.push(0);
    assert_eq!(
        validate_plan_procedures(&[entry], vec![skill]),
        Err(BackupRestoreProtocolError::SnapshotMismatch)
    );
}

#[test]
fn unrelated_records_do_not_change_the_required_procedure_set() {
    let (entry, skill) = selected(false);
    let mut unrelated = entry.clone();
    unrelated.kind = BackupRecordKind::MemoryRecord;
    unrelated.stable_id = "memory-1".to_owned();
    assert_eq!(
        validate_plan_procedures(&[unrelated, entry], vec![skill]),
        Ok(())
    );
}
