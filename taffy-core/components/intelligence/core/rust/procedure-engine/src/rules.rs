// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The six structural rules, and the refusal that names the one that broke.
//!
//! # Checked twice, and the second time is the one that matters
//!
//! [`validate`] runs when a procedure is stored and again when it is loaded. A
//! reader's first instinct is that the second run is redundant, and it is not:
//! the registry is compiled in and the milestone moves, so a procedure stored
//! against a build that had a verb can be loaded by a build that does not — and
//! a record written before a bound was lowered is a record on disk that no
//! longer satisfies it. Validating only at storage would make every such
//! procedure a live one that no code had ever agreed to.
//!
//! # A refusal names a rule, not a line
//!
//! [`Refusal`] carries the [`StructuralRule`] that broke, a
//! [`RefusalReason`] saying how, and the step it was about when it was about
//! one. The rule is derived from the reason rather than passed beside it, so
//! the two cannot disagree about what a refusal was, and
//! `every_rule_has_a_reason_that_names_it` keeps a rule from being documented
//! with nothing able to refuse under it.
//!
//! # What a passing `validate` does not say
//!
//! It says the record is *structurally* a procedure. It does not say the
//! procedure will work, and one gap is worth naming because it looks like
//! something this function would have caught: a step may reference an earlier
//! step that produces no handle — `browser.navigate` followed by a read whose
//! `node` comes from it. The reference is strictly backwards, so rule 2 is
//! satisfied, and the type is right, so the schema is satisfied; what is wrong
//! is that the earlier step yields nothing to bind. Deciding that needs a fact
//! the tool table does not carry — which names produce a handle — and inventing
//! one here would be inventing a second tool table. The binding fails at
//! replay, where the handle table is the thing that answers, and it fails
//! closed there.
//!
//! # Where rule 6 is, and why it is not here
//!
//! Rule 6 is a replay-time downgrade and not a load-time refusal — see
//! [`crate::field`]. What this module owns of it is the *shape* that makes the
//! downgrade possible: a fill step must record what it fills and a step that
//! fills nothing must not claim to. A fill with no recorded purpose would reach
//! [`crate::field::disposition`] with nothing to compare, and the honest answer
//! there would have to be invented.

use task_engine::tool::{self, Milestone, ToolLookup};

use crate::matching::MAX_MATCH_CLAUSES;
use crate::record::Procedure;
use crate::step::{ProcedureStep, StepValue, MAX_PROCEDURE_STEPS};

/// One of the six structural rules of decision 0055 section 4.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StructuralRule {
    /// 1. A step is a verb and its arguments, never a computation.
    NoExpressions,
    /// 2. Steps run in order and never jump.
    NoBackwardEdges,
    /// 3. A verb resolves in the compiled-in table, arguments included.
    NoNewVerbs,
    /// 4. Matching is a catalogue, not a pattern language.
    CataloguedPhrases,
    /// 5. [`MAX_PROCEDURE_STEPS`].
    StepBound,
    /// 6. An unclassified field is handed to the person.
    UnclassifiedFieldHandedOver,
}

impl StructuralRule {
    /// Every rule, in the order decision 0055 section 4 states them.
    pub const ALL: &'static [Self] = &[
        Self::NoExpressions,
        Self::NoBackwardEdges,
        Self::NoNewVerbs,
        Self::CataloguedPhrases,
        Self::StepBound,
        Self::UnclassifiedFieldHandedOver,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::NoExpressions => "no_expressions",
            Self::NoBackwardEdges => "no_backward_edges",
            Self::NoNewVerbs => "no_new_verbs",
            Self::CataloguedPhrases => "catalogued_phrases",
            Self::StepBound => "step_bound",
            Self::UnclassifiedFieldHandedOver => "unclassified_field_handed_over",
        }
    }
}

/// How a rule was broken.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RefusalReason {
    /// More steps than [`MAX_PROCEDURE_STEPS`].
    TooManySteps,
    /// No steps at all. A procedure that does nothing is not a shorter
    /// procedure: it narrows nothing (see [`crate::narrowing`]) and it matches
    /// pages in order to do nothing to them.
    NoSteps,
    /// A verb no registry row matches.
    UnregisteredVerb,
    /// A verb the product excludes by requirement. It is registered so the
    /// refusal is enumerable, and it will never run.
    ExcludedVerb,
    /// A verb a later milestone owns. The build cannot perform it, so a
    /// procedure naming it is one that cannot run on this build.
    VerbNotYetBuilt,
    /// The arguments do not match the verb's compiled-in schema, as
    /// `task_engine::tool::validate` decides it.
    Arguments(tool::ArgumentRefusalReason),
    /// A literal whose text was written expecting substitution.
    ExpressionInLiteral,
    /// A request to the person with no name on it.
    UnnamedRequestToPerson,
    /// A reference to the step itself, or to a later one.
    StepReferenceNotEarlier,
    /// A condition that claims nothing, and so claims every page.
    EmptyMatchCondition,
    /// More clauses than [`MAX_MATCH_CLAUSES`].
    TooManyClauses,
    /// A fill step that does not record what it fills.
    FillRecordsNoPurpose,
    /// A step that fills nothing, claiming to fill something.
    PurposeOnAStepThatFillsNothing,
}

impl RefusalReason {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::TooManySteps,
        Self::NoSteps,
        Self::UnregisteredVerb,
        Self::ExcludedVerb,
        Self::VerbNotYetBuilt,
        Self::Arguments(tool::ArgumentRefusalReason::UnknownArgument),
        Self::ExpressionInLiteral,
        Self::UnnamedRequestToPerson,
        Self::StepReferenceNotEarlier,
        Self::EmptyMatchCondition,
        Self::TooManyClauses,
        Self::FillRecordsNoPurpose,
        Self::PurposeOnAStepThatFillsNothing,
    ];

    /// The one rule this reason belongs to.
    pub const fn rule(self) -> StructuralRule {
        match self {
            Self::TooManySteps | Self::NoSteps => StructuralRule::StepBound,
            // A step whose arguments the table does not declare is naming a
            // signature the build does not have, which is introducing a verb by
            // another route.
            Self::UnregisteredVerb
            | Self::ExcludedVerb
            | Self::VerbNotYetBuilt
            | Self::Arguments(_) => StructuralRule::NoNewVerbs,
            Self::ExpressionInLiteral | Self::UnnamedRequestToPerson => {
                StructuralRule::NoExpressions
            }
            Self::StepReferenceNotEarlier => StructuralRule::NoBackwardEdges,
            Self::EmptyMatchCondition | Self::TooManyClauses => StructuralRule::CataloguedPhrases,
            Self::FillRecordsNoPurpose | Self::PurposeOnAStepThatFillsNothing => {
                StructuralRule::UnclassifiedFieldHandedOver
            }
        }
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::TooManySteps => "too_many_steps",
            Self::NoSteps => "no_steps",
            Self::UnregisteredVerb => "unregistered_verb",
            Self::ExcludedVerb => "excluded_verb",
            Self::VerbNotYetBuilt => "verb_not_yet_built",
            Self::Arguments(reason) => reason.label(),
            Self::ExpressionInLiteral => "expression_in_literal",
            Self::UnnamedRequestToPerson => "unnamed_request_to_person",
            Self::StepReferenceNotEarlier => "step_reference_not_earlier",
            Self::EmptyMatchCondition => "empty_match_condition",
            Self::TooManyClauses => "too_many_clauses",
            Self::FillRecordsNoPurpose => "fill_records_no_purpose",
            Self::PurposeOnAStepThatFillsNothing => "purpose_on_a_step_that_fills_nothing",
        }
    }
}

/// Why a procedure was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Refusal {
    /// Which rule broke.
    pub rule: StructuralRule,
    /// How.
    pub reason: RefusalReason,
    /// The step it was about, when it was about one.
    pub step: Option<usize>,
}

impl Refusal {
    const fn about(reason: RefusalReason, step: usize) -> Self {
        Self {
            rule: reason.rule(),
            reason,
            step: Some(step),
        }
    }

    const fn whole_record(reason: RefusalReason) -> Self {
        Self {
            rule: reason.rule(),
            reason,
            step: None,
        }
    }
}

/// Whether `procedure` satisfies every structural rule on a build at
/// `milestone`.
///
/// The order is fixed and part of the contract: the record's own bounds, then
/// the condition, then each step in order. A validator whose order varied would
/// let one broken procedure be refused two different ways on two runs, and a
/// refusal a person is shown has to be the same refusal every time they look.
///
/// Provenance is not read. `two_provenances_are_validated_identically` is what
/// keeps that true.
pub fn validate(procedure: &Procedure, milestone: Milestone) -> Result<(), Refusal> {
    if procedure.steps.is_empty() {
        return Err(Refusal::whole_record(RefusalReason::NoSteps));
    }
    if procedure.steps.len() > MAX_PROCEDURE_STEPS {
        return Err(Refusal::whole_record(RefusalReason::TooManySteps));
    }
    if procedure.condition.is_empty() {
        return Err(Refusal::whole_record(RefusalReason::EmptyMatchCondition));
    }
    if procedure.condition.len() > MAX_MATCH_CLAUSES {
        return Err(Refusal::whole_record(RefusalReason::TooManyClauses));
    }
    for (index, step) in procedure.steps.iter().enumerate() {
        check_step(step, index, milestone)?;
    }
    Ok(())
}

/// One step, against the table and against its own position.
fn check_step(step: &ProcedureStep, index: usize, milestone: Milestone) -> Result<(), Refusal> {
    let entry = match tool::resolve(&step.verb, milestone) {
        ToolLookup::Available(entry) => entry,
        ToolLookup::Unknown => return Err(Refusal::about(RefusalReason::UnregisteredVerb, index)),
        ToolLookup::Excluded(_) => return Err(Refusal::about(RefusalReason::ExcludedVerb, index)),
        ToolLookup::Unavailable { .. } => {
            return Err(Refusal::about(RefusalReason::VerbNotYetBuilt, index))
        }
    };
    check_fill_shape(step, index)?;
    check_values(step, index)?;
    // The one argument validator this product has, applied to the witnesses
    // each value shape stands for. Writing a second one here is how two answers
    // to "are these the right arguments" come to exist, and only one of them
    // would be the one dispatch uses.
    let supplied: Vec<tool::SuppliedArgument> = step
        .arguments
        .iter()
        .map(|argument| {
            // The witness takes the shape the row declares for that parameter.
            // A name the row does not declare yields `None`, and `validate`
            // below is what refuses it — deciding here would be the second
            // argument validator this block exists to avoid.
            let declared = entry
                .definition()
                .parameter(&argument.name)
                .map(|parameter| parameter.value_type);
            tool::SuppliedArgument::new(
                argument.name.clone(),
                argument.value.schema_witness(declared),
            )
        })
        .collect();
    tool::validate(entry.definition(), &supplied)
        .map_err(|refusal| Refusal::about(RefusalReason::Arguments(refusal.reason), index))
}

/// A fill records what it fills, and nothing else claims to.
fn check_fill_shape(step: &ProcedureStep, index: usize) -> Result<(), Refusal> {
    match (step.is_a_fill(), step.fills.is_some()) {
        (true, false) => Err(Refusal::about(RefusalReason::FillRecordsNoPurpose, index)),
        (false, true) => Err(Refusal::about(
            RefusalReason::PurposeOnAStepThatFillsNothing,
            index,
        )),
        (true, true) | (false, false) => Ok(()),
    }
}

/// Every value of one step: no expression, and no edge that is not backwards.
fn check_values(step: &ProcedureStep, index: usize) -> Result<(), Refusal> {
    for argument in &step.arguments {
        if argument.value.carries_a_substitution_sigil() {
            return Err(Refusal::about(RefusalReason::ExpressionInLiteral, index));
        }
        match argument.value {
            // Strictly earlier. `earlier == index` is a step naming itself,
            // which is a loop of one; `earlier > index` is a jump, because
            // satisfying it would mean running a later step first. Both are the
            // backward edge rule 2 refuses, and neither is expressible any
            // other way in this record.
            StepValue::FromEarlierStep { step: earlier } if earlier >= index => {
                return Err(Refusal::about(
                    RefusalReason::StepReferenceNotEarlier,
                    index,
                ))
            }
            StepValue::FromPerson { purpose } if !purpose.is_named() => {
                return Err(Refusal::about(RefusalReason::UnnamedRequestToPerson, index))
            }
            StepValue::SemanticTarget { .. }
            | StepValue::Literal(_)
            | StepValue::FromEarlierStep { .. }
            | StepValue::FromPerson { .. } => {}
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{validate, Refusal, RefusalReason, StructuralRule};
    use crate::field::FieldPurpose;
    use crate::matching::{MatchClause, MatchCondition};
    use crate::record::{Procedure, ProcedureId, ProcedureProvenance, ProcedureScope};
    use crate::step::{ProcedureStep, StepArgument, StepValue};
    use bip_types::action::PostconditionKind;
    use bip_types::snapshot::SemanticRole;
    use task_engine::tool::{ArgumentRefusalReason, ArgumentValue, Milestone};

    fn condition() -> MatchCondition {
        MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::SearchField)])
    }

    fn procedure(steps: Vec<ProcedureStep>) -> Procedure {
        Procedure::draft(
            ProcedureId::new("one").unwrap(),
            ProcedureScope::for_origin("https://example.test").unwrap(),
            condition(),
            steps,
            ProcedureProvenance::Authored,
        )
    }

    #[test]
    fn every_rule_has_a_reason_that_names_it() {
        // A rule with no reason is a rule nothing can refuse under, which is
        // how one of the six comes to be documented and unenforced.
        let mut named: Vec<StructuralRule> = Vec::new();
        for reason in RefusalReason::ALL {
            let rule = reason.rule();
            if !named.contains(&rule) {
                named.push(rule);
            }
        }
        for rule in StructuralRule::ALL {
            assert!(named.contains(rule), "no reason names {}", rule.label());
        }
        assert_eq!(named.len(), StructuralRule::ALL.len());
    }

    #[test]
    fn two_provenances_are_validated_identically() {
        // Section 2 of decision 0055: provenance is a field and not an input to
        // any authority decision. A recorded procedure *feels* more trustworthy
        // than an authored one because the assistant is known to have run those
        // exact steps — and that feeling is about the past page. The two are
        // put through every refusal here, rather than through one, so a rule
        // that ever started reading provenance fails on the pair.
        let accepted = vec![ProcedureStep::new(
            "browser.dom.query",
            PostconditionKind::NoMutation,
        )];
        let cases: Vec<Vec<ProcedureStep>> = vec![
            // One that passes, first, so the pair cannot agree merely because
            // everything is refused.
            accepted.clone(),
            Vec::new(),
            vec![ProcedureStep::new(
                "nope.nope",
                PostconditionKind::NoMutation,
            )],
            vec![ProcedureStep::new(
                "device.clipboard.read",
                PostconditionKind::NoMutation,
            )],
            vec![ProcedureStep::new(
                "browser.form.fill",
                PostconditionKind::NodeValueChanged,
            )],
        ];
        assert_eq!(validate(&procedure(accepted), Milestone::M3), Ok(()));
        for steps in cases {
            let mut authored = procedure(steps.clone());
            authored.provenance = ProcedureProvenance::Authored;
            let mut recorded = procedure(steps);
            recorded.provenance = ProcedureProvenance::RecordedFromTask;
            assert_eq!(
                validate(&authored, Milestone::M3),
                validate(&recorded, Milestone::M3)
            );
        }
    }

    #[test]
    fn no_fill_can_carry_the_bytes_of_what_the_person_typed() {
        // Credentials, one-time codes, payment values and passkeys never enter
        // the AI data plane, and a stored procedure is the most durable place
        // one could have ended up. The guarantee is not a rule of this module:
        // a fill's value parameter is declared `SuppliedValue`, so a literal
        // carrying text is a type mismatch to the one argument validator this
        // product has, whatever the purpose says. Walked over the whole
        // vocabulary rather than over the members somebody worried about,
        // because a purpose added later is covered by the same schema and this
        // is what says so.
        for purpose in FieldPurpose::ALL {
            let typed =
                ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
                    .taking(vec![
                        StepArgument::new("field", StepValue::Literal(ArgumentValue::Handle(1))),
                        StepArgument::literal(
                            "value_from",
                            ArgumentValue::Text("123456".to_owned()),
                        ),
                    ])
                    .filling(*purpose);
            assert_eq!(
                validate(&procedure(vec![typed]), Milestone::M5),
                Err(Refusal::about(
                    RefusalReason::Arguments(ArgumentRefusalReason::TypeMismatch),
                    0
                )),
                "{}",
                purpose.label()
            );
        }
    }

    #[test]
    fn the_reasons_have_distinct_labels() {
        let mut seen: Vec<&str> = Vec::new();
        for reason in RefusalReason::ALL {
            assert!(!seen.contains(&reason.label()), "{}", reason.label());
            seen.push(reason.label());
        }
        let mut rules: Vec<&str> = Vec::new();
        for rule in StructuralRule::ALL {
            assert!(!rules.contains(&rule.label()), "{}", rule.label());
            rules.push(rule.label());
        }
    }
}
