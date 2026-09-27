// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A step: a verb the build already has, its arguments, and its postcondition.
//!
//! # Rule 1 is mostly carried by the type, and partly by a scan
//!
//! [`StepValue`] has three variants and none of them computes anything. There
//! is no arithmetic, no concatenation, no conditional and no reference to
//! anything outside the procedure, because a stored expression language is a
//! stored program and a stored program is a thing whose behaviour cannot be
//! read off its text.
//!
//! What the type cannot carry is the *other* half of the same hazard: a literal
//! whose text was written expecting substitution. `"{{step0.value}}"` is a
//! perfectly good `String` and a perfectly bad procedure — it will be typed
//! into a page verbatim, doing something different from what its author read.
//! [`StepValue::carries_a_substitution_sigil`] is a fixed substring scan for
//! exactly that shape. It is not a pattern language and it evaluates nothing:
//! it looks for a handful of compiled-in two-character openers and answers yes
//! or no.
//!
//! # Rule 2 has nothing to express
//!
//! There is no jump, no branch and no repeat here, so control flow is the step
//! order and only the step order. The one edge a procedure can carry is data —
//! [`StepValue::FromEarlierStep`] — and it must name a *strictly* earlier step.
//! A reference to the same step is a self-loop; a reference to a later one is a
//! jump wearing a different word, since satisfying it would mean running that
//! step first. Both are refused by [`crate::rules::validate`], which is where
//! a step's index is known.
//!
//! # The postcondition is BIP's, unextended
//!
//! [`PostconditionKind`] comes from the browsing protocol exactly as it is. A
//! replayed step's success is decided by the same verifier that decides an
//! ordinary action's, so replay needs no new protocol member. Wanting one here
//! means the step is describing something the product cannot verify, which is a
//! defect in the step rather than a gap in the protocol.

use bip_types::action::PostconditionKind;
use bip_types::snapshot::SemanticRole;
use task_engine::tool::{self, ArgumentValue, Milestone, ParameterType, ToolEntry};

use crate::field::FieldPurpose;

/// How many steps one procedure may hold.
///
/// Refused at storage rather than truncated at replay. Truncating produces a
/// *different* procedure that still claims the original's identity, which is
/// the worst of the available failures: the record a person reads and the
/// sequence that runs would be two things sharing one name.
pub const MAX_PROCEDURE_STEPS: usize = 32;

/// The verbs whose whole purpose is to put a value into a field.
///
/// A compiled-in list beside the registry rather than a column on
/// `ToolEntry`, because "does this write into a form field" is this crate's
/// question about a step and not the tool table's question about a name. The
/// list is checked against the registry by
/// `every_fill_verb_is_registered`, so it cannot name something the table
/// dropped.
///
/// `browser.form.submit` is deliberately absent: submitting is consequential
/// and it fills nothing, so rule 6 has no field to be about.
pub const FILL_VERBS: &[&str] = &["browser.form.fill"];

/// Openers that mean somebody expected this text to be substituted.
///
/// Compiled in, matched as substrings, and evaluated never. The point is not to
/// parse a template language — it is to notice that a literal was written in
/// one, so the procedure is refused instead of quietly typing its own source
/// code into a page.
const SUBSTITUTION_SIGILS: &[&str] = &["{{", "${", "$(", "%{", "<%", "#{", "@{"];

/// One argument's value.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StepValue {
    /// A unique visible enabled node reselected from a fresh page.
    SemanticTarget {
        role: SemanticRole,
        phrase: crate::matching::PhraseId,
    },
    /// A value the record carries.
    Literal(ArgumentValue),
    /// A handle an earlier step of this same procedure produced.
    ///
    /// The index is into the procedure's own step list and must be strictly
    /// less than the index of the step that names it.
    FromEarlierStep {
        /// Which earlier step.
        step: usize,
    },
    /// A named request to the person.
    ///
    /// The name is the purpose, which is what a person is shown when they are
    /// asked. [`FieldPurpose::Unknown`] is not a name, and a request that
    /// carries it is refused: asking somebody for a value without saying what
    /// it is for is not a request, it is a prompt.
    FromPerson {
        /// What the person is being asked for.
        purpose: FieldPurpose,
    },
}

impl StepValue {
    /// A short, compiled-in name for the shape, safe to record in an audit
    /// event.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::SemanticTarget { .. } => "semantic_target",
            Self::Literal(_) => "literal",
            Self::FromEarlierStep { .. } => "from_earlier_step",
            Self::FromPerson { .. } => "from_person",
        }
    }

    /// Whether a literal's text was written expecting substitution.
    ///
    /// Only a literal can be: the other two variants carry no text at all.
    pub fn carries_a_substitution_sigil(&self) -> bool {
        let Self::Literal(value) = self else {
            return false;
        };
        let Some(text) = value.text() else {
            return false;
        };
        SUBSTITUTION_SIGILS.iter().any(|sigil| text.contains(sigil))
    }

    /// The value this stands for when the step runs, for schema checking only.
    ///
    /// Each variant produces exactly one type, so this is a statement about the
    /// variant rather than a guess about a value:
    ///
    /// - a literal is itself;
    /// - an earlier step yields a handle, so the witness is handle-shaped. The
    ///   number is the step index and is emphatically **not** the handle that
    ///   step will issue — the real binding happens at replay against the handle
    ///   table, and fails closed there. Only the shape is under test here;
    /// - a person supplies whatever shape the parameter declares, which is why
    ///   this takes the declared type. Both shapes are things a person can be
    ///   asked for and they are not interchangeable: a search query is text
    ///   the person types and the model then reads, while a field value is
    ///   minted in the browser's vault and the model only ever names its
    ///   position (decisions 0063 and 0088). Where the schema declares a
    ///   supplied value the witness carries no bytes at all; everywhere else it
    ///   carries the purpose's own compiled-in name, so the text is neither
    ///   empty nor unbounded.
    ///
    ///   The number in a supplied-value witness is **not** the position the
    ///   replayed step will name. The person is asked again at replay and the
    ///   binding is made there.
    ///
    /// This exists so that `task_engine::tool::validate` — the one argument
    /// validator this product has — can check a step, instead of this crate
    /// growing a second copy of its rules that would drift from it. It is never
    /// a value anything sends anywhere.
    pub fn schema_witness(&self, declared: Option<ParameterType>) -> ArgumentValue {
        match self {
            Self::SemanticTarget { .. } => ArgumentValue::Handle(0),
            Self::Literal(value) => value.clone(),
            Self::FromEarlierStep { step } => {
                ArgumentValue::Handle(u32::try_from(*step).unwrap_or(u32::MAX))
            }
            Self::FromPerson { purpose } => match declared {
                Some(ParameterType::SuppliedValue) => ArgumentValue::SuppliedValue(0),
                Some(
                    ParameterType::Handle
                    | ParameterType::Text
                    | ParameterType::Address
                    | ParameterType::Count
                    | ParameterType::Flag
                    | ParameterType::Choice(_),
                )
                | None => ArgumentValue::Text(purpose.label().to_owned()),
            },
        }
    }
}

/// One argument of one step.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StepArgument {
    /// The parameter name, which the verb's compiled-in schema must declare.
    pub name: String,
    /// What is supplied for it.
    pub value: StepValue,
}

impl StepArgument {
    /// One argument.
    pub fn new(name: impl Into<String>, value: StepValue) -> Self {
        Self {
            name: name.into(),
            value,
        }
    }

    /// One argument carrying a literal.
    pub fn literal(name: impl Into<String>, value: ArgumentValue) -> Self {
        Self::new(name, StepValue::Literal(value))
    }
}

/// One step of a procedure.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProcedureStep {
    /// The tool name. It must resolve in `task_engine::tool::REGISTRY`.
    pub verb: String,
    /// Its arguments.
    pub arguments: Vec<StepArgument>,
    /// The effect the step declares it expects, from BIP, unextended.
    pub postcondition: PostconditionKind,
    /// What the field this step fills was classified as, when it fills one.
    ///
    /// `Some` exactly for a verb in [`FILL_VERBS`] and `None` for every other,
    /// which [`crate::rules::validate`] checks in both directions. A fill with
    /// no recorded purpose would have nothing for rule 6 to compare against; a
    /// purpose on a step that fills nothing is a claim about a field the step
    /// never touches.
    pub fills: Option<FieldPurpose>,
}

impl ProcedureStep {
    /// A step with no arguments.
    pub fn new(verb: impl Into<String>, postcondition: PostconditionKind) -> Self {
        Self {
            verb: verb.into(),
            arguments: Vec::new(),
            postcondition,
            fills: None,
        }
    }

    /// The same step, with `arguments`.
    #[must_use]
    pub fn taking(mut self, arguments: Vec<StepArgument>) -> Self {
        self.arguments = arguments;
        self
    }

    /// The same step, recorded as filling a field of `purpose`.
    #[must_use]
    pub const fn filling(mut self, purpose: FieldPurpose) -> Self {
        self.fills = Some(purpose);
        self
    }

    /// Whether this step's verb is one that puts a value into a field.
    pub fn is_a_fill(&self) -> bool {
        FILL_VERBS.contains(&self.verb.as_str())
    }

    /// Whether any argument of this step is a position in what the person
    /// supplies, as the verb's compiled-in schema declares it.
    ///
    /// This is the question the narrowing asks — a step that needs a value
    /// only the person can give cannot run unless the task may ask for one
    /// (see [`crate::narrowing`]) — and the same question replay asks before
    /// it proposes anything. It is answered from the registry rather than from
    /// the record, so a record cannot make a parameter into a supplied value
    /// by claiming one.
    pub fn asks_for_a_supplied_value(&self, milestone: Milestone) -> bool {
        let Some(entry) = tool::resolve(&self.verb, milestone).entry() else {
            return false;
        };
        self.arguments.iter().any(|argument| {
            supply_of(&argument.value, declared_type(entry, &argument.name))
                == ArgumentSupply::PersonsPosition
        })
    }
}

/// What one argument needs somebody to produce when the step runs.
///
/// Four answers rather than "is this a literal", because the three that are
/// not a literal are three different situations with three different
/// resolutions, and a single answer would let a reader take any of them for
/// the others.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ArgumentSupply {
    /// A closed target requiring the current page's complete node identity.
    SemanticTarget,
    /// The record carries it.
    Recorded,
    /// A number an earlier step issued.
    ///
    /// Only a model turn's handle table could bind one, and a replayed
    /// procedure has no turn — see [`crate::replay`].
    Handle,
    /// A position in the values the person supplies when they are asked.
    ///
    /// The record names a field's *purpose* and never what was typed into it,
    /// which is exactly what `ParameterType::SuppliedValue` was built for
    /// (decisions 0063 and 0088): the position is minted at replay, by the
    /// browser, from an answer this process never sees.
    PersonsPosition,
    /// Bytes the person would type for the assistant to read.
    ///
    /// A person can be asked for two different things and they are not
    /// interchangeable. This one is `user.ask`'s shape — text the model then
    /// reads — and a replay has no model to read it and no field on a proposal
    /// to carry it.
    PersonsText,
}

impl ArgumentSupply {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::SemanticTarget => "semantic_target",
            Self::Recorded => "recorded",
            Self::Handle => "handle",
            Self::PersonsPosition => "persons_position",
            Self::PersonsText => "persons_text",
        }
    }
}

/// What `value` needs produced, given the type its parameter declares.
///
/// `declared` is the registry's answer for that parameter name and `None`
/// where the row declares no such name — which is a record
/// [`crate::rules::validate`] refuses, so the reading here only has to be the
/// safe one: a person's value at a parameter nothing declares is text rather
/// than a position.
pub fn supply_of(value: &StepValue, declared: Option<ParameterType>) -> ArgumentSupply {
    match value {
        StepValue::SemanticTarget { .. } => ArgumentSupply::SemanticTarget,
        StepValue::Literal(_) => ArgumentSupply::Recorded,
        StepValue::FromEarlierStep { .. } => ArgumentSupply::Handle,
        StepValue::FromPerson { .. } => match declared {
            Some(ParameterType::SuppliedValue) => ArgumentSupply::PersonsPosition,
            Some(
                ParameterType::Handle
                | ParameterType::Text
                | ParameterType::Address
                | ParameterType::Count
                | ParameterType::Flag
                | ParameterType::Choice(_),
            )
            | None => ArgumentSupply::PersonsText,
        },
    }
}

/// The type `entry`'s compiled-in schema declares for the parameter `name`.
pub fn declared_type(entry: &ToolEntry, name: &str) -> Option<ParameterType> {
    entry
        .definition()
        .parameter(name)
        .map(|parameter| parameter.value_type)
}

#[cfg(test)]
mod tests {
    use super::{
        ProcedureStep, StepArgument, StepValue, FILL_VERBS, MAX_PROCEDURE_STEPS,
        SUBSTITUTION_SIGILS,
    };
    use crate::field::FieldPurpose;
    use bip_types::action::PostconditionKind;
    use task_engine::tool::{ArgumentValue, ParameterType, REGISTRY};

    #[test]
    fn the_step_bound_is_thirty_two() {
        assert_eq!(MAX_PROCEDURE_STEPS, 32);
    }

    #[test]
    fn every_fill_verb_is_registered_and_is_not_the_submit() {
        for name in FILL_VERBS {
            assert!(
                REGISTRY.iter().any(|entry| entry.name == *name),
                "{name} is named as a fill and is not registered"
            );
        }
        assert!(!FILL_VERBS.contains(&"browser.form.submit"));
        assert!(
            ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
                .is_a_fill()
        );
        assert!(!ProcedureStep::new("browser.dom.read", PostconditionKind::NoMutation).is_a_fill());
    }

    #[test]
    fn a_literal_written_in_a_template_language_is_noticed() {
        for sigil in SUBSTITUTION_SIGILS {
            let text = format!("hello {sigil}step0.value}}");
            let value = StepValue::Literal(ArgumentValue::Text(text));
            assert!(value.carries_a_substitution_sigil(), "{sigil}");
        }
    }

    #[test]
    fn ordinary_text_and_the_two_valueless_shapes_carry_no_sigil() {
        for text in ["Rue de Rivoli 12", "50% off", "a{b}c", "$5.00", "#1"] {
            let value = StepValue::Literal(ArgumentValue::Text(text.to_owned()));
            assert!(!value.carries_a_substitution_sigil(), "{text}");
        }
        // A count carries no text at all, so there is nothing to scan.
        assert!(!StepValue::Literal(ArgumentValue::Count(3)).carries_a_substitution_sigil());
        assert!(!StepValue::FromEarlierStep { step: 0 }.carries_a_substitution_sigil());
        assert!(!StepValue::FromPerson {
            purpose: FieldPurpose::EmailAddress
        }
        .carries_a_substitution_sigil());
    }

    #[test]
    fn each_shape_stands_for_exactly_one_type_and_never_for_nothing() {
        assert_eq!(
            StepValue::FromEarlierStep { step: 4 }.schema_witness(Some(ParameterType::Handle)),
            ArgumentValue::Handle(4)
        );
        // A person can be asked for two different things, and the witness takes
        // the shape the row declares. Asked for text — a search query, say —
        // the witness is the purpose's own name, so it is neither empty nor
        // unbounded.
        let asked_for_text = StepValue::FromPerson {
            purpose: FieldPurpose::PostalCode,
        }
        .schema_witness(Some(ParameterType::Text));
        assert_eq!(
            asked_for_text,
            ArgumentValue::Text("postal_code".to_owned())
        );
        for purpose in FieldPurpose::ALL {
            let witness = StepValue::FromPerson { purpose: *purpose }
                .schema_witness(Some(ParameterType::Text));
            assert!(
                witness.text().is_some_and(|text| !text.is_empty()),
                "{}",
                purpose.label()
            );
        }
        // Asked for a field value, the witness carries no bytes for any
        // purpose. This is the assertion that keeps a recorded fill from ever
        // holding what the person typed: there is nothing on it to hold.
        for purpose in FieldPurpose::ALL {
            let witness = StepValue::FromPerson { purpose: *purpose }
                .schema_witness(Some(ParameterType::SuppliedValue));
            assert_eq!(
                witness,
                ArgumentValue::SuppliedValue(0),
                "{}",
                purpose.label()
            );
            assert!(witness.text().is_none(), "{}", purpose.label());
        }
        let literal = ArgumentValue::Address("https://example.test".to_owned());
        assert_eq!(
            StepValue::Literal(literal.clone()).schema_witness(Some(ParameterType::Address)),
            literal
        );
    }

    #[test]
    fn every_value_shape_has_a_distinct_compiled_in_label() {
        let shapes = [
            StepValue::Literal(ArgumentValue::Count(0)),
            StepValue::FromEarlierStep { step: 0 },
            StepValue::FromPerson {
                purpose: FieldPurpose::Quantity,
            },
        ];
        let mut seen: Vec<&str> = Vec::new();
        for shape in &shapes {
            assert!(!seen.contains(&shape.label()), "{}", shape.label());
            seen.push(shape.label());
        }
    }

    #[test]
    fn a_step_is_built_by_naming_its_parts_and_nothing_else() {
        let step = ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
            .taking(vec![
                StepArgument::new("field", StepValue::FromEarlierStep { step: 0 }),
                StepArgument::literal("value", ArgumentValue::Text("Paris".to_owned())),
            ])
            .filling(FieldPurpose::Locality);
        assert_eq!(step.arguments.len(), 2);
        assert_eq!(step.fills, Some(FieldPurpose::Locality));
        assert_eq!(step.postcondition, PostconditionKind::NodeValueChanged);
    }
}
