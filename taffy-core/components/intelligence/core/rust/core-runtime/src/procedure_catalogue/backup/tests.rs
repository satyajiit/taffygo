// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use policy_engine::origin::normalize_serialization;
use procedure_engine::{
    build_source_table, decode_beside, encode, Procedure, ProcedureProvenance, ProcedureStatus,
    RefusalReason,
};

fn procedure() -> Procedure {
    build_source_table(&normalize_serialization("https://example.test").unwrap()).unwrap()
}

fn record(procedure: &Procedure) -> wire::SkillRecord {
    wire::SkillRecord {
        skill_id: procedure.id.as_str().to_owned(),
        origin: procedure.scope.origin().display(),
        provenance: match procedure.provenance {
            ProcedureProvenance::Authored => wire::SkillProvenance::Authored,
            ProcedureProvenance::RecordedFromTask => wire::SkillProvenance::RecordedFromTask,
            ProcedureProvenance::InstalledFromPack => wire::SkillProvenance::InstalledFromPack,
        },
        status: match procedure.status {
            ProcedureStatus::Draft => wire::SkillStatus::Draft,
            ProcedureStatus::Active => wire::SkillStatus::Active,
            ProcedureStatus::Superseded => wire::SkillStatus::Superseded,
            ProcedureStatus::Retired => wire::SkillStatus::Retired,
            ProcedureStatus::Disabled => wire::SkillStatus::Disabled,
        },
        active_version: procedure.version.0,
        definition: encode(procedure).unwrap(),
        step_count: u32::try_from(procedure.steps.len()).unwrap(),
        installed_at_utc_ms: 1,
        updated_at_utc_ms: 2,
    }
}

fn assert_bootstrap_parity(skills: Vec<wire::SkillRecord>) -> Result<(), ProcedureCatalogueError> {
    let expected =
        ProcedureCatalogue::restore(skills.clone(), Vec::new(), Milestone::M8).map(|_| ());
    let result = validate_backup_procedures(skills);
    assert_eq!(result, expected);
    result
}

#[test]
fn empty_selection_is_valid_without_a_target_runtime() {
    assert_eq!(assert_bootstrap_parity(Vec::new()), Ok(()));
}

#[test]
fn both_supported_provenances_and_every_stored_status_are_validated() {
    for provenance in [
        ProcedureProvenance::Authored,
        ProcedureProvenance::RecordedFromTask,
    ] {
        for status in [
            ProcedureStatus::Draft,
            ProcedureStatus::Active,
            ProcedureStatus::Superseded,
            ProcedureStatus::Retired,
            ProcedureStatus::Disabled,
        ] {
            let mut definition = procedure();
            definition.provenance = provenance;
            definition.status = status;
            assert_eq!(assert_bootstrap_parity(vec![record(&definition)]), Ok(()));
        }
    }
}

#[test]
fn committed_lifecycle_changes_do_not_require_reencoding_the_version() {
    let mut definition = procedure();
    definition.status = ProcedureStatus::Draft;
    let mut skill = record(&definition);
    let immutable_bytes = skill.definition.clone();
    for status in [
        wire::SkillStatus::Active,
        wire::SkillStatus::Disabled,
        wire::SkillStatus::Superseded,
        wire::SkillStatus::Retired,
        wire::SkillStatus::Draft,
    ] {
        skill.status = status;
        assert_eq!(assert_bootstrap_parity(vec![skill.clone()]), Ok(()));
        assert_eq!(skill.definition, immutable_bytes);
    }
}

#[test]
fn valid_header_and_decodable_body_do_not_admit_an_unknown_verb() {
    let mut definition = procedure();
    definition.steps[0].verb = "not-a-product-verb".to_owned();
    let skill = record(&definition);
    assert_eq!(
        decode_beside(&skill.definition, skill.step_count),
        Ok(definition)
    );
    assert!(matches!(
        assert_bootstrap_parity(vec![skill]),
        Err(ProcedureCatalogueError::Structural(refusal))
            if refusal.reason == RefusalReason::UnregisteredVerb
    ));
}

#[test]
fn disabled_invalid_procedures_are_not_skipped() {
    let mut definition = procedure();
    definition.status = ProcedureStatus::Disabled;
    definition.steps[0].verb = "not-a-product-verb".to_owned();
    assert!(matches!(
        assert_bootstrap_parity(vec![record(&definition)]),
        Err(ProcedureCatalogueError::Structural(_))
    ));
}

#[test]
fn trailing_or_truncated_body_is_refused_despite_an_intact_header() {
    let valid = record(&procedure());
    let mut trailing = valid.clone();
    trailing.definition.push(0);
    let mut truncated = valid;
    truncated.definition.pop();
    for skill in [trailing, truncated] {
        assert!(matches!(
            assert_bootstrap_parity(vec![skill]),
            Err(ProcedureCatalogueError::Definition(_))
        ));
    }
}

#[test]
fn envelope_and_definition_must_agree_in_every_identity_field() {
    let valid = record(&procedure());
    let mut variants = vec![valid.clone(); 5];
    variants[0].skill_id = "different-skill".to_owned();
    variants[1].origin = "https://different.test".to_owned();
    variants[2].active_version = 2;
    variants[3].provenance = wire::SkillProvenance::RecordedFromTask;
    variants[4].step_count += 1;
    for skill in variants {
        assert!(assert_bootstrap_parity(vec![skill]).is_err());
    }
}

#[test]
fn duplicate_identity_across_provenances_refuses_the_whole_selection() {
    let mut definition = procedure();
    let authored = record(&definition);
    definition.provenance = ProcedureProvenance::RecordedFromTask;
    let learned = record(&definition);
    assert_eq!(
        assert_bootstrap_parity(vec![authored, learned]),
        Err(ProcedureCatalogueError::DuplicateSkill)
    );
}

#[test]
fn reserved_pack_provenance_is_not_turned_into_an_authored_record() {
    let mut definition = procedure();
    definition.provenance = ProcedureProvenance::InstalledFromPack;
    assert_eq!(
        assert_bootstrap_parity(vec![record(&definition)]),
        Err(ProcedureCatalogueError::InvalidMetadata)
    );
}

#[test]
fn oversized_selection_and_definition_are_refused_before_decode() {
    let valid = record(&procedure());
    assert_eq!(
        assert_bootstrap_parity(vec![valid.clone(); wire::MAX_SKILLS_PER_PROFILE + 1]),
        Err(ProcedureCatalogueError::TooManySkills)
    );
    let mut oversized = valid;
    oversized.definition = vec![0xff; wire::MAX_SKILL_DEFINITION_BYTES + 1];
    assert_eq!(
        assert_bootstrap_parity(vec![oversized]),
        Err(ProcedureCatalogueError::InvalidMetadata)
    );
}

#[test]
fn one_bad_record_refuses_the_complete_set_without_changing_the_source() {
    let valid = record(&procedure());
    let source =
        ProcedureCatalogue::restore(vec![valid.clone()], Vec::new(), Milestone::M8).unwrap();
    let source_before = source.clone();
    let mut invalid = valid.clone();
    invalid.skill_id = "other-skill".to_owned();
    invalid.definition.push(0);
    assert!(assert_bootstrap_parity(vec![valid, invalid]).is_err());
    assert_eq!(source, source_before);
}
