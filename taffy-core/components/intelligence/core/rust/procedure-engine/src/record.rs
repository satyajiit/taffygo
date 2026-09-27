// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The record itself: what it is called, what it is about, and where it came
//! from.
//!
//! # Provenance is a field, and it is not a permission
//!
//! [`ProcedureProvenance`] exists so a person can see where a procedure came
//! from and so an audit record can say so. It is deliberately not an input to
//! any authority decision, and this is the part that is easy to get wrong in
//! the tempting direction: a recorded procedure *feels* more trustworthy than
//! an authored one, because the assistant is known to have executed those exact
//! steps successfully. That feeling is about the **past** page. The next page
//! may differ, may be a different site shaped to resemble it, or may be
//! hostile. Provenance tells you where a sequence came from and nothing about
//! the document it is about to run against, and only the second question is a
//! security question.
//!
//! Nothing in this crate reads provenance to decide anything. The test
//! `two_provenances_are_validated_identically` is what keeps that true as the
//! rules grow.
//!
//! # Scope is constructed, not validated later
//!
//! [`ProcedureScope`] can only be built from a canonical tuple origin, so a
//! scope that could never match is refused at the moment it is written rather
//! than carried to a later check. Two shapes are refused and both matter:
//!
//! - **An opaque origin.** It is session-local by construction, so a procedure
//!   scoped to one is dead the moment the session that produced it ends. Storing
//!   it would store a record that can never match again and can never be
//!   explained to a person either — an opaque origin has no place to show.
//! - **A serialization this build would not itself have written.**
//!   `HTTPS://Example.test` parses, and it is not what `normalize_serialization`
//!   emits. A scope is compared against a page's origin, and putting a value
//!   nothing canonicalised on the security side of that comparison is how two
//!   spellings of one origin become two origins — or worse, how one spelling of
//!   two origins becomes one.

use policy_engine::origin::{normalize_serialization, NormalizedOrigin, OriginError};

use crate::matching::{MatchCondition, MatchVerdict, PageFacts};
use crate::status::ProcedureStatus;
use crate::step::ProcedureStep;

/// How many bytes a procedure identifier may hold.
///
/// An identifier is minted by the core and shown to a person; it is not a place
/// to carry page text. The bound is small on purpose.
pub const MAX_PROCEDURE_ID_BYTES: usize = 128;

/// Why a record part could not be built.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RecordError {
    /// An identifier with no bytes.
    EmptyId,
    /// An identifier past [`MAX_PROCEDURE_ID_BYTES`].
    IdTooLong,
    /// An identifier holding something other than lowercase letters, digits,
    /// `-` and `.`. Identifiers are compared and displayed, so the character
    /// set is the smallest one that supports both.
    IdNotWellFormed,
    /// The scope's origin is not an origin at all.
    ScopeOrigin(OriginError),
    /// The scope's origin parses, but is not the canonical serialization of
    /// what it parses to.
    ScopeOriginNotCanonical,
    /// The scope's origin is opaque, and an opaque origin is session-local.
    ScopeOriginOpaque,
    /// Completed-task provenance or its bounded task identity is invalid.
    InvalidRecordedTask,
    /// The whole recorded sequence cannot be replayed by this build.
    UnsupportedRecordedFlow,
}

impl RecordError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::EmptyId => "empty_id",
            Self::IdTooLong => "id_too_long",
            Self::IdNotWellFormed => "id_not_well_formed",
            Self::ScopeOrigin(_) => "scope_origin",
            Self::ScopeOriginNotCanonical => "scope_origin_not_canonical",
            Self::ScopeOriginOpaque => "scope_origin_opaque",
            Self::InvalidRecordedTask => "invalid_recorded_task",
            Self::UnsupportedRecordedFlow => "unsupported_recorded_flow",
        }
    }
}

/// What a procedure is called.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ProcedureId(String);

impl ProcedureId {
    /// One identifier, or why it is not one.
    pub fn new(value: impl Into<String>) -> Result<Self, RecordError> {
        let value = value.into();
        if value.is_empty() {
            return Err(RecordError::EmptyId);
        }
        if value.len() > MAX_PROCEDURE_ID_BYTES {
            return Err(RecordError::IdTooLong);
        }
        let well_formed = value.chars().all(|character| {
            character.is_ascii_lowercase()
                || character.is_ascii_digit()
                || matches!(character, '-' | '.')
        });
        if !well_formed {
            return Err(RecordError::IdNotWellFormed);
        }
        Ok(Self(value))
    }

    /// The identifier as text.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// Which revision of a procedure this is.
///
/// A number rather than a hash, because a person is shown it. Superseding a
/// procedure mints the next version; editing one in place does not exist, which
/// is what lets an audit record name the exact steps that ran.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ProcedureVersion(pub u32);

impl ProcedureVersion {
    /// The version a newly drafted procedure carries.
    pub const FIRST: Self = Self(1);

    /// The version that supersedes this one, or `None` at the ceiling.
    ///
    /// Saturating would mint a version equal to the one it replaces, and two
    /// different step lists sharing one version is the failure an audit record
    /// cannot recover from.
    pub const fn next(self) -> Option<Self> {
        match self.0.checked_add(1) {
            Some(next) => Some(Self(next)),
            None => None,
        }
    }
}

/// Where a procedure came from. A field, never an authority input.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ProcedureProvenance {
    /// Written down by a person or shipped by the product.
    Authored,
    /// Built from a task the assistant completed once.
    RecordedFromTask,
    /// Installed from a pack somebody else wrote.
    ///
    /// Reserved: the member exists so the vocabulary is complete and the
    /// refusal is enumerable, and there is no code behind it. Installing a
    /// procedure somebody else wrote requires knowing who signed it, and key
    /// custody is open (OD-107) — `tools/release.d/fragments/` holds exactly
    /// one fragment and it records `credential_source: none`.
    InstalledFromPack,
}

impl ProcedureProvenance {
    /// Every member, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Authored,
        Self::RecordedFromTask,
        Self::InstalledFromPack,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Authored => "authored",
            Self::RecordedFromTask => "recorded_from_task",
            Self::InstalledFromPack => "installed_from_pack",
        }
    }

    /// Whether this member is reserved and has no code behind it.
    ///
    /// Reading this is not reading a trust level. It answers "does this build
    /// implement that path at all", which is the same answer for every record
    /// that carries the member, whatever the record says — see the module
    /// header, and `two_provenances_are_validated_identically` for the half
    /// that is implemented.
    pub const fn is_reserved(self) -> bool {
        matches!(self, Self::InstalledFromPack)
    }
}

/// What a procedure is about: the one origin it was written for.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct ProcedureScope {
    origin: NormalizedOrigin,
}

impl ProcedureScope {
    /// The scope for one canonical tuple origin written as text, or why it is
    /// not one.
    ///
    /// Canonicality is checked here and opacity is not, because
    /// `normalize_serialization` reads `scheme://host[:port]` and returns a
    /// tuple origin or an error — an opaque origin has no serialization to be
    /// written as, so it cannot arrive through this door.
    pub fn for_origin(serialization: &str) -> Result<Self, RecordError> {
        let origin = normalize_serialization(serialization).map_err(RecordError::ScopeOrigin)?;
        if origin.display() != serialization {
            return Err(RecordError::ScopeOriginNotCanonical);
        }
        Self::from_normalized(origin)
    }

    /// The scope for an origin the core has already decoded, or why it is not
    /// one.
    ///
    /// This is the door an opaque origin arrives through, and the one a
    /// recorder uses: a task that just finished holds the page's origin as a
    /// decoded value rather than as text. Nothing is re-canonicalised here —
    /// the value is already the output of the canonicaliser — so the one thing
    /// left to refuse is opacity.
    pub fn from_normalized(origin: NormalizedOrigin) -> Result<Self, RecordError> {
        if origin.is_opaque() {
            return Err(RecordError::ScopeOriginOpaque);
        }
        Ok(Self { origin })
    }

    /// The origin this procedure is about.
    pub const fn origin(&self) -> &NormalizedOrigin {
        &self.origin
    }

    /// Whether `observed` is the origin this procedure is about.
    ///
    /// Same-origin and never same-site: a procedure written for one origin has
    /// seen one origin's pages.
    pub fn covers(&self, observed: &NormalizedOrigin) -> bool {
        self.origin.is_same_origin(observed)
    }
}

/// One stored sequence of steps, however it was arrived at.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Procedure {
    /// What it is called.
    pub id: ProcedureId,
    /// Which revision this is.
    pub version: ProcedureVersion,
    /// Where it is in the one lifecycle.
    pub status: ProcedureStatus,
    /// The origin it was written for.
    pub scope: ProcedureScope,
    /// When it applies.
    pub condition: MatchCondition,
    /// What it does, in order.
    pub steps: Vec<ProcedureStep>,
    /// Where it came from. Not a permission.
    pub provenance: ProcedureProvenance,
    /// Exact completed task that supplied the recording, never external input.
    pub recorded_from_task_id: Option<String>,
}

impl Procedure {
    /// A draft, which is where every procedure enters whatever its provenance.
    ///
    /// "The assistant did this once" is evidence that it worked once, not that
    /// it is correct, so a recorded procedure enters at
    /// [`ProcedureStatus::Draft`] exactly as an authored one does.
    pub fn draft(
        id: ProcedureId,
        scope: ProcedureScope,
        condition: MatchCondition,
        steps: Vec<ProcedureStep>,
        provenance: ProcedureProvenance,
    ) -> Self {
        Self {
            id,
            version: ProcedureVersion::FIRST,
            status: ProcedureStatus::Draft,
            scope,
            condition,
            steps,
            provenance,
            recorded_from_task_id: None,
        }
    }

    /// Associates an entirely replayable recording with its verified source task.
    pub fn from_task(mut self, task_id: impl Into<String>) -> Result<Self, RecordError> {
        let task_id = task_id.into();
        if self.provenance != ProcedureProvenance::RecordedFromTask
            || task_id.is_empty()
            || task_id.len() > 256
            || !task_id
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || b"_-.:".contains(&byte))
        {
            return Err(RecordError::InvalidRecordedTask);
        }
        if !crate::recording::replayable_recording(&self) {
            return Err(RecordError::UnsupportedRecordedFlow);
        }
        self.recorded_from_task_id = Some(task_id);
        Ok(self)
    }

    /// Every verb this procedure names, in step order and without repeats.
    ///
    /// This is the allowlist a narrowing is built from — see
    /// [`crate::narrowing::narrow`], which is the only caller that may treat it
    /// as one.
    pub fn verbs(&self) -> Vec<String> {
        let mut names: Vec<String> = Vec::new();
        for step in &self.steps {
            if !names.iter().any(|seen| seen == &step.verb) {
                names.push(step.verb.clone());
            }
        }
        names
    }

    /// Whether this procedure applies to the page in front of the assistant.
    ///
    /// Scope first, and separately, because "this procedure is about somewhere
    /// else" and "this procedure is about here and does not apply yet" are two
    /// different things to tell a person — and only the first is a statement
    /// about the origin an action would reach.
    ///
    /// Status is deliberately not read here. Whether a record applies to a page
    /// and whether a person has it switched on are two questions, and a caller
    /// that folded them would have no way to explain to somebody why their
    /// disabled procedure "does not match".
    pub fn verdict(&self, facts: &PageFacts) -> MatchVerdict {
        if !self.scope.covers(facts.origin()) {
            return MatchVerdict::ScopeDiffers;
        }
        self.condition.evaluate(facts)
    }

    /// Whether this procedure may be proposed from at all.
    ///
    /// Status only. Being runnable is necessary and nowhere near sufficient:
    /// the scope must cover the page, the condition must hold, every structural
    /// rule must still pass at load, and every step is still decided by
    /// `policy-engine` one at a time.
    pub const fn is_runnable(&self) -> bool {
        self.status.is_runnable()
    }
}

#[cfg(test)]
mod tests {
    use super::{
        Procedure, ProcedureId, ProcedureProvenance, ProcedureScope, ProcedureVersion, RecordError,
        MAX_PROCEDURE_ID_BYTES,
    };
    use crate::matching::MatchCondition;
    use crate::status::ProcedureStatus;
    use crate::step::ProcedureStep;
    use bip_types::action::PostconditionKind;

    fn condition() -> MatchCondition {
        MatchCondition::new(vec![crate::matching::MatchClause::RolePresent(
            bip_types::snapshot::SemanticRole::SearchField,
        )])
    }

    #[test]
    fn an_identifier_is_bounded_and_well_formed() {
        assert!(ProcedureId::new("book.the.usual-table").is_ok());
        assert_eq!(ProcedureId::new(""), Err(RecordError::EmptyId));
        assert_eq!(
            ProcedureId::new("x".repeat(MAX_PROCEDURE_ID_BYTES + 1)),
            Err(RecordError::IdTooLong)
        );
        for rejected in ["Book", "book table", "book/table", "book\u{2028}"] {
            assert_eq!(
                ProcedureId::new(rejected),
                Err(RecordError::IdNotWellFormed),
                "{rejected}"
            );
        }
    }

    #[test]
    fn a_scope_is_a_canonical_tuple_origin_and_nothing_else() {
        let scope = ProcedureScope::for_origin("https://example.test").unwrap();
        assert!(scope.covers(scope.origin()));
        // Parses, and is not what this build would have written.
        assert_eq!(
            ProcedureScope::for_origin("HTTPS://Example.test"),
            Err(RecordError::ScopeOriginNotCanonical)
        );
        // The default port is canonically absent, so spelling it is a second
        // serialization of one origin.
        assert_eq!(
            ProcedureScope::for_origin("https://example.test:443"),
            Err(RecordError::ScopeOriginNotCanonical)
        );
        assert!(matches!(
            ProcedureScope::for_origin("not-an-origin"),
            Err(RecordError::ScopeOrigin(_))
        ));
    }

    #[test]
    fn an_opaque_origin_is_refused_at_the_door_it_can_arrive_through() {
        // A sandboxed frame's origin is session-local, so a procedure scoped to
        // one is dead the moment the session that produced it ends — and it can
        // never be shown to a person either, because an opaque origin has no
        // place to show. A recorder holds a decoded origin rather than text,
        // which is why this refusal lives on the decoded constructor.
        let opaque = policy_engine::origin::NormalizedOrigin::Opaque {
            opaque_id: "session-local".to_owned(),
        };
        assert_eq!(
            ProcedureScope::from_normalized(opaque),
            Err(RecordError::ScopeOriginOpaque)
        );
        let Ok(tuple) = policy_engine::origin::normalize_serialization("https://example.test")
        else {
            unreachable!("the fixture origin is an origin")
        };
        assert!(ProcedureScope::from_normalized(tuple).is_ok());
    }

    #[test]
    fn a_scope_never_covers_a_neighbouring_origin() {
        let scope = ProcedureScope::for_origin("https://example.test").unwrap();
        for other in [
            "https://www.example.test",
            "http://example.test",
            "https://example.test.evil.test",
        ] {
            let observed = ProcedureScope::for_origin(other).unwrap();
            assert!(!scope.covers(observed.origin()), "{other}");
        }
    }

    #[test]
    fn every_procedure_enters_at_draft_however_it_was_arrived_at() {
        for provenance in [
            ProcedureProvenance::Authored,
            ProcedureProvenance::RecordedFromTask,
        ] {
            let procedure = Procedure::draft(
                ProcedureId::new("one").unwrap(),
                ProcedureScope::for_origin("https://example.test").unwrap(),
                condition(),
                Vec::new(),
                provenance,
            );
            assert_eq!(procedure.status, ProcedureStatus::Draft);
            assert_eq!(procedure.version, ProcedureVersion::FIRST);
            assert!(!procedure.is_runnable());
        }
    }

    #[test]
    fn the_reserved_provenance_is_the_only_reserved_one() {
        assert!(ProcedureProvenance::InstalledFromPack.is_reserved());
        assert!(!ProcedureProvenance::Authored.is_reserved());
        assert!(!ProcedureProvenance::RecordedFromTask.is_reserved());
        let mut seen: Vec<&str> = Vec::new();
        for provenance in ProcedureProvenance::ALL {
            assert!(
                !seen.contains(&provenance.label()),
                "{}",
                provenance.label()
            );
            seen.push(provenance.label());
        }
    }

    #[test]
    fn a_version_refuses_to_repeat_itself_at_the_ceiling() {
        assert_eq!(ProcedureVersion::FIRST.next(), Some(ProcedureVersion(2)));
        assert_eq!(ProcedureVersion(u32::MAX).next(), None);
    }

    #[test]
    fn a_page_on_another_origin_is_told_apart_from_a_page_that_does_not_apply() {
        use crate::matching::{MatchVerdict, PageFacts, PageNode};
        use policy_engine::origin::normalize_serialization;

        let procedure = Procedure::draft(
            ProcedureId::new("one").unwrap(),
            ProcedureScope::for_origin("https://example.test").unwrap(),
            condition(),
            vec![ProcedureStep::new(
                "browser.dom.read",
                PostconditionKind::NoMutation,
            )],
            ProcedureProvenance::Authored,
        );
        let search = PageNode::new(bip_types::snapshot::SemanticRole::SearchField);

        let here = PageFacts::new(
            normalize_serialization("https://example.test").unwrap(),
            vec![search.clone()],
        );
        assert_eq!(procedure.verdict(&here), MatchVerdict::Matched);

        // The same page, everywhere else: a procedure written for one origin
        // has seen one origin's pages, and looking alike is not evidence.
        let elsewhere = PageFacts::new(
            normalize_serialization("https://example.evil.test").unwrap(),
            vec![search],
        );
        assert_eq!(procedure.verdict(&elsewhere), MatchVerdict::ScopeDiffers);

        // The right origin, and the page is not the one it was written for.
        let bare = PageFacts::new(
            normalize_serialization("https://example.test").unwrap(),
            Vec::new(),
        );
        assert!(matches!(procedure.verdict(&bare), MatchVerdict::Unmet(_)));
    }

    #[test]
    fn the_verb_list_keeps_step_order_and_drops_repeats() {
        let steps = vec![
            ProcedureStep::new("browser.navigate", PostconditionKind::CommittedNavigation),
            ProcedureStep::new("browser.dom.read", PostconditionKind::NoMutation),
            ProcedureStep::new("browser.navigate", PostconditionKind::CommittedNavigation),
        ];
        let procedure = Procedure::draft(
            ProcedureId::new("one").unwrap(),
            ProcedureScope::for_origin("https://example.test").unwrap(),
            condition(),
            steps,
            ProcedureProvenance::Authored,
        );
        assert_eq!(
            procedure.verbs(),
            vec!["browser.navigate", "browser.dom.read"]
        );
    }
}
