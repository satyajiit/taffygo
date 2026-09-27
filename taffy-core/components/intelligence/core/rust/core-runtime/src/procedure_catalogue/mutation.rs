// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact-version mutation of the resident saved-skill aggregate.
//!
//! This is the only producer of durable skill installation/status/removal
//! effects. Teaching accepts the browser recorder's closed vocabulary: BIP
//! ordinals, reference-only arguments and a verb that must resolve in the
//! compiled tool registry. It has no field capable of carrying code, a CSS
//! selector, page text or a model-authored value.

use procedure_engine::{
    encode, transition, LifecycleActor, Procedure, ProcedureId, ProcedureStatus,
};

use super::{version_id, CataloguedProcedure, ProcedureCatalogue};
use core_service_types as wire;

mod decode;

use decode::decode_recording;

/// Why a person-requested skill change was refused before any effect escaped.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SkillMutationError {
    InvalidShape,
    UnknownSkill,
    SkillAlreadyExists,
    StaleVersion,
    TooManySkills,
    TooManyVersions,
    RecordingRefused,
    LifecycleRefused,
    DefinitionTooLarge,
}

/// The one browser storage body a prepared mutation needs.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SkillMutationPersistence {
    Install(wire::SkillInstallEffect),
    SetStatus(wire::SkillStatusEffect),
    Forget(wire::SkillForgetEffect),
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum CatalogueChange {
    Replace {
        expected_version: Option<u32>,
        record: wire::SkillRecord,
        procedure: Box<Procedure>,
    },
    SetStatus {
        expected_version: u32,
        expected_status: ProcedureStatus,
        status: ProcedureStatus,
        changed_at_utc_ms: u64,
    },
    Forget {
        expected_version: u32,
    },
}

/// A validated immutable plan staged until the browser confirms its write.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PreparedSkillMutation {
    skill_id: String,
    persistence: SkillMutationPersistence,
    change: CatalogueChange,
}

impl PreparedSkillMutation {
    pub fn skill_id(&self) -> &str {
        &self.skill_id
    }

    pub const fn persistence(&self) -> &SkillMutationPersistence {
        &self.persistence
    }

    pub fn committed_revision(&self) -> u64 {
        match &self.change {
            CatalogueChange::Replace { record, .. } => u64::from(record.active_version),
            CatalogueChange::SetStatus {
                expected_version, ..
            }
            | CatalogueChange::Forget { expected_version } => u64::from(*expected_version),
        }
    }
}

impl ProcedureCatalogue {
    /// Prepares an internally recorded complete flow for the same draft install.
    pub fn prepare_recorded_procedure(
        &self,
        procedure: Procedure,
        recorded_at_epoch_ms: u64,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        if recorded_at_epoch_ms == 0
            || i64::try_from(recorded_at_epoch_ms).is_err()
            || procedure.status != ProcedureStatus::Draft
            || procedure.version.0 != 1
            || procedure.provenance != procedure_engine::ProcedureProvenance::RecordedFromTask
            || procedure.recorded_from_task_id.is_none()
        {
            return Err(SkillMutationError::InvalidShape);
        }
        let task_id = procedure
            .recorded_from_task_id
            .clone()
            .ok_or(SkillMutationError::InvalidShape)?;
        let procedure = procedure
            .from_task(task_id)
            .map_err(|_| SkillMutationError::RecordingRefused)?;
        procedure_engine::validate(&procedure, task_engine::Milestone::M8)
            .map_err(|_| SkillMutationError::RecordingRefused)?;
        if self.procedures.len() >= wire::MAX_SKILLS_PER_PROFILE {
            return Err(SkillMutationError::TooManySkills);
        }
        if self
            .procedures
            .iter()
            .any(|entry| entry.procedure.id == procedure.id)
        {
            return Err(SkillMutationError::SkillAlreadyExists);
        }
        build_install_plan(recorded_at_epoch_ms, None, procedure)
    }

    /// Validates one complete command without mutating resident state.
    pub fn prepare_mutation(
        &self,
        command: &wire::MutateSkillCommand,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        if command.recorded_at_epoch_ms == 0
            || i64::try_from(command.recorded_at_epoch_ms).is_err()
            || ProcedureId::new(command.skill_id.clone()).is_err()
        {
            return Err(SkillMutationError::InvalidShape);
        }
        match command.kind {
            wire::SkillMutationKind::Teach => self.prepare_teach(command),
            wire::SkillMutationKind::Update => self.prepare_update(command),
            wire::SkillMutationKind::SetEnabled => self.prepare_status(command),
            wire::SkillMutationKind::Remove => self.prepare_remove(command),
        }
    }

    /// Installs a completed write only if resident state is still exactly the
    /// state the prepared plan was decided against.
    pub fn install_mutation(
        &mut self,
        prepared: PreparedSkillMutation,
    ) -> Result<(), SkillMutationError> {
        let current = self
            .procedures
            .iter()
            .position(|entry| entry.procedure.id.as_str() == prepared.skill_id);
        match prepared.change {
            CatalogueChange::Replace {
                expected_version,
                record,
                procedure,
            } => {
                if current
                    .and_then(|index| self.procedures.get(index))
                    .map(|entry| entry.procedure.version.0)
                    != expected_version
                {
                    return Err(SkillMutationError::StaleVersion);
                }
                let entry = CataloguedProcedure {
                    version_id: version_id(&procedure),
                    procedure: *procedure,
                    installed_at_utc_ms: record.installed_at_utc_ms,
                    updated_at_utc_ms: record.updated_at_utc_ms,
                };
                if let Some(index) = current {
                    let Some(slot) = self.procedures.get_mut(index) else {
                        return Err(SkillMutationError::StaleVersion);
                    };
                    *slot = entry;
                } else {
                    self.procedures.push(entry);
                    self.procedures
                        .sort_by(|left, right| left.procedure.id.cmp(&right.procedure.id));
                }
            }
            CatalogueChange::SetStatus {
                expected_version,
                expected_status,
                status,
                changed_at_utc_ms,
            } => {
                let Some(index) = current else {
                    return Err(SkillMutationError::UnknownSkill);
                };
                let Some(entry) = self.procedures.get_mut(index) else {
                    return Err(SkillMutationError::UnknownSkill);
                };
                if entry.procedure.version.0 != expected_version
                    || entry.procedure.status != expected_status
                {
                    return Err(SkillMutationError::StaleVersion);
                }
                entry.procedure.status = status;
                entry.updated_at_utc_ms = changed_at_utc_ms;
            }
            CatalogueChange::Forget { expected_version } => {
                let Some(index) = current else {
                    return Err(SkillMutationError::UnknownSkill);
                };
                if self
                    .procedures
                    .get(index)
                    .is_none_or(|entry| entry.procedure.version.0 != expected_version)
                {
                    return Err(SkillMutationError::StaleVersion);
                }
                self.procedures.remove(index);
                self.recall.retain(|run| run.skill_id != prepared.skill_id);
            }
        }
        Ok(())
    }

    fn prepare_teach(
        &self,
        command: &wire::MutateSkillCommand,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        if command.expected_version != 0
            || command.enabled
            || self.procedures.len() >= wire::MAX_SKILLS_PER_PROFILE
        {
            return Err(if self.procedures.len() >= wire::MAX_SKILLS_PER_PROFILE {
                SkillMutationError::TooManySkills
            } else {
                SkillMutationError::InvalidShape
            });
        }
        if self
            .procedures
            .iter()
            .any(|entry| entry.procedure.id.as_str() == command.skill_id)
        {
            return Err(SkillMutationError::SkillAlreadyExists);
        }
        let procedure = decode_recording(command)?;
        build_install_plan(command.recorded_at_epoch_ms, None, procedure)
    }

    fn prepare_update(
        &self,
        command: &wire::MutateSkillCommand,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        if command.expected_version == 0 || command.enabled {
            return Err(SkillMutationError::InvalidShape);
        }
        let current = self.current(command)?;
        if current.procedure.version.0 as usize >= wire::MAX_SKILL_VERSIONS_PER_SKILL {
            return Err(SkillMutationError::TooManyVersions);
        }
        if current.procedure.scope.origin().display() != command.origin {
            return Err(SkillMutationError::InvalidShape);
        }
        let mut procedure = decode_recording(command)?;
        procedure.version = current
            .procedure
            .version
            .next()
            .ok_or(SkillMutationError::TooManyVersions)?;
        procedure.provenance = current.procedure.provenance;
        procedure
            .recorded_from_task_id
            .clone_from(&current.procedure.recorded_from_task_id);
        build_install_plan(
            command.recorded_at_epoch_ms,
            Some((current.procedure.version.0, current.installed_at_utc_ms)),
            procedure,
        )
    }

    fn prepare_status(
        &self,
        command: &wire::MutateSkillCommand,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        if !mutation_payload_is_empty(command) {
            return Err(SkillMutationError::InvalidShape);
        }
        let current = self.current(command)?;
        let next = if command.enabled {
            match current.procedure.status {
                ProcedureStatus::Disabled => ProcedureStatus::Draft,
                ProcedureStatus::Draft => ProcedureStatus::Active,
                _ => return Err(SkillMutationError::LifecycleRefused),
            }
        } else {
            ProcedureStatus::Disabled
        };
        transition(current.procedure.status, next, LifecycleActor::Person)
            .map_err(|_| SkillMutationError::LifecycleRefused)?;
        Ok(PreparedSkillMutation {
            skill_id: command.skill_id.clone(),
            persistence: SkillMutationPersistence::SetStatus(wire::SkillStatusEffect {
                skill_id: command.skill_id.clone(),
                status: status_to_wire(next),
                changed_at_utc_ms: command.recorded_at_epoch_ms,
                version: command.expected_version,
            }),
            change: CatalogueChange::SetStatus {
                expected_version: command.expected_version,
                expected_status: current.procedure.status,
                status: next,
                changed_at_utc_ms: command.recorded_at_epoch_ms,
            },
        })
    }

    fn prepare_remove(
        &self,
        command: &wire::MutateSkillCommand,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        if command.enabled || !mutation_payload_is_empty(command) {
            return Err(SkillMutationError::InvalidShape);
        }
        self.current(command)?;
        Ok(PreparedSkillMutation {
            skill_id: command.skill_id.clone(),
            persistence: SkillMutationPersistence::Forget(wire::SkillForgetEffect {
                skill_id: command.skill_id.clone(),
            }),
            change: CatalogueChange::Forget {
                expected_version: command.expected_version,
            },
        })
    }

    fn current(
        &self,
        command: &wire::MutateSkillCommand,
    ) -> Result<&CataloguedProcedure, SkillMutationError> {
        let current = self
            .procedures
            .iter()
            .find(|entry| entry.procedure.id.as_str() == command.skill_id)
            .ok_or(SkillMutationError::UnknownSkill)?;
        if command.expected_version == 0 || current.procedure.version.0 != command.expected_version
        {
            return Err(SkillMutationError::StaleVersion);
        }
        Ok(current)
    }
}

fn build_install_plan(
    recorded_at_epoch_ms: u64,
    previous: Option<(u32, u64)>,
    procedure: Procedure,
) -> Result<PreparedSkillMutation, SkillMutationError> {
    let definition = encode(&procedure).map_err(|_| SkillMutationError::RecordingRefused)?;
    if definition.is_empty() || definition.len() > wire::MAX_SKILL_DEFINITION_BYTES {
        return Err(SkillMutationError::DefinitionTooLarge);
    }
    let step_count =
        u32::try_from(procedure.steps.len()).map_err(|_| SkillMutationError::InvalidShape)?;
    let installed_at_utc_ms = previous.map_or(recorded_at_epoch_ms, |(_, installed)| installed);
    let record = wire::SkillRecord {
        skill_id: procedure.id.as_str().to_owned(),
        origin: procedure.scope.origin().display(),
        provenance: provenance_to_wire(procedure.provenance),
        status: wire::SkillStatus::Draft,
        active_version: procedure.version.0,
        definition: definition.clone(),
        step_count,
        installed_at_utc_ms,
        updated_at_utc_ms: recorded_at_epoch_ms,
    };
    Ok(PreparedSkillMutation {
        skill_id: procedure.id.as_str().to_owned(),
        persistence: SkillMutationPersistence::Install(wire::SkillInstallEffect {
            skill_id: procedure.id.as_str().to_owned(),
            origin: procedure.scope.origin().display(),
            provenance: record.provenance,
            version: procedure.version.0,
            definition,
            step_count,
            recorded_at_utc_ms: recorded_at_epoch_ms,
        }),
        change: CatalogueChange::Replace {
            expected_version: previous.map(|(version, _)| version),
            record,
            procedure: Box::new(procedure),
        },
    })
}

fn mutation_payload_is_empty(command: &wire::MutateSkillCommand) -> bool {
    command.origin.is_empty()
        && command.clauses.is_empty()
        && command.steps.is_empty()
        && command.admitted == 0
}

const fn provenance_to_wire(value: procedure_engine::ProcedureProvenance) -> wire::SkillProvenance {
    match value {
        procedure_engine::ProcedureProvenance::Authored => wire::SkillProvenance::Authored,
        procedure_engine::ProcedureProvenance::RecordedFromTask => {
            wire::SkillProvenance::RecordedFromTask
        }
        procedure_engine::ProcedureProvenance::InstalledFromPack => {
            wire::SkillProvenance::InstalledFromPack
        }
    }
}

const fn status_to_wire(value: ProcedureStatus) -> wire::SkillStatus {
    match value {
        ProcedureStatus::Draft => wire::SkillStatus::Draft,
        ProcedureStatus::Active => wire::SkillStatus::Active,
        ProcedureStatus::Superseded => wire::SkillStatus::Superseded,
        ProcedureStatus::Retired => wire::SkillStatus::Retired,
        ProcedureStatus::Disabled => wire::SkillStatus::Disabled,
    }
}
