// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The record as bytes, and the bytes back as the record.
//!
//! Decision 0091 section 2. `core_skill_version.definition` is a `BLOB` and
//! everything about what a procedure *is* — the step list, a step's verb and
//! postcondition kind, an argument's shape, a match clause, a field purpose —
//! lives inside it and is this crate's to serialize. That choice is what makes
//! adding a step kind a change to one serializer and its round-trip test
//! instead of a journal head-version bump, and the bump is the expensive half:
//! raising the head means revisiting **every** migration entry rather than
//! adding one, against a ladder that is inside the checksummed identity and
//! cannot be corrected in place.
//!
//! The trade the same section names is what this module is for. The database
//! can no longer check anything inside a definition, so this crate has to — at
//! store and again at load — and every check here is written to refuse rather
//! than to repair.
//!
//! # Everything closed is written as its own compiled-in name
//!
//! There is not one numeric discriminant in this format. A status, a
//! provenance, a clause shape, a role, a state, a phrase, a postcondition
//! kind, a value shape, an argument type and a field purpose are each written
//! as the short name that enumeration already carries for an audit record, and
//! read back by looking that name up in the enumeration's own `ALL`. Three
//! things follow, and each of them is the reason:
//!
//! - **Unknown fails closed and says which vocabulary it failed in.** A name
//!   no member claims is [`DecodeError::UnknownMember`] naming the
//!   [`Enumeration`] — never a default, never a skip, and never the nearest
//!   member.
//! - **Reordering an enumeration cannot silently re-read a stored record.** An
//!   index would; a name cannot. This matters most for the two enumerations
//!   this crate does not own, `bip_types`' `SemanticRole` and `NodeState`,
//!   whose declaration order is the schema's to change.
//! - **There is no second table to maintain.** The names are the ones
//!   `label()` and `wire()` already return, and the existing "every member has
//!   a distinct label" tests are what keep them usable as keys.
//!
//! # Version, bound, and the count beside the blob
//!
//! [`DEFINITION_FORMAT_VERSION`] is written into every definition, so a
//! version 1 definition remains readable without a source-task association.
//! Version 2 adds that optional association; all new writes use version 2.
//! Unsupported versions fail with [`DecodeError::UnsupportedFormatVersion`]
//! rather than being misread. A refused definition closes bootstrap with a
//! named reason instead of quietly omitting a saved arrangement.
//!
//! [`MAX_DEFINITION_BYTES`] is the column's own `CHECK`, and it is enforced in
//! both directions: [`encode`] stops and names [`EncodeError::TooLarge`] as
//! soon as it is past the bound rather than assembling a definition it has
//! already decided to refuse, and [`decode`] refuses a blob larger than the
//! column could have held. Nothing here truncates anything.
//!
//! `step_count` sits beside the blob as its own column because a bound has to
//! be enforceable by the database that holds it, so the count is in the header
//! where [`step_count`] can read it without decoding, and
//! [`decode_beside`] refuses a definition whose count is not the column's
//! ([`DecodeError::StepCountNotTheColumns`]).
//!
//! # What this module does not decide
//!
//! It does not validate. `MAX_PROCEDURE_STEPS`, the six structural rules and
//! the registry are [`crate::rules::validate`]'s, which runs when a procedure
//! is stored and again when it is loaded. A codec that also enforced rule 5
//! would be the second place that rule is expressed, and two places are two
//! answers waiting to differ. What this module refuses is what it cannot
//! *write down* or cannot *read back*, which is a different question with
//! different answers.

mod decode;
mod encode;

pub use self::decode::{decode, decode_beside, format_version, step_count};
pub use self::encode::encode;

use crate::record::RecordError;

/// How many bytes one encoded definition may hold.
///
/// The `CHECK` on `core_skill_version.definition` in
/// `taffy-core/components/storage/core/schema/core_service_journal.json`, in
/// this crate's own words. The number is the column's; this constant is what
/// lets the refusal happen where a person can be told about it rather than as
/// a constraint violation from a store.
pub const MAX_DEFINITION_BYTES: usize = 65_536;

/// The layout every definition this build writes carries.
///
/// One number for the whole format. A step kind, a field purpose or a clause
/// shape added to this crate does not move it — that is decision 0091's fourth
/// validation item and the point of putting the record in a blob at all — and
/// `adding_a_step_kind_costs_no_schema_version` is the assertion.
pub const DEFINITION_FORMAT_VERSION: u16 = 2;

/// What a definition starts with.
///
/// Four bytes, so a blob that is not a definition at all is
/// [`DecodeError::NotADefinition`] rather than a version number read out of
/// somebody else's bytes.
const MAGIC: [u8; 4] = *b"TFSK";

/// One closed vocabulary a definition names.
///
/// Carried by [`DecodeError::UnknownMember`] so a refusal says *which*
/// enumeration a stored name is not in. "Something in this definition is not a
/// member" is not a thing anybody can act on; "the field purpose is not one
/// this build has" is.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Enumeration {
    /// [`crate::status::ProcedureStatus`].
    ProcedureStatus,
    /// [`crate::record::ProcedureProvenance`].
    ProcedureProvenance,
    /// Which of [`crate::matching::MatchClause`]'s three shapes.
    MatchClauseShape,
    /// `bip_types::snapshot::SemanticRole`.
    SemanticRole,
    /// `bip_types::snapshot::NodeState`.
    NodeState,
    /// [`crate::matching::PhraseId`].
    Phrase,
    /// `bip_types::action::PostconditionKind`.
    PostconditionKind,
    /// Which shape of [`crate::step::StepValue`] supplies an argument.
    StepValueShape,
    /// Which of `task_engine::tool::ArgumentValue`'s seven types.
    ArgumentValueType,
    /// [`crate::field::FieldPurpose`].
    FieldPurpose,
}

impl Enumeration {
    /// Every vocabulary a definition names, in the order a definition reaches
    /// them.
    pub const ALL: &'static [Self] = &[
        Self::ProcedureStatus,
        Self::ProcedureProvenance,
        Self::MatchClauseShape,
        Self::SemanticRole,
        Self::NodeState,
        Self::Phrase,
        Self::PostconditionKind,
        Self::StepValueShape,
        Self::ArgumentValueType,
        Self::FieldPurpose,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ProcedureStatus => "procedure_status",
            Self::ProcedureProvenance => "procedure_provenance",
            Self::MatchClauseShape => "match_clause_shape",
            Self::SemanticRole => "semantic_role",
            Self::NodeState => "node_state",
            Self::Phrase => "phrase",
            Self::PostconditionKind => "postcondition_kind",
            Self::StepValueShape => "step_value_shape",
            Self::ArgumentValueType => "argument_value_type",
            Self::FieldPurpose => "field_purpose",
        }
    }
}

/// Why a procedure could not be written down.
///
/// Every member is a record this format cannot *express*, and none of them is
/// a record this format disapproves of: the rules are
/// [`crate::rules::validate`]'s. A refusal here is always the whole
/// definition, because half a definition is a different procedure wearing the
/// original's identity.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EncodeError {
    /// Past [`MAX_DEFINITION_BYTES`].
    ///
    /// `bytes` is how much had been written when the encoder stopped, which is
    /// at least the bound and less than the whole. The encoder refuses as soon
    /// as it is past rather than assembling a definition it will not return,
    /// so the number says "at least this much" and never "exactly this much".
    TooLarge {
        /// How much had been written when the encoder stopped.
        bytes: usize,
    },
    /// More steps than a definition can count.
    TooManySteps {
        /// How many.
        steps: usize,
    },
    /// More match clauses than a definition can count.
    TooManyClauses {
        /// How many.
        clauses: usize,
    },
    /// More arguments on one step than a definition can count.
    TooManyArguments {
        /// How many.
        arguments: usize,
    },
    /// A string longer than a definition can length-prefix.
    TextTooLong {
        /// How long.
        bytes: usize,
    },
    /// A reference to an earlier step past what a definition can hold.
    StepReferenceTooLarge {
        /// Which step it named.
        step: usize,
    },
}

impl EncodeError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::TooLarge { .. } => "too_large",
            Self::TooManySteps { .. } => "too_many_steps",
            Self::TooManyClauses { .. } => "too_many_clauses",
            Self::TooManyArguments { .. } => "too_many_arguments",
            Self::TextTooLong { .. } => "text_too_long",
            Self::StepReferenceTooLarge { .. } => "step_reference_too_large",
        }
    }
}

/// Why bytes are not a procedure this build can read.
///
/// Closed, and every member refuses the whole definition. There is no member
/// meaning "read as much as made sense": a definition read in part is a
/// procedure that will be replayed, and the part that did not survive is where
/// it silently does something other than what the person agreed to.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DecodeError {
    /// Larger than the column could have held, so it is not from the column.
    TooLarge {
        /// How large.
        bytes: usize,
    },
    /// The bytes do not start the way a definition starts.
    NotADefinition,
    /// A definition, of a layout this build does not have.
    ///
    /// Decision 0091's second validation item: refused **by its version**,
    /// named, and never read under a layout that was not the one it was
    /// written in.
    UnsupportedFormatVersion {
        /// The version the definition claims.
        found: u16,
    },
    /// The definition ends inside something it declared.
    Truncated,
    /// The definition ends, and the blob does not.
    TrailingBytes,
    /// A string that is not text.
    NotUtf8,
    /// A name no member of that vocabulary claims.
    UnknownMember {
        /// Which vocabulary.
        enumeration: Enumeration,
    },
    /// A presence byte that is neither absent nor present.
    OptionTagUnknown {
        /// What was written there.
        found: u8,
    },
    /// A flag byte that is neither yes nor no.
    FlagNotZeroOrOne {
        /// What was written there.
        found: u8,
    },
    /// A reference to an earlier step this target cannot hold.
    ///
    /// The mirror of [`EncodeError::StepReferenceTooLarge`], and unreachable
    /// on a target whose `usize` is at least as wide as the stored number. It
    /// is a refusal rather than a saturation because a step reference that
    /// came back as a *different* step would satisfy rule 2 and replay against
    /// the wrong handle.
    StepReferenceUnreadable {
        /// What the definition named.
        step: u32,
    },
    /// The definition's step count is not the column's.
    ///
    /// The count is beside the blob because a bound has to be enforceable by
    /// the database that holds it. Two answers to how many steps a procedure
    /// has is one answer too many, and the caller is told which two.
    StepCountNotTheColumns {
        /// What the column beside the blob says.
        column: u32,
        /// What the definition says.
        definition: u16,
    },
    /// A part of the record this build would not have written.
    ///
    /// The identifier and the scope are re-made through their own
    /// constructors rather than dropped into the struct, so a stored scope
    /// that is not a canonical tuple origin — or an identifier that is not a
    /// well-formed one — is refused at load exactly as it would have been
    /// refused at first writing.
    Record(RecordError),
}

impl DecodeError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::TooLarge { .. } => "too_large",
            Self::NotADefinition => "not_a_definition",
            Self::UnsupportedFormatVersion { .. } => "unsupported_format_version",
            Self::Truncated => "truncated",
            Self::TrailingBytes => "trailing_bytes",
            Self::NotUtf8 => "not_utf8",
            Self::UnknownMember { .. } => "unknown_member",
            Self::OptionTagUnknown { .. } => "option_tag_unknown",
            Self::FlagNotZeroOrOne { .. } => "flag_not_zero_or_one",
            Self::StepReferenceUnreadable { .. } => "step_reference_unreadable",
            Self::StepCountNotTheColumns { .. } => "step_count_not_the_columns",
            Self::Record(reason) => reason.label(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{DecodeError, EncodeError, Enumeration, MAGIC, MAX_DEFINITION_BYTES};
    use crate::record::RecordError;

    #[test]
    fn the_bound_is_the_columns_own_check() {
        // `core_skill_version.definition BLOB NOT NULL CHECK(length(definition)
        // > 0 AND length(definition) <= 65536)`. The number lives in the
        // journal schema; this is the copy that lets the refusal be named
        // before a store ever sees the row.
        assert_eq!(MAX_DEFINITION_BYTES, 65_536);
        assert_eq!(MAGIC.len(), 4);
    }

    #[test]
    fn every_enumeration_has_a_distinct_compiled_in_label() {
        let mut seen: Vec<&str> = Vec::new();
        for enumeration in Enumeration::ALL {
            assert!(
                !seen.contains(&enumeration.label()),
                "{}",
                enumeration.label()
            );
            seen.push(enumeration.label());
        }
        assert_eq!(seen.len(), Enumeration::ALL.len());
    }

    #[test]
    fn every_refusal_shape_has_a_compiled_in_label() {
        let encoding = [
            EncodeError::TooLarge { bytes: 1 },
            EncodeError::TooManySteps { steps: 1 },
            EncodeError::TooManyClauses { clauses: 1 },
            EncodeError::TooManyArguments { arguments: 1 },
            EncodeError::TextTooLong { bytes: 1 },
            EncodeError::StepReferenceTooLarge { step: 1 },
        ];
        let mut seen: Vec<&str> = Vec::new();
        for error in encoding {
            assert!(!seen.contains(&error.label()), "{}", error.label());
            seen.push(error.label());
        }
        let decoding = [
            DecodeError::TooLarge { bytes: 1 },
            DecodeError::NotADefinition,
            DecodeError::UnsupportedFormatVersion { found: 0 },
            DecodeError::Truncated,
            DecodeError::TrailingBytes,
            DecodeError::NotUtf8,
            DecodeError::UnknownMember {
                enumeration: Enumeration::Phrase,
            },
            DecodeError::OptionTagUnknown { found: 2 },
            DecodeError::FlagNotZeroOrOne { found: 2 },
            DecodeError::StepReferenceUnreadable { step: 1 },
            DecodeError::StepCountNotTheColumns {
                column: 1,
                definition: 2,
            },
            DecodeError::Record(RecordError::EmptyId),
        ];
        let mut named: Vec<&str> = Vec::new();
        for error in decoding {
            assert!(!named.contains(&error.label()), "{}", error.label());
            named.push(error.label());
        }
    }
}
