// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed-vocabulary decoding for browser-observed skill recordings.

use bip_types::action::PostconditionKind;
use bip_types::snapshot::{NodeState, SemanticRole};
use core_service_types as wire;
use procedure_engine::{
    record_procedure, ArgumentDescriptor, FieldPurpose, LedgerEntry, MatchClause, PhraseId,
    Procedure, ProcedureId, RecordedValue, Recording, StepDescriptor,
};
use task_engine::tool::Milestone;

use super::SkillMutationError;

pub(super) fn decode_recording(
    command: &wire::MutateSkillCommand,
) -> Result<Procedure, SkillMutationError> {
    if command.origin.is_empty()
        || command.origin.len() > wire::MAX_NORMALIZED_ORIGIN_BYTES
        || command.clauses.is_empty()
        || command.clauses.len() > wire::MAX_SKILL_MATCH_CLAUSES
        || command.steps.is_empty()
        || command.steps.len() > wire::MAX_SKILL_STEPS
        || usize::try_from(command.admitted).ok() != Some(command.steps.len())
    {
        return Err(SkillMutationError::InvalidShape);
    }
    let origin = policy_engine::origin::normalize_serialization(&command.origin)
        .map_err(|_| SkillMutationError::InvalidShape)?;
    if origin.display() != command.origin {
        return Err(SkillMutationError::InvalidShape);
    }
    let clauses = command
        .clauses
        .iter()
        .map(decode_clause)
        .collect::<Result<Vec<_>, _>>()?;
    let entries = command
        .steps
        .iter()
        .map(decode_step)
        .collect::<Result<Vec<_>, _>>()?;
    let recording = Recording::new(origin, clauses, entries, command.steps.len());
    let id =
        ProcedureId::new(command.skill_id.clone()).map_err(|_| SkillMutationError::InvalidShape)?;
    record_procedure(id, &recording, Milestone::M8)
        .map_err(|_| SkillMutationError::RecordingRefused)
}

fn decode_clause(value: &wire::SkillObservedClause) -> Result<MatchClause, SkillMutationError> {
    let role = closed(SemanticRole::ALL, value.role)?;
    match value.kind {
        wire::SkillClauseKind::RolePresent if value.detail == 0 => {
            Ok(MatchClause::RolePresent(role))
        }
        wire::SkillClauseKind::PhraseAt => Ok(MatchClause::PhraseAt {
            role,
            phrase: closed(PhraseId::ALL, value.detail)?,
        }),
        wire::SkillClauseKind::StateAt => Ok(MatchClause::StateAt {
            role,
            state: closed(NodeState::ALL, value.detail)?,
        }),
        wire::SkillClauseKind::RolePresent => Err(SkillMutationError::InvalidShape),
    }
}

fn decode_step(value: &wire::SkillObservedStep) -> Result<LedgerEntry, SkillMutationError> {
    if value.verb.is_empty()
        || value.verb.len() > wire::MAX_TOOL_ID_BYTES
        || value.arguments.len() > wire::MAX_SKILL_ARGUMENTS_PER_STEP
    {
        return Err(SkillMutationError::InvalidShape);
    }
    let arguments = value
        .arguments
        .iter()
        .map(decode_argument)
        .collect::<Result<Vec<_>, _>>()?;
    let mut descriptor = StepDescriptor::new(
        value.verb.clone(),
        closed(PostconditionKind::ALL, value.postcondition)?,
    )
    .taking(arguments);
    if value.has_fill {
        descriptor = descriptor.filling(closed(FieldPurpose::ALL, value.fill_purpose)?);
    } else if value.fill_purpose != 0 {
        return Err(SkillMutationError::InvalidShape);
    }
    Ok(LedgerEntry::Described(descriptor))
}

fn decode_argument(
    value: &wire::SkillObservedArgument,
) -> Result<ArgumentDescriptor, SkillMutationError> {
    let parameter =
        usize::try_from(value.parameter).map_err(|_| SkillMutationError::InvalidShape)?;
    let public = value.kind == wire::SkillArgumentKind::PublicAddress;
    let semantic = value.kind == wire::SkillArgumentKind::SemanticTarget;
    if public != value.public_address.is_some() || semantic != value.semantic_target.is_some() {
        return Err(SkillMutationError::InvalidShape);
    }
    let decoded = match value.kind {
        wire::SkillArgumentKind::PublicAddress if value.value == 0 && value.purpose == 0 => {
            RecordedValue::PublicAddress(
                value
                    .public_address
                    .clone()
                    .ok_or(SkillMutationError::InvalidShape)?,
            )
        }
        wire::SkillArgumentKind::SemanticTarget if value.value == 0 && value.purpose == 0 => {
            let target = value
                .semantic_target
                .as_ref()
                .ok_or(SkillMutationError::InvalidShape)?;
            RecordedValue::SemanticTarget {
                role: closed(SemanticRole::ALL, target.role)?,
                phrase: closed(PhraseId::ALL, target.phrase)?,
            }
        }
        wire::SkillArgumentKind::FromEarlierStep if value.purpose == 0 => {
            RecordedValue::FromEarlierStep {
                step: usize::try_from(value.value).map_err(|_| SkillMutationError::InvalidShape)?,
            }
        }
        wire::SkillArgumentKind::FromPerson if value.value == 0 => RecordedValue::FromPerson {
            purpose: closed(FieldPurpose::ALL, value.purpose)?,
        },
        wire::SkillArgumentKind::Choice if value.purpose == 0 => RecordedValue::Choice {
            index: usize::try_from(value.value).map_err(|_| SkillMutationError::InvalidShape)?,
        },
        wire::SkillArgumentKind::Count if value.purpose == 0 => RecordedValue::Count(value.value),
        wire::SkillArgumentKind::Flag if value.purpose == 0 && value.value <= 1 => {
            RecordedValue::Flag(value.value == 1)
        }
        _ => return Err(SkillMutationError::InvalidShape),
    };
    Ok(ArgumentDescriptor::new(parameter, decoded))
}

fn closed<T: Copy>(values: &[T], ordinal: u32) -> Result<T, SkillMutationError> {
    usize::try_from(ordinal)
        .ok()
        .and_then(|index| values.get(index))
        .copied()
        .ok_or(SkillMutationError::InvalidShape)
}
