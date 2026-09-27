// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded restored procedures and their content-free recent run ledger.
//!
//! Storage answers this domain once, at bootstrap. This module is the other
//! side of that seam: it decodes and validates every row as one all-or-nothing
//! catalogue, matches only active procedures, and narrows a selected task's
//! existing tool set. It performs no I/O and grants no authority.

use std::collections::BTreeSet;

use core_service_types as wire;
use procedure_engine::{
    decode_beside, narrow, validate, DecodeError, MatchVerdict, PageFacts, Procedure,
    ProcedureProvenance, ProcedureStatus, Refusal,
};
use task_engine::tool::{EffectiveToolSet, Milestone};

mod backup;
mod mutation;
mod view;

pub use backup::validate_backup_procedures;
pub use mutation::{PreparedSkillMutation, SkillMutationError, SkillMutationPersistence};

/// One procedure that matched the live page, identified by its immutable
/// version rather than by its position in the bootstrap list.
#[derive(Clone, Copy, Debug)]
pub struct ProcedureOffer<'a> {
    pub skill_version_id: &'a str,
    pub procedure: &'a Procedure,
}

/// Why no catalogue can truthfully be restored from a bootstrap.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProcedureCatalogueError {
    TooManySkills,
    TooMuchRecall,
    DuplicateSkill,
    InvalidMetadata,
    Definition(DecodeError),
    Structural(Refusal),
    InvalidRecall,
    RecallOutOfOrder,
}

/// Why a selected saved procedure cannot own a new task.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SkillStartError {
    UnknownVersion,
    NotRunnable,
    ScopeDiffers,
    NarrowingRefused,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct CataloguedProcedure {
    version_id: String,
    procedure: Procedure,
    installed_at_utc_ms: u64,
    updated_at_utc_ms: u64,
}

/// Every installed procedure and the newest bounded run rows for one profile.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ProcedureCatalogue {
    procedures: Vec<CataloguedProcedure>,
    recall: Vec<wire::SkillRunRecord>,
}

impl ProcedureCatalogue {
    /// Restores the complete bounded catalogue, or refuses it whole.
    pub fn restore(
        skills: Vec<wire::SkillRecord>,
        recall: Vec<wire::SkillRunRecord>,
        milestone: Milestone,
    ) -> Result<Self, ProcedureCatalogueError> {
        if skills.len() > wire::MAX_SKILLS_PER_PROFILE {
            return Err(ProcedureCatalogueError::TooManySkills);
        }
        if recall.len() > wire::MAX_SKILL_RECALL_ENTRIES {
            return Err(ProcedureCatalogueError::TooMuchRecall);
        }
        let mut ids = BTreeSet::new();
        let mut procedures = Vec::with_capacity(skills.len());
        for record in skills {
            if !metadata_is_bounded(&record) {
                return Err(ProcedureCatalogueError::InvalidMetadata);
            }
            if !ids.insert(record.skill_id.clone()) {
                return Err(ProcedureCatalogueError::DuplicateSkill);
            }
            let mut procedure = decode_beside(&record.definition, record.step_count)
                .map_err(ProcedureCatalogueError::Definition)?;
            validate(&procedure, milestone).map_err(ProcedureCatalogueError::Structural)?;
            if !metadata_matches(&record, &procedure) {
                return Err(ProcedureCatalogueError::InvalidMetadata);
            }
            // A committed lifecycle change updates the durable row, not the
            // immutable version body. Decode and validate that body first.
            procedure.status = status(record.status);
            let version_id = version_id(&procedure);
            if version_id.len() > wire::MAX_IDENTIFIER_BYTES {
                return Err(ProcedureCatalogueError::InvalidMetadata);
            }
            procedures.push(CataloguedProcedure {
                version_id,
                procedure,
                installed_at_utc_ms: record.installed_at_utc_ms,
                updated_at_utc_ms: record.updated_at_utc_ms,
            });
        }
        procedures.sort_by(|left, right| left.procedure.id.cmp(&right.procedure.id));
        validate_recall(&recall, &procedures)?;
        Ok(Self { procedures, recall })
    }

    /// Active procedures whose scope and closed condition match `facts`.
    pub fn matching(&self, facts: &PageFacts) -> Vec<ProcedureOffer<'_>> {
        self.procedures
            .iter()
            .filter(|entry| {
                entry.procedure.is_runnable()
                    && entry.procedure.verdict(facts) == MatchVerdict::Matched
            })
            .map(|entry| ProcedureOffer {
                skill_version_id: entry.version_id.as_str(),
                procedure: &entry.procedure,
            })
            .collect()
    }

    /// Resolves the exact immutable procedure a person selected from an offer.
    pub fn resolve(&self, skill_version_id: &str) -> Option<&Procedure> {
        self.procedures
            .iter()
            .find(|entry| entry.version_id == skill_version_id)
            .map(|entry| &entry.procedure)
    }

    /// Narrows an already-reviewed task start for one selected procedure.
    pub fn narrow_start(
        &self,
        skill_version_id: &str,
        source_origin: &str,
        milestone: Milestone,
        allowlist: &[String],
    ) -> Result<Vec<String>, SkillStartError> {
        let procedure = self
            .resolve(skill_version_id)
            .ok_or(SkillStartError::UnknownVersion)?;
        if !procedure.is_runnable() {
            return Err(SkillStartError::NotRunnable);
        }
        let scope = policy_engine::origin::normalize_serialization(source_origin)
            .map_err(|_| SkillStartError::ScopeDiffers)?;
        if !procedure.scope.covers(&scope) {
            return Err(SkillStartError::ScopeDiffers);
        }
        let existing = EffectiveToolSet::for_task(milestone, allowlist);
        let narrowed =
            narrow(&existing, procedure).map_err(|_| SkillStartError::NarrowingRefused)?;
        Ok(narrowed.names().into_iter().map(str::to_owned).collect())
    }

    /// Newest-first, content-free runs restored beside the procedures.
    pub fn recall(&self) -> &[wire::SkillRunRecord] {
        &self.recall
    }
}

fn metadata_matches(record: &wire::SkillRecord, procedure: &Procedure) -> bool {
    record.skill_id == procedure.id.as_str()
        && record.origin == procedure.scope.origin().display()
        && record.active_version == procedure.version.0
        && provenance(record.provenance) == Some(procedure.provenance)
}

fn metadata_is_bounded(record: &wire::SkillRecord) -> bool {
    !record.skill_id.is_empty()
        && record.skill_id.len() <= wire::MAX_SKILL_ID_BYTES
        && !record.origin.is_empty()
        && record.origin.len() <= wire::MAX_NORMALIZED_ORIGIN_BYTES
        && record.active_version > 0
        && usize::try_from(record.active_version)
            .is_ok_and(|version| version <= wire::MAX_SKILL_VERSIONS_PER_SKILL)
        && !record.definition.is_empty()
        && record.definition.len() <= wire::MAX_SKILL_DEFINITION_BYTES
        && record.step_count > 0
        && usize::try_from(record.step_count).is_ok_and(|count| count <= wire::MAX_SKILL_STEPS)
        && i64::try_from(record.installed_at_utc_ms).is_ok()
        && i64::try_from(record.updated_at_utc_ms).is_ok()
}

fn provenance(value: wire::SkillProvenance) -> Option<ProcedureProvenance> {
    match value {
        wire::SkillProvenance::Authored => Some(ProcedureProvenance::Authored),
        wire::SkillProvenance::RecordedFromTask => Some(ProcedureProvenance::RecordedFromTask),
        wire::SkillProvenance::InstalledFromPack => None,
    }
}

const fn status(value: wire::SkillStatus) -> ProcedureStatus {
    match value {
        wire::SkillStatus::Draft => ProcedureStatus::Draft,
        wire::SkillStatus::Active => ProcedureStatus::Active,
        wire::SkillStatus::Superseded => ProcedureStatus::Superseded,
        wire::SkillStatus::Retired => ProcedureStatus::Retired,
        wire::SkillStatus::Disabled => ProcedureStatus::Disabled,
    }
}

fn version_id(procedure: &Procedure) -> String {
    format!("{}@{}", procedure.id.as_str(), procedure.version.0)
}

fn validate_recall(
    recall: &[wire::SkillRunRecord],
    procedures: &[CataloguedProcedure],
) -> Result<(), ProcedureCatalogueError> {
    let mut previous = u64::MAX;
    let mut task_ids = BTreeSet::new();
    for run in recall {
        if run.ran_at_utc_ms > previous {
            return Err(ProcedureCatalogueError::RecallOutOfOrder);
        }
        previous = run.ran_at_utc_ms;
        let installation = procedures
            .iter()
            .find(|entry| entry.procedure.id.as_str() == run.skill_id);
        if run.skill_id.is_empty()
            || run.skill_id.len() > wire::MAX_SKILL_ID_BYTES
            || run.task_id.is_empty()
            || run.task_id.len() > wire::MAX_IDENTIFIER_BYTES
            || !task_ids.insert(run.task_id.as_str())
            || run.version == 0
            || !usize::try_from(run.version)
                .is_ok_and(|version| version <= wire::MAX_SKILL_VERSIONS_PER_SKILL)
            || i64::try_from(run.ran_at_utc_ms).is_err()
            || installation.is_none()
            || installation.is_some_and(|entry| run.version > entry.procedure.version.0)
        {
            return Err(ProcedureCatalogueError::InvalidRecall);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use bip_types::snapshot::SemanticRole;
    use policy_engine::origin::normalize_serialization;
    use procedure_engine::{build_source_table, encode, PageNode};

    fn record(status: wire::SkillStatus) -> wire::SkillRecord {
        let origin = normalize_serialization("https://example.test").unwrap();
        let mut procedure = build_source_table(&origin).unwrap();
        procedure.status = match status {
            wire::SkillStatus::Active => ProcedureStatus::Active,
            wire::SkillStatus::Draft => ProcedureStatus::Draft,
            _ => ProcedureStatus::Disabled,
        };
        wire::SkillRecord {
            skill_id: procedure.id.as_str().to_owned(),
            origin: procedure.scope.origin().display(),
            provenance: wire::SkillProvenance::Authored,
            status,
            active_version: procedure.version.0,
            definition: encode(&procedure).unwrap(),
            step_count: u32::try_from(procedure.steps.len()).unwrap_or_else(|_| unreachable!()),
            installed_at_utc_ms: 1,
            updated_at_utc_ms: 2,
        }
    }

    fn facts(origin: &str) -> PageFacts {
        PageFacts::new(
            normalize_serialization(origin).unwrap(),
            vec![PageNode::new(SemanticRole::Document)],
        )
    }

    #[test]
    fn active_rows_decode_match_and_narrow_without_widening() {
        let catalogue = ProcedureCatalogue::restore(
            vec![record(wire::SkillStatus::Active)],
            Vec::new(),
            Milestone::M5,
        )
        .unwrap();
        let offered = catalogue.matching(&facts("https://example.test"));
        assert_eq!(offered.len(), 1);
        let narrowed = catalogue
            .narrow_start(
                offered[0].skill_version_id,
                "https://example.test",
                Milestone::M5,
                &[
                    task_engine::REVIEWED_OBSERVATION_TOOL.to_owned(),
                    "browser.tabs".to_owned(),
                ],
            )
            .unwrap();
        assert!(narrowed
            .iter()
            .any(|name| name == task_engine::REVIEWED_OBSERVATION_TOOL));
        assert!(!narrowed.iter().any(|name| name == "browser.tabs"));
        assert!(narrowed.iter().any(|name| name == "user.handover"));
    }

    #[test]
    fn malformed_stale_and_mismatched_rows_fail_the_whole_restore() {
        let mut malformed = record(wire::SkillStatus::Active);
        malformed.definition[4] = 0xff;
        assert!(matches!(
            ProcedureCatalogue::restore(vec![malformed], Vec::new(), Milestone::M5),
            Err(ProcedureCatalogueError::Definition(_))
        ));

        let mut mismatched = record(wire::SkillStatus::Active);
        mismatched.active_version = 2;
        assert_eq!(
            ProcedureCatalogue::restore(vec![mismatched], Vec::new(), Milestone::M5),
            Err(ProcedureCatalogueError::InvalidMetadata)
        );
    }

    #[test]
    fn inactive_rows_are_validated_but_never_offered() {
        let catalogue = ProcedureCatalogue::restore(
            vec![record(wire::SkillStatus::Draft)],
            Vec::new(),
            Milestone::M5,
        )
        .unwrap();
        assert!(catalogue
            .matching(&facts("https://example.test"))
            .is_empty());
    }

    #[test]
    fn committed_lifecycle_status_restores_without_rewriting_the_definition() {
        let mut stored = record(wire::SkillStatus::Draft);
        let definition = stored.definition.clone();
        for current in [
            wire::SkillStatus::Active,
            wire::SkillStatus::Disabled,
            wire::SkillStatus::Draft,
        ] {
            stored.status = current;
            let catalogue =
                ProcedureCatalogue::restore(vec![stored.clone()], Vec::new(), Milestone::M5)
                    .unwrap();
            assert_eq!(catalogue.procedures[0].procedure.status, status(current));
            assert_eq!(
                catalogue.matching(&facts("https://example.test")).len(),
                usize::from(current == wire::SkillStatus::Active)
            );
            assert_eq!(stored.definition, definition);
        }
    }

    #[test]
    fn oversized_metadata_is_refused_before_definition_decode() {
        let mut oversized = record(wire::SkillStatus::Active);
        oversized.definition = vec![0xff; wire::MAX_SKILL_DEFINITION_BYTES + 1];
        assert_eq!(
            ProcedureCatalogue::restore(vec![oversized], Vec::new(), Milestone::M5),
            Err(ProcedureCatalogueError::InvalidMetadata)
        );
    }

    #[test]
    fn duplicate_task_runs_are_not_accepted_as_recall() {
        let skill = record(wire::SkillStatus::Active);
        let run = wire::SkillRunRecord {
            skill_id: skill.skill_id.clone(),
            version: skill.active_version,
            task_id: "task-1".to_owned(),
            outcome: wire::SkillRunOutcome::Completed,
            ran_at_utc_ms: 10,
        };
        assert_eq!(
            ProcedureCatalogue::restore(vec![skill], vec![run.clone(), run], Milestone::M5),
            Err(ProcedureCatalogueError::InvalidRecall)
        );
    }
}
