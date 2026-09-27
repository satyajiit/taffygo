// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Turning what the browser observed into a record, or into nothing.
//!
//! Decision 0055 section 6. This module is the core's half of recording: the
//! browser builds [`StepDescriptor`]s from its live capability ledger while
//! the task is still in memory, and [`record`] decides whether those
//! descriptors are a procedure. The split is the privilege direction and not a
//! layering preference — the descriptors are the browser's because only the
//! browser has the semantic graph, and the record is the core's because the
//! core owns durable state (record 0037).
//!
//! # Why this cannot read the journal instead
//!
//! It is forced rather than chosen. The journal retains counts and closed
//! enumerations and no page content — that is what keeps page text out of
//! durable storage — so what it holds about a completed step is five integers,
//! and five integers cannot be replayed. Recording either happens while the
//! task is live or it does not happen.
//!
//! # A recording refuses as a whole
//!
//! [`SkillRecordError`] returns nothing rather than a procedure with a gap in
//! it. A procedure missing a step is not a shorter procedure: it will be
//! replayed, and the gap is where it silently does something other than what
//! the person watched happen. Three guards carry that, and they catch
//! different failures:
//!
//! - an entry the browser could not describe refuses the record and names
//!   which entry and why ([`SkillRecordError::StepNotDescribed`]);
//! - descriptors that do not account for every capability the ledger admitted
//!   refuse it ([`SkillRecordError::LedgerNotAccountedFor`]), which is what
//!   catches a builder that dropped one on an error path rather than saying so;
//! - the assembled record is put through all six structural rules **here**,
//!   at record time, and a refusal names the rule
//!   ([`SkillRecordError::Structural`]).
//!
//! The third is worth stating plainly: the rules are checked when a procedure
//! is stored and again when it is loaded, and recording is a storage. Waiting
//! until replay to notice that a recorded procedure breaks one of them would
//! mean showing a person a procedure the product was never going to run.
//!
//! # Provenance is written down and never read
//!
//! Every record this module produces carries
//! [`ProcedureProvenance::RecordedFromTask`] and enters at
//! [`crate::status::ProcedureStatus::Draft`], exactly as an authored one does.
//! "The assistant did this once" is evidence that it worked once, not that it
//! is correct — and the page it worked on is not the page it will next be run
//! against. Nothing here or downstream reads the field to decide anything;
//! `two_provenances_are_validated_identically` in [`crate::rules`] is what
//! keeps that true as the rules grow.
//!
//! # One judge, and a fixed refusal order
//!
//! The order is part of the contract, for the reason [`crate::rules::validate`]
//! gives: a person shown a refusal has to be shown the same refusal every time
//! they look. Whole-recording facts first — the ledger accounting, then the
//! origin — then each entry in order, then the six rules over the assembled
//! record.

mod descriptor;
mod reviewed;
pub use reviewed::public_address_is_valid;
pub(crate) use reviewed::replayable_recording;

use task_engine::tool::{self, ArgumentValue, Milestone, ToolDefinition, ToolLookup};

use crate::matching::MatchCondition;
use crate::record::{Procedure, ProcedureId, ProcedureProvenance, ProcedureScope, RecordError};
use crate::rules::{validate, Refusal};
use crate::step::{ProcedureStep, StepArgument, StepValue};

pub use self::descriptor::{
    ArgumentDescriptor, LedgerEntry, RecordedValue, Recording, StepDescriptor, UndescribedReason,
};

/// Why a recording produced no procedure.
///
/// Every member refuses the whole record. There is no member meaning "saved,
/// with a note" — see the module header.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SkillRecordError {
    /// A purported public address is not the reviewed same-origin first step.
    InvalidPublicAddress,
    /// The descriptors do not account for every capability the ledger
    /// admitted, in one direction or the other. Fewer means one was dropped;
    /// more means one was invented.
    LedgerNotAccountedFor {
        /// What the ledger says it admitted.
        admitted: usize,
        /// How many entries the descriptor builder produced.
        entries: usize,
    },
    /// One admitted action the browser could not turn into a step.
    StepNotDescribed {
        /// Which entry.
        step: usize,
        /// Why the browser could not describe it.
        reason: UndescribedReason,
    },
    /// The origin the task ran on is not one a record may be about.
    Scope(RecordError),
    /// An argument named a position the verb's compiled-in schema does not
    /// have.
    NoSuchParameter {
        /// Which step.
        step: usize,
        /// Which of that step's arguments.
        argument: usize,
    },
    /// A choice named a position the parameter's compiled-in set does not
    /// have.
    NoSuchChoice {
        /// Which step.
        step: usize,
        /// Which of that step's arguments.
        argument: usize,
    },
    /// The assembled record breaks one of the six structural rules, and the
    /// refusal names which.
    Structural(Refusal),
}

impl SkillRecordError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::InvalidPublicAddress => "invalid_public_address",
            Self::LedgerNotAccountedFor { .. } => "ledger_not_accounted_for",
            Self::StepNotDescribed { .. } => "step_not_described",
            Self::Scope(reason) => reason.label(),
            Self::NoSuchParameter { .. } => "no_such_parameter",
            Self::NoSuchChoice { .. } => "no_such_choice",
            Self::Structural(refusal) => refusal.reason.label(),
        }
    }
}

/// The procedure `recording` describes, or why there is none.
///
/// `id` is the core's to mint: an identifier is shown to a person and compared
/// against other records, and it is the one field of the result that does not
/// come from the browser at all. [`ProcedureId`] already refuses anything that
/// is not a well-formed one, so there is no identifier refusal here.
///
/// `milestone` is the build's, and it decides rule 3: a recording naming a
/// verb this build cannot perform is refused rather than stored against a
/// later build that might be able to.
pub fn record(
    id: ProcedureId,
    recording: &Recording,
    milestone: Milestone,
) -> Result<Procedure, SkillRecordError> {
    if recording.entries().len() != recording.admitted() {
        return Err(SkillRecordError::LedgerNotAccountedFor {
            admitted: recording.admitted(),
            entries: recording.entries().len(),
        });
    }
    let scope = ProcedureScope::from_normalized(recording.origin().clone())
        .map_err(SkillRecordError::Scope)?;
    let mut steps: Vec<ProcedureStep> = Vec::with_capacity(recording.entries().len());
    for (index, entry) in recording.entries().iter().enumerate() {
        match entry {
            LedgerEntry::Undescribed(reason) => {
                return Err(SkillRecordError::StepNotDescribed {
                    step: index,
                    reason: *reason,
                })
            }
            LedgerEntry::Described(descriptor) => {
                for argument in &descriptor.arguments {
                    if let RecordedValue::PublicAddress(address) = &argument.value {
                        if index != 0
                            || descriptor.verb != "browser.navigate"
                            || argument.parameter != 0
                            || !public_address_is_valid(address, recording.origin())
                        {
                            return Err(SkillRecordError::InvalidPublicAddress);
                        }
                    }
                }
                steps.push(step_from(descriptor, index, milestone)?);
            }
        }
    }
    let procedure = Procedure::draft(
        id,
        scope,
        MatchCondition::new(recording.facts().to_vec()),
        steps,
        ProcedureProvenance::RecordedFromTask,
    );
    validate(&procedure, milestone).map_err(SkillRecordError::Structural)?;
    Ok(procedure)
}

/// One descriptor as a step, with the table's own name for its verb.
///
/// A verb this build cannot perform is assembled **bare** — the verb, its
/// postcondition and what it recorded filling, and no arguments. That is not a
/// degradation: there is no compiled-in schema this build would resolve the
/// argument positions against, and `validate` refuses such a step by rule 3
/// before it looks at anything else, so the fault a person is told about is the
/// verb rather than an argument index nobody can check. The "always" in that
/// sentence is the load-bearing word, and the unit test named for it walks the
/// whole registry rather than sampling it — a row that came to be assembled
/// bare *and* accepted would be a record stored without the arguments its
/// author watched happen.
fn step_from(
    descriptor: &StepDescriptor,
    index: usize,
    milestone: Milestone,
) -> Result<ProcedureStep, SkillRecordError> {
    let lookup = tool::resolve(&descriptor.verb, milestone);
    // The table's name for the verb, never the caller's spelling of it — the
    // same rule `replay::action` follows when it proposes one. Where no row
    // claims the name there is nothing to canonicalise against, and the record
    // that would carry the caller's spelling is refused before it exists.
    let verb = lookup
        .entry()
        .and_then(|entry| entry.canonical_name(&descriptor.verb))
        .unwrap_or_else(|| descriptor.verb.clone());
    let mut step = ProcedureStep::new(verb, descriptor.postcondition);
    if let Some(purpose) = descriptor.fills {
        step = step.filling(purpose);
    }
    let ToolLookup::Available(entry) = lookup else {
        return Ok(step);
    };
    let mut arguments: Vec<StepArgument> = Vec::with_capacity(descriptor.arguments.len());
    for (position, argument) in descriptor.arguments.iter().enumerate() {
        arguments.push(argument_from(
            entry.definition(),
            argument,
            index,
            position,
        )?);
    }
    Ok(step.taking(arguments))
}

/// One argument descriptor as an argument, with the build's own name for its
/// parameter and the build's own text for its value.
fn argument_from(
    definition: ToolDefinition,
    argument: &ArgumentDescriptor,
    step: usize,
    position: usize,
) -> Result<StepArgument, SkillRecordError> {
    let Some(parameter) = definition.parameters.get(argument.parameter) else {
        return Err(SkillRecordError::NoSuchParameter {
            step,
            argument: position,
        });
    };
    let value = match &argument.value {
        RecordedValue::PublicAddress(address) => {
            StepValue::Literal(ArgumentValue::Address(address.clone()))
        }
        RecordedValue::SemanticTarget { role, phrase } => StepValue::SemanticTarget {
            role: *role,
            phrase: *phrase,
        },
        RecordedValue::FromEarlierStep { step: earlier } => {
            StepValue::FromEarlierStep { step: *earlier }
        }
        RecordedValue::FromPerson { purpose } => StepValue::FromPerson { purpose: *purpose },
        RecordedValue::Choice { index } => {
            let Some(name) = parameter.value_type.choices().get(*index) else {
                return Err(SkillRecordError::NoSuchChoice {
                    step,
                    argument: position,
                });
            };
            StepValue::Literal(ArgumentValue::Choice((*name).to_owned()))
        }
        RecordedValue::Count(count) => StepValue::Literal(ArgumentValue::Count(*count)),
        RecordedValue::Flag(flag) => StepValue::Literal(ArgumentValue::Flag(*flag)),
    };
    Ok(StepArgument::new(parameter.name, value))
}

#[cfg(test)]
mod tests {
    use super::{
        record, ArgumentDescriptor, LedgerEntry, RecordedValue, Recording, SkillRecordError,
        StepDescriptor,
    };
    use crate::field::FieldPurpose;
    use crate::matching::MatchClause;
    use crate::record::{ProcedureId, ProcedureProvenance};
    use crate::rules::{RefusalReason, StructuralRule};
    use crate::status::ProcedureStatus;
    use crate::step::StepValue;
    use bip_types::action::PostconditionKind;
    use bip_types::snapshot::SemanticRole;
    use policy_engine::origin::{normalize_serialization, NormalizedOrigin};
    use task_engine::tool::{ArgumentValue, Milestone, NameMatch, ToolAvailability, REGISTRY};

    fn origin() -> NormalizedOrigin {
        let Ok(origin) = normalize_serialization("https://example.test") else {
            unreachable!("the fixture origin is an origin")
        };
        origin
    }

    fn id() -> ProcedureId {
        let Ok(id) = ProcedureId::new("recorded.once") else {
            unreachable!("the fixture identifier is well formed")
        };
        id
    }

    fn facts() -> Vec<MatchClause> {
        vec![MatchClause::RolePresent(SemanticRole::SearchField)]
    }

    fn query() -> StepDescriptor {
        StepDescriptor::new("browser.dom.query", PostconditionKind::NoMutation)
    }

    fn recording(entries: Vec<LedgerEntry>) -> Recording {
        let admitted = entries.len();
        Recording::new(origin(), facts(), entries, admitted)
    }

    #[test]
    fn a_recorded_procedure_enters_at_draft_and_says_where_it_came_from() {
        let Ok(procedure) = record(
            id(),
            &recording(vec![LedgerEntry::Described(query())]),
            Milestone::M3,
        ) else {
            unreachable!("this recording is a procedure")
        };
        assert_eq!(procedure.provenance, ProcedureProvenance::RecordedFromTask);
        assert_eq!(procedure.status, ProcedureStatus::Draft);
        assert!(!procedure.is_runnable());
        assert_eq!(procedure.verbs(), vec!["browser.dom.query"]);
    }

    #[test]
    fn a_choice_reaches_the_record_as_the_build_wrote_it() {
        // The index names a position in the parameter's own compiled-in set,
        // so what lands in the record is a string from the table. There is no
        // door here a caller's spelling could come through.
        let query =
            StepDescriptor::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
                ArgumentDescriptor::new(1, RecordedValue::Choice { index: 1 }),
            ]);
        let Ok(procedure) = record(
            id(),
            &recording(vec![LedgerEntry::Described(query)]),
            Milestone::M3,
        ) else {
            unreachable!("this recording is a procedure")
        };
        let Some(step) = procedure.steps.first() else {
            unreachable!("the record has one step")
        };
        let Some(argument) = step.arguments.first() else {
            unreachable!("the step has one argument")
        };
        assert_eq!(argument.name, "role");
        assert_eq!(
            argument.value,
            StepValue::Literal(ArgumentValue::Choice("button".to_owned()))
        );
    }

    #[test]
    fn an_index_the_build_does_not_have_is_refused_rather_than_clamped() {
        let bad_parameter =
            query().taking(vec![ArgumentDescriptor::new(9, RecordedValue::Count(1))]);
        assert_eq!(
            record(
                id(),
                &recording(vec![LedgerEntry::Described(bad_parameter)]),
                Milestone::M3
            ),
            Err(SkillRecordError::NoSuchParameter {
                step: 0,
                argument: 0
            })
        );
        let bad_choice =
            StepDescriptor::new("browser.dom.scroll", PostconditionKind::NoMutation).taking(vec![
                ArgumentDescriptor::new(0, RecordedValue::Choice { index: 9 }),
            ]);
        assert_eq!(
            record(
                id(),
                &recording(vec![LedgerEntry::Described(bad_choice)]),
                Milestone::M3
            ),
            Err(SkillRecordError::NoSuchChoice {
                step: 0,
                argument: 0
            })
        );
    }

    #[test]
    fn a_verb_this_build_cannot_perform_is_always_refused() {
        // The bare-assembly path drops the arguments, so it has to be true
        // that nothing can come out of it accepted. Every registry row this
        // milestone does not have, plus a name no row claims, walked rather
        // than sampled — a row that started being assembled bare *and*
        // accepted would be a record stored without the arguments its author
        // watched happen.
        let mut unavailable = 0_usize;
        for entry in REGISTRY {
            if entry.is_available_at(Milestone::M3) {
                continue;
            }
            unavailable = unavailable.saturating_add(1);
            // A namespace row's own prefix is not a callable name, so the walk
            // asks for one of the members the row claims.
            let name = match entry.matching {
                NameMatch::Exact => entry.name.to_owned(),
                NameMatch::Namespace { members } => {
                    let Some(member) = members.first() else {
                        unreachable!("{} claims no member", entry.name)
                    };
                    (*member).to_owned()
                }
            };
            let descriptor = StepDescriptor::new(name.clone(), PostconditionKind::NoMutation)
                .taking(vec![ArgumentDescriptor::new(0, RecordedValue::Count(1))]);
            let refused = record(
                id(),
                &recording(vec![LedgerEntry::Described(descriptor)]),
                Milestone::M3,
            );
            let expected = match entry.availability {
                ToolAvailability::ExcludedByRequirement => RefusalReason::ExcludedVerb,
                ToolAvailability::From(_) => RefusalReason::VerbNotYetBuilt,
            };
            match refused {
                Err(SkillRecordError::Structural(refusal)) => {
                    assert_eq!(refusal.reason, expected, "{name}");
                    assert_eq!(refusal.rule, StructuralRule::NoNewVerbs, "{name}");
                }
                Ok(_) | Err(_) => unreachable!("{name} must be refused by rule 3"),
            }
        }
        assert!(unavailable > 0, "the walk found nothing to check");
        let unknown = StepDescriptor::new("nope.nope", PostconditionKind::NoMutation);
        match record(
            id(),
            &recording(vec![LedgerEntry::Described(unknown)]),
            Milestone::M3,
        ) {
            Err(SkillRecordError::Structural(refusal)) => {
                assert_eq!(refusal.reason, RefusalReason::UnregisteredVerb);
            }
            Ok(_) | Err(_) => unreachable!("an unregistered verb must be refused by rule 3"),
        }
    }

    #[test]
    fn an_opaque_origin_records_nothing() {
        // A procedure scoped to a session-local origin is dead the moment the
        // session that produced it ends, and can never be shown to a person
        // either.
        let opaque = Recording::new(
            NormalizedOrigin::Opaque {
                opaque_id: "session-local".to_owned(),
            },
            facts(),
            vec![LedgerEntry::Described(query())],
            1,
        );
        assert!(matches!(
            record(id(), &opaque, Milestone::M3),
            Err(SkillRecordError::Scope(_))
        ));
    }

    #[test]
    fn a_fill_carries_what_it_was_for_and_never_what_it_was() {
        // The bytes the person typed were minted in the browser's vault, spent
        // once and scrubbed. What survives is the classification, which is what
        // the step shows the person when it asks them again.
        let fill = StepDescriptor::new("browser.form.fill", PostconditionKind::NodeValueChanged)
            .taking(vec![
                ArgumentDescriptor::new(0, RecordedValue::FromEarlierStep { step: 0 }),
                ArgumentDescriptor::new(
                    1,
                    RecordedValue::FromPerson {
                        purpose: FieldPurpose::EmailAddress,
                    },
                ),
            ])
            .filling(FieldPurpose::EmailAddress);
        let Ok(procedure) = record(
            id(),
            &recording(vec![
                LedgerEntry::Described(query()),
                LedgerEntry::Described(fill),
            ]),
            Milestone::M5,
        ) else {
            unreachable!("this recording is a procedure")
        };
        let Some(step) = procedure.steps.get(1) else {
            unreachable!("the record has two steps")
        };
        assert_eq!(
            step.arguments.get(1).map(|argument| &argument.value),
            Some(&StepValue::FromPerson {
                purpose: FieldPurpose::EmailAddress
            })
        );
        assert_eq!(step.fills, Some(FieldPurpose::EmailAddress));
    }

    #[test]
    fn every_error_shape_has_a_compiled_in_label() {
        let errors = [
            SkillRecordError::LedgerNotAccountedFor {
                admitted: 2,
                entries: 1,
            },
            SkillRecordError::StepNotDescribed {
                step: 0,
                reason: super::UndescribedReason::OutcomeUnknown,
            },
            SkillRecordError::NoSuchParameter {
                step: 0,
                argument: 0,
            },
            SkillRecordError::NoSuchChoice {
                step: 0,
                argument: 0,
            },
        ];
        let mut seen: Vec<&str> = Vec::new();
        for error in errors {
            assert!(!seen.contains(&error.label()), "{}", error.label());
            seen.push(error.label());
        }
    }
}
