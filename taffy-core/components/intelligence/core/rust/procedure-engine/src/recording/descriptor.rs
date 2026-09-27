// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Descriptors of the complete live sequence the person watched.
//!
//! The core's transient recorder combines browser-delivered semantic graphs
//! with committed action verification and handback events. The durable journal
//! alone lacks the page facts needed to reconstruct these descriptors. Manual
//! teaching reaches the same bounded recording validator through the browser.
//!
//! Values are references, compiled choices, counts or flags. The two replayable
//! additions are a closed semantic role and phrase, resolved on each fresh
//! page, and the public HTTPS starting address shown in full for review. The
//! address validator rejects credentials, query strings, fragments and any
//! different origin. Later link destinations are never stored. Field values,
//! search text and arbitrary page labels have no recording representation.
//!
//! Every admitted operation becomes an entry. [`LedgerEntry::Undescribed`]
//! refuses the whole recording, and [`Recording::admitted`] checks the complete
//! count. An unsupported step cannot silently disappear from a saved sequence.

use bip_types::action::PostconditionKind;
use bip_types::snapshot::SemanticRole;
use policy_engine::origin::NormalizedOrigin;

use crate::field::FieldPurpose;
use crate::matching::{MatchClause, PhraseId};

/// One value of one recorded argument.
///
/// Public starting addresses are the only stored strings; all other values
/// are closed vocabulary or references and never contain person-entered bytes.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum RecordedValue {
    /// A public starting address shown in full for the person's review.
    PublicAddress(String),
    /// Closed semantic vocabulary, resolved uniquely against each fresh page.
    SemanticTarget {
        role: SemanticRole,
        phrase: PhraseId,
    },
    /// The handle an earlier step of this same recording produced.
    ///
    /// The index is into the recording's own entry list. Whether it is
    /// *strictly* earlier is rule 2's question and is decided by
    /// [`crate::rules::validate`], where the step's own position is known.
    FromEarlierStep {
        /// Which earlier step.
        step: usize,
    },
    /// A named request to the person, standing for a value the browser held
    /// and the record may not.
    ///
    /// The bytes were minted in the browser's value vault when the person
    /// entered them, spent once, and scrubbed. What survives into the record
    /// is the classification of the field they were for, which is what a
    /// person is shown when the step asks them again.
    FromPerson {
        /// What the person is being asked for.
        purpose: FieldPurpose,
    },
    /// One name from the parameter's own compiled-in set, by position in it.
    ///
    /// An index rather than a name so that the string in the record comes from
    /// the build's table and never from the caller. It is the same rule
    /// `ToolEntry::canonical_name` follows for the verb, applied to the one
    /// argument type whose values are also compiled in.
    Choice {
        /// Which of the parameter's declared names.
        index: usize,
    },
    /// A count.
    Count(u64),
    /// Yes or no.
    Flag(bool),
}

impl RecordedValue {
    /// A short, compiled-in name for the shape, safe to record in an audit
    /// event.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::PublicAddress(_) => "public_address",
            Self::SemanticTarget { .. } => "semantic_target",
            Self::FromEarlierStep { .. } => "from_earlier_step",
            Self::FromPerson { .. } => "from_person",
            Self::Choice { .. } => "choice",
            Self::Count(_) => "count",
            Self::Flag(_) => "flag",
        }
    }
}

/// One argument of one observed step.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct ArgumentDescriptor {
    /// Which of the verb's declared parameters this is, by position in the
    /// compiled-in schema.
    ///
    /// A position and not a name, for the reason [`RecordedValue::Choice`]
    /// gives: the name that reaches the record is the build's own.
    pub parameter: usize,
    /// What was supplied for it.
    pub value: RecordedValue,
}

impl ArgumentDescriptor {
    /// One argument.
    pub const fn new(parameter: usize, value: RecordedValue) -> Self {
        Self { parameter, value }
    }
}

/// One step, as the browser observed it happen.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StepDescriptor {
    /// The tool the action was dispatched under.
    ///
    /// Canonicalised through the registry before it reaches the record, so the
    /// stored spelling is the table's and not the caller's.
    pub verb: String,
    /// Its arguments.
    pub arguments: Vec<ArgumentDescriptor>,
    /// The effect the action was verified against, from BIP, unextended.
    pub postcondition: PostconditionKind,
    /// What the classifier called the field this step filled, when it filled
    /// one.
    ///
    /// [`FieldPurpose::Unknown`] is a legitimate answer and not an omission:
    /// the record then carries a fill that will always be handed to the person
    /// (rule 6), which is visible and correct. Leaving it `None` on a fill is
    /// the omission, and it is refused.
    pub fills: Option<FieldPurpose>,
}

impl StepDescriptor {
    /// A step with no arguments.
    pub fn new(verb: impl Into<String>, postcondition: PostconditionKind) -> Self {
        Self {
            verb: verb.into(),
            arguments: Vec::new(),
            postcondition,
            fills: None,
        }
    }

    /// The same descriptor, with `arguments`.
    #[must_use]
    pub fn taking(mut self, arguments: Vec<ArgumentDescriptor>) -> Self {
        self.arguments = arguments;
        self
    }

    /// The same descriptor, recorded as filling a field of `purpose`.
    #[must_use]
    pub const fn filling(mut self, purpose: FieldPurpose) -> Self {
        self.fills = Some(purpose);
        self
    }
}

/// Why an admitted action could not be turned into a step.
///
/// A closed enumeration, because it travels to the person as the reason their
/// recording produced nothing. Every member is a fact about the *browser's*
/// side of the boundary; none of them is a judgement the core could have made.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum UndescribedReason {
    /// An argument was text a page or a model authored, and a record holds no
    /// such thing. See the module header.
    ValueIsNotAReference,
    /// The action targeted a node no earlier step of this recording produced,
    /// so there is no reference a later replay could bind.
    NodeNotFromThisRecording,
    /// The action was admitted and never settled, so what it did is unknown. A
    /// step recorded from an unknown outcome is a claim nobody observed.
    OutcomeUnknown,
    /// The action was not one a procedure step can name — a handover, or an
    /// effect dispatched outside the task's plan.
    NotARecordableVerb,
}

impl UndescribedReason {
    /// Every member, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::ValueIsNotAReference,
        Self::NodeNotFromThisRecording,
        Self::OutcomeUnknown,
        Self::NotARecordableVerb,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ValueIsNotAReference => "value_is_not_a_reference",
            Self::NodeNotFromThisRecording => "node_not_from_this_recording",
            Self::OutcomeUnknown => "outcome_unknown",
            Self::NotARecordableVerb => "not_a_recordable_verb",
        }
    }
}

/// One capability the ledger admitted, as the browser was able to describe it.
///
/// There is no third variant meaning "leave this one out". See the module
/// header on why a gap is worse than a refusal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LedgerEntry {
    /// A step.
    Described(StepDescriptor),
    /// Not a step, and why.
    Undescribed(UndescribedReason),
}

/// Everything the browser observed about one finished task.
///
/// Nothing is checked here. The bounds and the six rules belong to
/// [`crate::recording::record`], which is the one judge, for the reason
/// [`crate::matching::MatchCondition::new`] gives: a rule expressed in two
/// places is two answers waiting to differ.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Recording {
    origin: NormalizedOrigin,
    facts: Vec<MatchClause>,
    entries: Vec<LedgerEntry>,
    admitted: usize,
}

impl Recording {
    /// What the browser observed.
    ///
    /// `admitted` is the ledger's own count of capabilities it admitted for
    /// this task, read from the ledger rather than from `entries`, so that the
    /// two can disagree and be caught disagreeing.
    pub const fn new(
        origin: NormalizedOrigin,
        facts: Vec<MatchClause>,
        entries: Vec<LedgerEntry>,
        admitted: usize,
    ) -> Self {
        Self {
            origin,
            facts,
            entries,
            admitted,
        }
    }

    /// The origin the task's one consented source was on.
    pub const fn origin(&self) -> &NormalizedOrigin {
        &self.origin
    }

    /// What the browser observed about the page, in the vocabulary the
    /// catalogue closes.
    pub fn facts(&self) -> &[MatchClause] {
        &self.facts
    }

    /// One entry per capability the ledger admitted, in the order it admitted
    /// them.
    pub fn entries(&self) -> &[LedgerEntry] {
        &self.entries
    }

    /// How many capabilities the ledger says it admitted.
    pub const fn admitted(&self) -> usize {
        self.admitted
    }
}

#[cfg(test)]
mod tests {
    use super::{
        ArgumentDescriptor, LedgerEntry, RecordedValue, Recording, StepDescriptor,
        UndescribedReason,
    };
    use crate::field::FieldPurpose;
    use crate::matching::MatchClause;
    use bip_types::action::PostconditionKind;
    use bip_types::snapshot::SemanticRole;
    use policy_engine::origin::normalize_serialization;

    fn origin() -> policy_engine::origin::NormalizedOrigin {
        let Ok(origin) = normalize_serialization("https://example.test") else {
            unreachable!("the fixture origin is an origin")
        };
        origin
    }

    #[test]
    fn no_recorded_value_can_carry_a_string() {
        // The property this module exists for, asserted the only way a type
        // can be asserted about: by construction. Every variant is built here,
        // and none of them takes text — so a search query a model composed, an
        // address read off a link, or a value a person typed has no spelling
        // in this vocabulary at all.
        let shapes = [
            RecordedValue::FromEarlierStep { step: 0 },
            RecordedValue::FromPerson {
                purpose: FieldPurpose::SearchTerms,
            },
            RecordedValue::Choice { index: 0 },
            RecordedValue::Count(3),
            RecordedValue::Flag(true),
        ];
        let mut seen: Vec<&str> = Vec::new();
        for shape in &shapes {
            assert!(!seen.contains(&shape.label()), "{}", shape.label());
            seen.push(shape.label());
        }
        assert_eq!(seen.len(), shapes.len());
    }

    #[test]
    fn a_descriptor_is_built_by_naming_its_parts_and_nothing_else() {
        let descriptor =
            StepDescriptor::new("browser.form.fill", PostconditionKind::NodeValueChanged)
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
        assert_eq!(descriptor.arguments.len(), 2);
        assert_eq!(descriptor.fills, Some(FieldPurpose::EmailAddress));
        assert_eq!(
            descriptor.postcondition,
            PostconditionKind::NodeValueChanged
        );
    }

    #[test]
    fn every_undescribed_reason_has_a_distinct_compiled_in_label() {
        let mut seen: Vec<&str> = Vec::new();
        for reason in UndescribedReason::ALL {
            assert!(!seen.contains(&reason.label()), "{}", reason.label());
            seen.push(reason.label());
        }
    }

    #[test]
    fn a_recording_reports_the_ledger_count_and_the_entries_separately() {
        // The two come from different places on the browser side — the ledger
        // and the descriptor builder — and the whole point of carrying both is
        // that they can be compared. A `Recording` that derived one from the
        // other would have nothing to compare.
        let recording = Recording::new(
            origin(),
            vec![MatchClause::RolePresent(SemanticRole::Document)],
            vec![LedgerEntry::Undescribed(UndescribedReason::OutcomeUnknown)],
            4,
        );
        assert_eq!(recording.admitted(), 4);
        assert_eq!(recording.entries().len(), 1);
        assert_eq!(recording.facts().len(), 1);
        assert_eq!(recording.origin(), &origin());
    }
}
