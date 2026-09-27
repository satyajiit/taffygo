// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One procedure, written down.
//!
//! The writer refuses as it goes rather than at the end. A caller that handed
//! this a record far past the bound would otherwise have the whole of it
//! assembled in memory before being told it is not storable, and the number in
//! [`EncodeError::TooLarge`] would be the only thing that assembly bought.
//!
//! Every closed vocabulary is written as the name it already carries for an
//! audit record — `label()` here, `wire()` for the two enumerations
//! `bip_types` owns — so this file names no discriminant and no position. See
//! the module header of [`super`] for why that is the whole design and not a
//! preference about readability.

use task_engine::tool::ArgumentValue;

use crate::matching::{MatchClause, MatchCondition};
use crate::record::Procedure;
use crate::step::{ProcedureStep, StepArgument, StepValue};

use super::{EncodeError, DEFINITION_FORMAT_VERSION, MAGIC, MAX_DEFINITION_BYTES};

/// The bytes `procedure` is stored as, or why it cannot be stored.
///
/// The result is what `core_skill_version.definition` holds, and
/// [`Procedure::steps`]'s length is what `core_skill_version.step_count`
/// holds; [`super::step_count`] reads it back out of these bytes so the two
/// columns can be written from one place.
pub fn encode(procedure: &Procedure) -> Result<Vec<u8>, EncodeError> {
    let steps = u16::try_from(procedure.steps.len()).map_err(|_| EncodeError::TooManySteps {
        steps: procedure.steps.len(),
    })?;
    let mut writer = Writer::new();
    writer.raw(&MAGIC)?;
    writer.u16(DEFINITION_FORMAT_VERSION)?;
    writer.u16(steps)?;
    writer.text(procedure.id.as_str())?;
    writer.u32(procedure.version.0)?;
    writer.text(procedure.status.label())?;
    writer.text(procedure.provenance.label())?;
    match procedure.recorded_from_task_id.as_deref() {
        None => writer.u8(0)?,
        Some(task_id) => {
            writer.u8(1)?;
            writer.text(task_id)?;
        }
    }
    // The canonical serialization, which is the only spelling `ProcedureScope`
    // admits — so what is written is what `ProcedureScope::for_origin` will
    // accept back, and a second spelling of one origin cannot enter through
    // this door either.
    writer.text(&procedure.scope.origin().display())?;
    condition(&mut writer, &procedure.condition)?;
    for step in &procedure.steps {
        one_step(&mut writer, step)?;
    }
    Ok(writer.into_bytes())
}

/// When the procedure applies.
fn condition(writer: &mut Writer, condition: &MatchCondition) -> Result<(), EncodeError> {
    let clauses = condition.clauses();
    let count = u16::try_from(clauses.len()).map_err(|_| EncodeError::TooManyClauses {
        clauses: clauses.len(),
    })?;
    writer.u16(count)?;
    for clause in clauses {
        writer.text(clause.label())?;
        // Every shape is about a role, so the role is written once rather than
        // in each arm.
        writer.text(clause.role().wire())?;
        match *clause {
            MatchClause::RolePresent(_) => {}
            MatchClause::PhraseAt { phrase, .. } => writer.text(phrase.label())?,
            MatchClause::StateAt { state, .. } => writer.text(state.wire())?,
        }
    }
    Ok(())
}

/// One step: a verb, what it expects, what it fills, and its arguments.
fn one_step(writer: &mut Writer, step: &ProcedureStep) -> Result<(), EncodeError> {
    writer.text(&step.verb)?;
    writer.text(step.postcondition.wire())?;
    match step.fills {
        None => writer.u8(0)?,
        Some(purpose) => {
            writer.u8(1)?;
            writer.text(purpose.label())?;
        }
    }
    let count = u16::try_from(step.arguments.len()).map_err(|_| EncodeError::TooManyArguments {
        arguments: step.arguments.len(),
    })?;
    writer.u16(count)?;
    for argument in &step.arguments {
        one_argument(writer, argument)?;
    }
    Ok(())
}

/// One argument: the parameter it names and the shape supplied for it.
fn one_argument(writer: &mut Writer, argument: &StepArgument) -> Result<(), EncodeError> {
    writer.text(&argument.name)?;
    writer.text(argument.value.label())?;
    match &argument.value {
        StepValue::SemanticTarget { role, phrase } => {
            writer.text(role.wire())?;
            writer.text(phrase.label())
        }
        StepValue::Literal(value) => literal(writer, value),
        StepValue::FromEarlierStep { step } => {
            let index = u32::try_from(*step)
                .map_err(|_| EncodeError::StepReferenceTooLarge { step: *step })?;
            writer.u32(index)
        }
        StepValue::FromPerson { purpose } => writer.text(purpose.label()),
    }
}

/// One value the record carries.
fn literal(writer: &mut Writer, value: &ArgumentValue) -> Result<(), EncodeError> {
    writer.text(value.type_label())?;
    match value {
        ArgumentValue::Handle(number) | ArgumentValue::SuppliedValue(number) => writer.u32(*number),
        ArgumentValue::Text(text) | ArgumentValue::Address(text) | ArgumentValue::Choice(text) => {
            writer.text(text)
        }
        ArgumentValue::Count(count) => writer.u64(*count),
        ArgumentValue::Flag(flag) => writer.u8(u8::from(*flag)),
    }
}

/// Bytes, big-endian, and never more of them than the column can hold.
struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    const fn new() -> Self {
        Self { bytes: Vec::new() }
    }

    fn raw(&mut self, bytes: &[u8]) -> Result<(), EncodeError> {
        self.bytes.extend_from_slice(bytes);
        if self.bytes.len() > MAX_DEFINITION_BYTES {
            return Err(EncodeError::TooLarge {
                bytes: self.bytes.len(),
            });
        }
        Ok(())
    }

    fn u8(&mut self, value: u8) -> Result<(), EncodeError> {
        self.raw(&[value])
    }

    fn u16(&mut self, value: u16) -> Result<(), EncodeError> {
        self.raw(&value.to_be_bytes())
    }

    fn u32(&mut self, value: u32) -> Result<(), EncodeError> {
        self.raw(&value.to_be_bytes())
    }

    fn u64(&mut self, value: u64) -> Result<(), EncodeError> {
        self.raw(&value.to_be_bytes())
    }

    /// A length-prefixed string, which is also how every closed name travels.
    fn text(&mut self, value: &str) -> Result<(), EncodeError> {
        let length = u16::try_from(value.len())
            .map_err(|_| EncodeError::TextTooLong { bytes: value.len() })?;
        self.u16(length)?;
        self.raw(value.as_bytes())
    }

    fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }
}

#[cfg(test)]
mod tests {
    use super::{encode, Writer};
    use crate::definition::{EncodeError, MAX_DEFINITION_BYTES};
    use crate::matching::{MatchClause, MatchCondition};
    use crate::record::{Procedure, ProcedureId, ProcedureProvenance, ProcedureScope};
    use crate::step::{ProcedureStep, StepArgument, StepValue};
    use bip_types::action::PostconditionKind;
    use bip_types::snapshot::SemanticRole;
    use task_engine::tool::ArgumentValue;

    fn procedure(steps: Vec<ProcedureStep>) -> Procedure {
        Procedure::draft(
            ProcedureId::new("one").unwrap(),
            ProcedureScope::for_origin("https://example.test").unwrap(),
            MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::SearchField)]),
            steps,
            ProcedureProvenance::Authored,
        )
    }

    #[test]
    fn a_definition_past_the_column_is_refused_and_never_truncated() {
        // Two literals the column could not hold together. The refusal names
        // the bound; a truncating encoder would have produced a *different*
        // procedure still claiming this one's identity, which is the worst of
        // the available failures.
        let long = "x".repeat(40_000);
        let step =
            ProcedureStep::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
                StepArgument::literal("text", ArgumentValue::Text(long.clone())),
                StepArgument::literal("role", ArgumentValue::Choice(long)),
            ]);
        match encode(&procedure(vec![step])) {
            Err(EncodeError::TooLarge { bytes }) => {
                assert!(bytes > MAX_DEFINITION_BYTES, "{bytes}");
            }
            other => unreachable!("a definition past the column is refused: {other:?}"),
        }
    }

    #[test]
    fn a_definition_at_the_column_is_written() {
        // The bound refuses what is past it and nothing else, so a large
        // definition that fits is stored rather than rounded down to a safer
        // one.
        let step =
            ProcedureStep::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
                StepArgument::literal("text", ArgumentValue::Text("x".repeat(60_000))),
            ]);
        let Ok(bytes) = encode(&procedure(vec![step])) else {
            unreachable!("a definition inside the column is written")
        };
        assert!(bytes.len() <= MAX_DEFINITION_BYTES);
        assert!(bytes.len() > 60_000);
    }

    #[test]
    fn a_string_longer_than_the_prefix_can_count_is_refused_by_its_own_name() {
        let mut writer = Writer::new();
        let long = "x".repeat(usize::from(u16::MAX) + 1);
        assert_eq!(
            writer.text(&long),
            Err(EncodeError::TextTooLong { bytes: long.len() })
        );
    }

    #[test]
    fn a_step_reference_past_what_a_definition_holds_is_refused() {
        let step =
            ProcedureStep::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
                StepArgument::new("within", StepValue::FromEarlierStep { step: usize::MAX }),
            ]);
        assert_eq!(
            encode(&procedure(vec![step])),
            Err(EncodeError::StepReferenceTooLarge { step: usize::MAX })
        );
    }
}
