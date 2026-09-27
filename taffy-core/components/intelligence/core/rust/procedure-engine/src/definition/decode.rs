// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bytes back as one procedure, or a named reason they are not one.
//!
//! # Every read is bounded and every name is looked up
//!
//! The reader holds what is left of the blob and hands out slices by
//! [`slice::split_at`] after checking the length, so a definition that ends
//! inside something it declared is [`DecodeError::Truncated`] rather than a
//! read past the end. Nothing here indexes.
//!
//! Every closed name is resolved against that enumeration's own `ALL` (or
//! `from_wire`, for the two `bip_types` owns) and a name no member claims is
//! [`DecodeError::UnknownMember`] naming the vocabulary. There is no arm that
//! falls back to a member, and none that skips a field it could not read: a
//! definition read in part is a procedure that will be replayed, and the part
//! that did not survive is where it does something other than what the person
//! agreed to.
//!
//! # The parts that have their own constructors go through them
//!
//! The identifier and the scope are re-made by [`ProcedureId::new`] and
//! [`ProcedureScope::for_origin`] rather than dropped into the struct, so a
//! stored scope that is not a canonical tuple origin is refused at load
//! exactly as it would have been at first writing. This is the load half of
//! decision 0091 section 2: the database can no longer check anything inside a
//! definition, so this crate checks at store and again at load.

use bip_types::action::PostconditionKind;
use bip_types::snapshot::{NodeState, SemanticRole};
use task_engine::tool::ArgumentValue;

use crate::field::FieldPurpose;
use crate::matching::{MatchClause, MatchCondition, PhraseId};
use crate::record::{
    Procedure, ProcedureId, ProcedureProvenance, ProcedureScope, ProcedureVersion,
};
use crate::status::ProcedureStatus;
use crate::step::{ProcedureStep, StepArgument, StepValue};

use super::{DecodeError, Enumeration, DEFINITION_FORMAT_VERSION, MAGIC, MAX_DEFINITION_BYTES};

/// The layout `bytes` claims, whether or not this build has it.
///
/// Reported rather than refused, so a caller can name the version a stored
/// definition was written under when it turns out to be one this build cannot
/// read.
pub fn format_version(bytes: &[u8]) -> Result<u16, DecodeError> {
    let mut reader = bounded(bytes)?;
    let magic = reader.take(MAGIC.len())?;
    if magic != MAGIC {
        return Err(DecodeError::NotADefinition);
    }
    reader.u16()
}

/// How many steps the definition in `bytes` holds.
///
/// This is what `core_skill_version.step_count` mirrors. It is read from the
/// header without decoding the record, because the column exists so that a
/// bound can be enforced by the database that holds the blob rather than by
/// whoever remembers to look inside it.
pub fn step_count(bytes: &[u8]) -> Result<u16, DecodeError> {
    let mut reader = bounded(bytes)?;
    header(&mut reader)
}

/// The procedure `bytes` hold, or why they hold none.
pub fn decode(bytes: &[u8]) -> Result<Procedure, DecodeError> {
    let format = format_version(bytes)?;
    let mut reader = bounded(bytes)?;
    let steps = header(&mut reader)?;
    let procedure = body(&mut reader, steps, format)?;
    if !reader.is_empty() {
        return Err(DecodeError::TrailingBytes);
    }
    Ok(procedure)
}

/// The same, checked against the count stored beside the blob.
///
/// Two answers to how many steps a procedure has is one answer too many. The
/// column is what a store can enforce a bound with and the header is what the
/// definition says about itself; a disagreement means one of the two is not
/// about this record, and the caller is told both numbers rather than handed
/// whichever one this function happened to prefer.
pub fn decode_beside(bytes: &[u8], step_count_column: u32) -> Result<Procedure, DecodeError> {
    let definition = step_count(bytes)?;
    if u32::from(definition) != step_count_column {
        return Err(DecodeError::StepCountNotTheColumns {
            column: step_count_column,
            definition,
        });
    }
    decode(bytes)
}

/// A reader over a blob the column could have held.
fn bounded(bytes: &[u8]) -> Result<Reader<'_>, DecodeError> {
    if bytes.len() > MAX_DEFINITION_BYTES {
        return Err(DecodeError::TooLarge { bytes: bytes.len() });
    }
    Ok(Reader::new(bytes))
}

/// The magic, the format version, and the step count.
fn header(reader: &mut Reader<'_>) -> Result<u16, DecodeError> {
    let magic = reader.take(MAGIC.len())?;
    if magic != MAGIC {
        return Err(DecodeError::NotADefinition);
    }
    let version = reader.u16()?;
    if version != 1 && version != DEFINITION_FORMAT_VERSION {
        return Err(DecodeError::UnsupportedFormatVersion { found: version });
    }
    reader.u16()
}

/// Everything after the header.
fn body(reader: &mut Reader<'_>, steps: u16, format: u16) -> Result<Procedure, DecodeError> {
    let id = ProcedureId::new(reader.text()?).map_err(DecodeError::Record)?;
    let version = ProcedureVersion(reader.u32()?);
    let status = member(
        ProcedureStatus::ALL,
        reader.text()?,
        ProcedureStatus::label,
        Enumeration::ProcedureStatus,
    )?;
    let provenance = member(
        ProcedureProvenance::ALL,
        reader.text()?,
        ProcedureProvenance::label,
        Enumeration::ProcedureProvenance,
    )?;
    let recorded_from_task_id = if format >= 2 {
        match reader.u8()? {
            0 => None,
            1 => Some(reader.text()?.to_owned()),
            found => return Err(DecodeError::OptionTagUnknown { found }),
        }
    } else {
        None
    };
    let scope = ProcedureScope::for_origin(reader.text()?).map_err(DecodeError::Record)?;
    let condition = condition(reader)?;
    let mut list: Vec<ProcedureStep> = Vec::with_capacity(usize::from(steps));
    for _ in 0..steps {
        list.push(one_step(reader)?);
    }
    let procedure = Procedure {
        id,
        version,
        status,
        scope,
        condition,
        steps: list,
        provenance,
        recorded_from_task_id: None,
    };
    match recorded_from_task_id {
        Some(task_id) => procedure.from_task(task_id).map_err(DecodeError::Record),
        None => Ok(procedure),
    }
}

/// When the procedure applies.
fn condition(reader: &mut Reader<'_>) -> Result<MatchCondition, DecodeError> {
    let count = reader.u16()?;
    let mut clauses: Vec<MatchClause> = Vec::with_capacity(usize::from(count));
    for _ in 0..count {
        let shape = reader.text()?;
        let role = SemanticRole::from_wire(reader.text()?).ok_or(DecodeError::UnknownMember {
            enumeration: Enumeration::SemanticRole,
        })?;
        // The three names are `MatchClause::label`'s, and
        // `every_shape_is_read_back_under_the_name_it_is_written_under` is
        // what keeps that true when one of them is renamed.
        clauses.push(match shape {
            "role_present" => MatchClause::RolePresent(role),
            "phrase_at" => MatchClause::PhraseAt {
                role,
                phrase: member(
                    PhraseId::ALL,
                    reader.text()?,
                    PhraseId::label,
                    Enumeration::Phrase,
                )?,
            },
            "state_at" => MatchClause::StateAt {
                role,
                state: NodeState::from_wire(reader.text()?).ok_or(DecodeError::UnknownMember {
                    enumeration: Enumeration::NodeState,
                })?,
            },
            _ => {
                return Err(DecodeError::UnknownMember {
                    enumeration: Enumeration::MatchClauseShape,
                })
            }
        });
    }
    Ok(MatchCondition::new(clauses))
}

/// One step: a verb, what it expects, what it fills, and its arguments.
fn one_step(reader: &mut Reader<'_>) -> Result<ProcedureStep, DecodeError> {
    let verb = reader.text()?.to_owned();
    let postcondition =
        PostconditionKind::from_wire(reader.text()?).ok_or(DecodeError::UnknownMember {
            enumeration: Enumeration::PostconditionKind,
        })?;
    let fills = match reader.u8()? {
        0 => None,
        1 => Some(member(
            FieldPurpose::ALL,
            reader.text()?,
            FieldPurpose::label,
            Enumeration::FieldPurpose,
        )?),
        found => return Err(DecodeError::OptionTagUnknown { found }),
    };
    let count = reader.u16()?;
    let mut arguments: Vec<StepArgument> = Vec::with_capacity(usize::from(count));
    for _ in 0..count {
        arguments.push(one_argument(reader)?);
    }
    let step = ProcedureStep::new(verb, postcondition).taking(arguments);
    Ok(match fills {
        Some(purpose) => step.filling(purpose),
        None => step,
    })
}

/// One argument: the parameter it names and the shape supplied for it.
fn one_argument(reader: &mut Reader<'_>) -> Result<StepArgument, DecodeError> {
    let name = reader.text()?.to_owned();
    let shape = reader.text()?;
    // `StepValue::label`'s three names. There is no arm for a fourth shape,
    // and adding one to the type without adding one here is a decode that
    // refuses rather than a decode that guesses.
    let value = match shape {
        "semantic_target" => StepValue::SemanticTarget {
            role: SemanticRole::from_wire(reader.text()?).ok_or(DecodeError::UnknownMember {
                enumeration: Enumeration::SemanticRole,
            })?,
            phrase: member(
                PhraseId::ALL,
                reader.text()?,
                PhraseId::label,
                Enumeration::Phrase,
            )?,
        },
        "literal" => StepValue::Literal(literal(reader)?),
        "from_earlier_step" => {
            let earlier = reader.u32()?;
            StepValue::FromEarlierStep {
                // Written as a conversion rather than a cast so that a target
                // whose `usize` is narrower refuses the definition instead of
                // reading a different step reference out of it.
                step: usize::try_from(earlier)
                    .map_err(|_| DecodeError::StepReferenceUnreadable { step: earlier })?,
            }
        }
        "from_person" => StepValue::FromPerson {
            purpose: member(
                FieldPurpose::ALL,
                reader.text()?,
                FieldPurpose::label,
                Enumeration::FieldPurpose,
            )?,
        },
        _ => {
            return Err(DecodeError::UnknownMember {
                enumeration: Enumeration::StepValueShape,
            })
        }
    };
    Ok(StepArgument::new(name, value))
}

/// One value the record carries.
fn literal(reader: &mut Reader<'_>) -> Result<ArgumentValue, DecodeError> {
    // `ArgumentValue::type_label`'s seven names.
    match reader.text()? {
        "handle" => Ok(ArgumentValue::Handle(reader.u32()?)),
        "text" => Ok(ArgumentValue::Text(reader.text()?.to_owned())),
        "address" => Ok(ArgumentValue::Address(reader.text()?.to_owned())),
        "count" => Ok(ArgumentValue::Count(reader.u64()?)),
        "flag" => match reader.u8()? {
            0 => Ok(ArgumentValue::Flag(false)),
            1 => Ok(ArgumentValue::Flag(true)),
            found => Err(DecodeError::FlagNotZeroOrOne { found }),
        },
        "choice" => Ok(ArgumentValue::Choice(reader.text()?.to_owned())),
        "supplied_value" => Ok(ArgumentValue::SuppliedValue(reader.u32()?)),
        _ => Err(DecodeError::UnknownMember {
            enumeration: Enumeration::ArgumentValueType,
        }),
    }
}

/// The member of `all` that `token` names, or a refusal naming the vocabulary.
fn member<T: Copy>(
    all: &[T],
    token: &str,
    label: fn(T) -> &'static str,
    enumeration: Enumeration,
) -> Result<T, DecodeError> {
    all.iter()
        .copied()
        .find(|member| label(*member) == token)
        .ok_or(DecodeError::UnknownMember { enumeration })
}

/// What is left of the blob.
struct Reader<'a> {
    rest: &'a [u8],
}

impl<'a> Reader<'a> {
    const fn new(bytes: &'a [u8]) -> Self {
        Self { rest: bytes }
    }

    const fn is_empty(&self) -> bool {
        self.rest.is_empty()
    }

    /// The next `count` bytes, or the refusal that says there are not that
    /// many.
    fn take(&mut self, count: usize) -> Result<&'a [u8], DecodeError> {
        if self.rest.len() < count {
            return Err(DecodeError::Truncated);
        }
        let (head, tail) = self.rest.split_at(count);
        self.rest = tail;
        Ok(head)
    }

    fn u8(&mut self) -> Result<u8, DecodeError> {
        self.take(1)?.first().copied().ok_or(DecodeError::Truncated)
    }

    fn u16(&mut self) -> Result<u16, DecodeError> {
        let bytes = <[u8; 2]>::try_from(self.take(2)?).map_err(|_| DecodeError::Truncated)?;
        Ok(u16::from_be_bytes(bytes))
    }

    fn u32(&mut self) -> Result<u32, DecodeError> {
        let bytes = <[u8; 4]>::try_from(self.take(4)?).map_err(|_| DecodeError::Truncated)?;
        Ok(u32::from_be_bytes(bytes))
    }

    fn u64(&mut self) -> Result<u64, DecodeError> {
        let bytes = <[u8; 8]>::try_from(self.take(8)?).map_err(|_| DecodeError::Truncated)?;
        Ok(u64::from_be_bytes(bytes))
    }

    /// A length-prefixed string, which is also how every closed name travels.
    fn text(&mut self) -> Result<&'a str, DecodeError> {
        let length = usize::from(self.u16()?);
        core::str::from_utf8(self.take(length)?).map_err(|_| DecodeError::NotUtf8)
    }
}

#[cfg(test)]
mod tests {
    use super::{decode, format_version, step_count, Reader};
    use crate::definition::{encode, DecodeError, DEFINITION_FORMAT_VERSION};
    use crate::matching::{MatchClause, MatchCondition};
    use crate::record::{Procedure, ProcedureId, ProcedureProvenance, ProcedureScope};
    use crate::step::ProcedureStep;
    use bip_types::action::PostconditionKind;
    use bip_types::snapshot::SemanticRole;

    fn procedure() -> Procedure {
        Procedure::draft(
            ProcedureId::new("one").unwrap(),
            ProcedureScope::for_origin("https://example.test").unwrap(),
            MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::SearchField)]),
            vec![ProcedureStep::new(
                "browser.dom.read",
                PostconditionKind::NoMutation,
            )],
            ProcedureProvenance::Authored,
        )
    }

    #[test]
    fn a_reader_hands_out_nothing_it_does_not_have() {
        let mut reader = Reader::new(&[1, 2, 3]);
        assert_eq!(reader.take(4), Err(DecodeError::Truncated));
        assert_eq!(reader.take(2), Ok([1_u8, 2].as_slice()));
        assert!(!reader.is_empty());
        assert_eq!(reader.take(1), Ok([3_u8].as_slice()));
        assert!(reader.is_empty());
        assert_eq!(reader.u8(), Err(DecodeError::Truncated));
    }

    #[test]
    fn bytes_that_are_not_a_definition_are_told_apart_from_a_version() {
        // A blob that never was a definition must not have four of its bytes
        // read as somebody else's version number.
        assert_eq!(format_version(b"nope"), Err(DecodeError::NotADefinition));
        assert_eq!(step_count(b"nope"), Err(DecodeError::NotADefinition));
        assert_eq!(decode(b""), Err(DecodeError::Truncated));
    }

    #[test]
    fn the_header_says_what_the_column_beside_it_says() {
        let Ok(bytes) = encode(&procedure()) else {
            unreachable!("the fixture is storable")
        };
        assert_eq!(format_version(&bytes), Ok(DEFINITION_FORMAT_VERSION));
        assert_eq!(step_count(&bytes), Ok(1));
    }
}
