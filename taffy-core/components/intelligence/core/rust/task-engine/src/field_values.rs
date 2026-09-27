// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Asking the person to fill in a form, and learning only that they did.
//!
//! # What this carries, and what it refuses to
//!
//! Decision 0088 splits asking a person for a *value* from asking them a
//! *question*. [`crate::command::Command::RequestUserInput`] is the question:
//! the person types text and the assistant reads it. This is the value: the
//! person types into a surface the browser owns, the browser mints each answer
//! into its own vault, and what comes back into this process is a **count**.
//!
//! So there is no field here for a value, and there is deliberately no field
//! for anything derived from one either — no length, no shape, no mask, no
//! digest. A value that goes into a form field comes from a tiny domain, and a
//! digest over a domain that small is the value (decision 0063 section 4).
//! What this module holds is an identity, a target, and how many answers came
//! back.
//!
//! # Why the model names a form and not a list of fields
//!
//! The model designates one node — the form — and the browser decides which of
//! its fields need a person and what kind of thing each one is. That is not a
//! convenience: the browser is already the authority on a field's
//! classification, because it re-reads it from the node at dispatch and
//! believes nothing the proposal asserted about it. A model that could
//! enumerate the fields needing a person could also enumerate the ones that do
//! not, and there would then be two places a field's class is decided — which
//! would disagree exactly when it mattered.
//!
//! # A request is not authority
//!
//! Nothing here grants anything. The person filling a sheet in produces
//! references the browser holds; whether any of them may reach a field is
//! `policy-engine`'s answer, made per action, and the write classes stay
//! refused until the milestone of decision 0089 is ratified.

use core::fmt;

use bip_types::identity::{SemanticNodeId, TabId};

/// Maximum bytes in a field-value request identity.
pub const MAX_FIELD_VALUE_REQUEST_ID_BYTES: usize = 128;

/// Maximum bytes in one field's node identity, the observation's own bound.
pub const MAX_FIELD_NODE_ID_BYTES: usize = crate::observation::MAX_OBSERVATION_IDENTIFIER_BYTES;

/// How many fields one request may cover.
///
/// A bound on what one surface may ask a person for in one go, not a product
/// claim about forms. A page that needs more than this from a person in a
/// single step is one the errand should be handing over rather than driving.
pub const MAX_REQUESTED_FIELDS: u32 = 8;

/// Why a field-value fact was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FieldValueFactError {
    /// Empty or larger than the closed bound.
    InvalidRequestId,
    /// More answers than [`MAX_REQUESTED_FIELDS`], or more fields than one
    /// sheet has rows for.
    TooManyValues,
    /// A field's node identity was empty or longer than
    /// [`MAX_FIELD_NODE_ID_BYTES`].
    InvalidFieldNodeId,
    /// One field was named twice, or a companion repeated the line the
    /// request named.
    RepeatedFieldNodeId,
}

impl FieldValueFactError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::InvalidRequestId => "invalid_request_id",
            Self::TooManyValues => "too_many_values",
            Self::InvalidFieldNodeId => "invalid_field_node_id",
            Self::RepeatedFieldNodeId => "repeated_field_node_id",
        }
    }
}

/// The identity of one request to the person to fill a form in.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct FieldValueRequestId(String);

impl FieldValueRequestId {
    /// Accepts one non-empty bounded opaque identity.
    pub fn new(value: impl Into<String>) -> Result<Self, FieldValueFactError> {
        let value = value.into();
        if value.is_empty() || value.len() > MAX_FIELD_VALUE_REQUEST_ID_BYTES {
            return Err(FieldValueFactError::InvalidRequestId);
        }
        Ok(Self(value))
    }

    /// The opaque identity, for exact correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for FieldValueRequestId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// The identity of the request the `sequence`-th call of turn `ordinal` makes.
///
/// Derived from the same two numbers a handover's is, and for the same reason:
/// a turn ordinal only ever increases and a reply is frozen the moment it is
/// read, so the *n*-th call of the *m*-th turn happens exactly once, and a
/// replay reaches the same request rather than opening a second one. Neither
/// number is a position in a batch that could be re-planned.
///
/// Total: the format cannot produce an empty or over-long string.
pub fn field_value_request_id_for_call(ordinal: u64, sequence: u32) -> FieldValueRequestId {
    FieldValueRequestId(format!("turn-{ordinal}-values-{sequence}"))
}

/// What one of the person's answers is called, where the browser holds it.
///
/// The browser mints each answer into its own vault under this name, and this
/// process composes a proposal naming the same one. Neither sends the other a
/// name: both derive it from the request identity and the answer's position,
/// which they already agree on because the request identity is itself derived
/// (see [`field_value_request_id_for_call`]).
///
/// Deriving it rather than exchanging it is what keeps the count-only crossing
/// honest. Had the browser sent its names across, this process would hold a
/// list whose length is the number of values the person typed and whose
/// contents are opaque — which is the same fact the count carries, arrived at
/// by a route that has to be trusted not to encode anything else in the
/// strings. A derived name cannot carry what nobody wrote into it.
///
/// It is safe for a model to be near the *index* and never near this: there is
/// no argument shape in which a model can write one. A model names a position
/// as an integer ([`crate::tool::ArgumentValue::SuppliedValue`]), this process
/// bounds that position against what the person actually supplied, and only
/// then is a name derived. What the browser does with the name it derives is
/// still checked in full — the vault refuses a reference belonging to another
/// task, one that has expired, one already spent, and one whose class does not
/// match the field it is about to enter.
///
/// Total: the format cannot produce an empty string.
pub fn value_reference_for_supplied(request: &FieldValueRequestId, index: u32) -> String {
    format!("{}-value-{index}", request.as_str())
}

/// Distinct fields on one page, named by the observation's own identities.
///
/// Two lists have this shape, and both cross the seam in node identities and
/// nothing else (decision 0238). One goes out with a request: the other fields
/// on the page that only the person can supply, which the browser asks about
/// on the same sheet as the line the model named. The other comes back with
/// the answer: the field each held value was minted for, in position order,
/// which is what lets this process put value `i` into field `i` without a model
/// turn between the answer and the fills.
///
/// Neither is a value or is derived from one. A node identity is a fact this
/// process already held from the page it read before the person typed
/// anything, and which fields a list names is decided by which fields the
/// sheet showed — never by what went into them (decision 0063 section 4).
///
/// Bounded by the sheet: never more than [`MAX_REQUESTED_FIELDS`], every
/// identity non-empty and within [`MAX_FIELD_NODE_ID_BYTES`], and no field
/// twice. A list that breaks any of those is refused rather than repaired,
/// because a repaired list is a fill aimed at a field nobody named.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FieldNodeIds(Vec<SemanticNodeId>);

impl FieldNodeIds {
    /// No fields.
    pub const fn none() -> Self {
        Self(Vec::new())
    }

    /// Accepts a bounded list of distinct field identities, in the order
    /// given.
    pub fn new(ids: Vec<SemanticNodeId>) -> Result<Self, FieldValueFactError> {
        if ids.len() > MAX_REQUESTED_FIELDS as usize {
            return Err(FieldValueFactError::TooManyValues);
        }
        for (index, id) in ids.iter().enumerate() {
            if id.as_str().is_empty() || id.as_str().len() > MAX_FIELD_NODE_ID_BYTES {
                return Err(FieldValueFactError::InvalidFieldNodeId);
            }
            if ids.get(..index).is_some_and(|earlier| earlier.contains(id)) {
                return Err(FieldValueFactError::RepeatedFieldNodeId);
            }
        }
        Ok(Self(ids))
    }

    /// The fields a request asks about beside the one it names.
    ///
    /// One row fewer than [`Self::new`] admits, because the named line takes
    /// the first row of the sheet, and the named line itself is refused: a
    /// companion that repeated it would ask the person for one value twice.
    pub fn companions_of(
        named: &SemanticNodeId,
        ids: Vec<SemanticNodeId>,
    ) -> Result<Self, FieldValueFactError> {
        if ids.len() >= MAX_REQUESTED_FIELDS as usize {
            return Err(FieldValueFactError::TooManyValues);
        }
        if ids.contains(named) {
            return Err(FieldValueFactError::RepeatedFieldNodeId);
        }
        Self::new(ids)
    }

    /// The fields, in order.
    pub fn as_slice(&self) -> &[SemanticNodeId] {
        &self.0
    }

    /// How many fields. Never more than [`MAX_REQUESTED_FIELDS`].
    pub fn len(&self) -> u32 {
        u32::try_from(self.0.len()).unwrap_or(u32::MAX)
    }

    /// Whether the list names no field.
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    /// The field at `index`, or `None` past the end.
    pub fn get(&self, index: u32) -> Option<&SemanticNodeId> {
        self.0.get(usize::try_from(index).ok()?)
    }
}

/// Where the person's held values go (decision 0238).
///
/// The tab the request named, which is the tab every field the sheet showed
/// stands in, and the field each held value was minted for, by position. Held
/// beside the count for exactly as long as the count is, and never restored:
/// the journal does not record the fields, so a task rebuilt from it knows how
/// many values the person gave and not where they go, and the model is told
/// to fill them itself as it was before this existed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HeldValuePlacement {
    tab_id: TabId,
    fields: FieldNodeIds,
}

impl HeldValuePlacement {
    /// Binds the fields the browser reported to the tab the request named.
    pub const fn new(tab_id: TabId, fields: FieldNodeIds) -> Self {
        Self { tab_id, fields }
    }

    /// The tab the fields stand in.
    pub const fn tab_id(&self) -> &TabId {
        &self.tab_id
    }

    /// The field each held value was minted for, in position order.
    pub const fn fields(&self) -> &FieldNodeIds {
        &self.fields
    }
}

/// What came back when the person answered.
///
/// One number naming the contiguous prefix `[0, value)`. The browser stops
/// minting at the first unavailable position, so every index below this count
/// resolves and no index at or above it was minted by that answer. The
/// references themselves remain browser-owned and never enter this process.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SuppliedValueCount(u32);

impl SuppliedValueCount {
    /// Accepts a count within [`MAX_REQUESTED_FIELDS`].
    ///
    /// Zero is admitted and is not an error: a person who opened the sheet and
    /// filled nothing in has answered, and the assistant needs to learn that
    /// rather than wait for a surface that has already closed.
    pub const fn new(value: u32) -> Result<Self, FieldValueFactError> {
        if value > MAX_REQUESTED_FIELDS {
            return Err(FieldValueFactError::TooManyValues);
        }
        Ok(Self(value))
    }

    /// How many values the person supplied.
    pub const fn get(self) -> u32 {
        self.0
    }

    /// Whether the person supplied nothing at all.
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }
}

/// What became of one request for values, as the next move it implies.
///
/// A zero count is not one fact. The person may have closed the sheet; the
/// line named may take no value; the browser may have had nothing to draw, or
/// no surface to draw it on, or a page that moved underneath it. Every one of
/// those ends in `0`, and each wants a different next move, so a model given
/// only the number has to guess — and on the myAadhaar CAPTCHA it guessed
/// "ask again" eleven times over a picture that was merely below the fold
/// (decision 0215).
///
/// The members name the move rather than the browser's own clause. The
/// browser keeps its fourteen abandonment clauses and its capture's eleven
/// refusal clauses at full granularity in the device log, where a person
/// debugging wants them; what crosses is the instruction, because a clause is
/// a fact about somebody else's internals and a model handed one would have to
/// infer the instruction anyway.
///
/// Nothing here is chosen from anything on the page. The set is fixed at build
/// time and the choice is a function of the browser's own control flow, which
/// is what keeps decision 0088's count-only crossing content-free with an
/// enumeration beside the count.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FieldValueAskOutcome {
    /// The person filled the sheet in. The count is what they gave.
    Answered,
    /// The sheet was drawn and the person closed it without filling it in.
    Dismissed,
    /// The line the task named takes no value, so no sheet could be about it.
    NotAField,
    /// The challenge's picture is on the page and outside the part in view.
    ///
    /// The one member whose target was right. Nothing is wrong with the line
    /// the task named and nothing about asking again is wrong either — the
    /// page has to move first.
    ChallengeOffScreen,
    /// The browser could not build a sheet for that field for another reason.
    CannotBeShown,
    /// The page changed while the request was being prepared or answered.
    PageMoved,
    /// Nothing could draw a sheet at all.
    NoSurface,
}

/// The one browser-owned value set the next model turn may name by position.
///
/// Keeping the request identity beside the count is what makes a position
/// meaningful after the request has closed. Both facts are re-derived by
/// replaying the journalled request and supply commands; neither contains a
/// value, a reference chosen by the browser, or anything derived from the
/// person's bytes.
///
/// The outcome is the one thing here that a replay does not restore, and the
/// `Option` says so rather than defaulting. `TaskTransactionBatch` is
/// versioned by exact equality, so a member added to the durable format
/// refuses every batch already written, and an outcome is advice for one turn
/// rather than a fact about the task: a process that died and came back is
/// past the turn the advice was for. So it is `Some` on every live path and
/// `None` on every restored one, and the sentence for `None` says the reason
/// did not survive a restart instead of claiming the person answered.
///
/// The placement is the second such fact and follows the same rule for the
/// same reason (decision 0238): `Some` on a live answer whose request named a
/// tab, `None` on every restored one.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SuppliedFieldValues {
    request_id: FieldValueRequestId,
    count: SuppliedValueCount,
    outcome: Option<FieldValueAskOutcome>,
    placement: Option<HeldValuePlacement>,
}

impl SuppliedFieldValues {
    /// Binds an answered request to its content-free answer count.
    pub const fn new(
        request_id: FieldValueRequestId,
        count: SuppliedValueCount,
        outcome: Option<FieldValueAskOutcome>,
    ) -> Self {
        Self {
            request_id,
            count,
            outcome,
            placement: None,
        }
    }

    /// The same answer, knowing where each held value goes.
    #[must_use]
    pub fn with_placement(mut self, placement: Option<HeldValuePlacement>) -> Self {
        self.placement = placement;
        self
    }

    /// What became of the ask, or `None` when it was rebuilt from the journal.
    pub const fn outcome(&self) -> Option<FieldValueAskOutcome> {
        self.outcome
    }

    /// Where each held value goes, or `None` when this process does not know:
    /// rebuilt from the journal, or answered by a browser that did not say.
    pub const fn placement(&self) -> Option<&HeldValuePlacement> {
        self.placement.as_ref()
    }

    /// The browser/core-generated request identity used to derive references.
    pub const fn request_id(&self) -> &FieldValueRequestId {
        &self.request_id
    }

    /// How many zero-based positions the model may name.
    pub const fn count(&self) -> SuppliedValueCount {
        self.count
    }

    /// Whether this exact position was supplied by the person.
    pub const fn contains(&self, index: u32) -> bool {
        index < self.count.get()
    }
}

#[cfg(test)]
mod tests {
    use super::{
        field_value_request_id_for_call, value_reference_for_supplied, FieldNodeIds,
        FieldValueFactError, FieldValueRequestId, SuppliedValueCount, MAX_FIELD_NODE_ID_BYTES,
        MAX_FIELD_VALUE_REQUEST_ID_BYTES, MAX_REQUESTED_FIELDS,
    };
    use bip_types::identity::SemanticNodeId;

    #[test]
    fn a_request_identity_is_derived_so_a_replay_reaches_the_same_one() {
        let first = field_value_request_id_for_call(7, 2);
        assert_eq!(first, field_value_request_id_for_call(7, 2));
        assert_ne!(first, field_value_request_id_for_call(7, 3));
        assert_ne!(first, field_value_request_id_for_call(8, 2));
    }

    #[test]
    fn a_derived_identity_is_always_constructible() {
        let derived = field_value_request_id_for_call(u64::MAX, u32::MAX);
        assert!(!derived.as_str().is_empty());
        assert!(derived.as_str().len() <= MAX_FIELD_VALUE_REQUEST_ID_BYTES);
        assert!(FieldValueRequestId::new(derived.as_str()).is_ok());
    }

    #[test]
    fn an_identity_is_bounded_and_never_empty() {
        assert_eq!(
            FieldValueRequestId::new(""),
            Err(FieldValueFactError::InvalidRequestId)
        );
        let long = "v".repeat(MAX_FIELD_VALUE_REQUEST_ID_BYTES + 1);
        assert_eq!(
            FieldValueRequestId::new(long),
            Err(FieldValueFactError::InvalidRequestId)
        );
    }

    #[test]
    fn nobody_answering_is_an_answer_and_never_an_error() {
        // A person who opened the sheet and filled nothing in has answered.
        // Treating that as a refusal would leave the task waiting on a surface
        // that has already closed.
        let Ok(none) = SuppliedValueCount::new(0) else {
            unreachable!("zero is a count")
        };
        assert!(none.is_empty());
        assert_eq!(none.get(), 0);
    }

    #[test]
    fn a_count_past_the_bound_is_refused() {
        assert!(SuppliedValueCount::new(MAX_REQUESTED_FIELDS).is_ok());
        assert_eq!(
            SuppliedValueCount::new(MAX_REQUESTED_FIELDS + 1),
            Err(FieldValueFactError::TooManyValues)
        );
    }

    #[test]
    fn a_held_value_is_named_the_same_way_on_both_sides_of_the_crossing() {
        // The browser derives the name it mints under from these two facts and
        // this process derives the name it proposes from the same two, so the
        // test that matters is that the derivation is a function: same request
        // and same position, same name; anything else, a different one.
        let request = field_value_request_id_for_call(4, 1);
        let second = value_reference_for_supplied(&request, 1);
        assert_eq!(second, value_reference_for_supplied(&request, 1));
        assert_ne!(second, value_reference_for_supplied(&request, 0));
        assert_ne!(
            second,
            value_reference_for_supplied(&field_value_request_id_for_call(4, 2), 1)
        );
        assert_ne!(
            second,
            value_reference_for_supplied(&field_value_request_id_for_call(5, 1), 1)
        );
    }

    #[test]
    fn no_position_produces_a_name_that_is_not_one() {
        // A name is derived after the position has been bounded, but the
        // derivation is total anyway: a caller that skipped the bound gets a
        // usable name and a vault miss, never an empty string the browser
        // would have to interpret.
        let request = field_value_request_id_for_call(u64::MAX, u32::MAX);
        for index in [0, 1, MAX_REQUESTED_FIELDS, u32::MAX] {
            assert!(!value_reference_for_supplied(&request, index).is_empty());
        }
    }

    fn node_ids(count: usize) -> Vec<SemanticNodeId> {
        (0..count)
            .map(|index| SemanticNodeId::new(format!("field-{index}")))
            .collect()
    }

    #[test]
    fn a_list_of_fields_is_bounded_distinct_and_kept_in_order() {
        let Ok(fields) = FieldNodeIds::new(node_ids(MAX_REQUESTED_FIELDS as usize)) else {
            unreachable!("one field per supplied value is admitted")
        };
        assert_eq!(fields.len(), MAX_REQUESTED_FIELDS);
        assert_eq!(fields.get(0), Some(&SemanticNodeId::new("field-0")));
        assert_eq!(fields.get(MAX_REQUESTED_FIELDS), None);
        assert_eq!(
            FieldNodeIds::new(node_ids(MAX_REQUESTED_FIELDS as usize + 1)),
            Err(FieldValueFactError::TooManyValues)
        );
        let mut repeated = node_ids(3);
        repeated.push(SemanticNodeId::new("field-1"));
        assert_eq!(
            FieldNodeIds::new(repeated),
            Err(FieldValueFactError::RepeatedFieldNodeId)
        );
        for bad in [String::new(), "n".repeat(MAX_FIELD_NODE_ID_BYTES + 1)] {
            assert_eq!(
                FieldNodeIds::new(vec![SemanticNodeId::new(bad)]),
                Err(FieldValueFactError::InvalidFieldNodeId)
            );
        }
    }

    #[test]
    fn a_companion_never_repeats_the_named_line_and_leaves_it_a_row() {
        let named = SemanticNodeId::new("named");
        assert!(FieldNodeIds::companions_of(&named, Vec::new()).is_ok_and(|none| none.is_empty()));
        assert_eq!(
            FieldNodeIds::companions_of(&named, vec![SemanticNodeId::new("named")]),
            Err(FieldValueFactError::RepeatedFieldNodeId)
        );
        let most = MAX_REQUESTED_FIELDS as usize - 1;
        assert!(FieldNodeIds::companions_of(&named, node_ids(most)).is_ok());
        assert_eq!(
            FieldNodeIds::companions_of(&named, node_ids(most + 1)),
            Err(FieldValueFactError::TooManyValues)
        );
    }

    #[test]
    fn every_error_shape_has_a_compiled_in_label() {
        for error in [
            FieldValueFactError::InvalidRequestId,
            FieldValueFactError::TooManyValues,
            FieldValueFactError::InvalidFieldNodeId,
            FieldValueFactError::RepeatedFieldNodeId,
        ] {
            assert!(!error.label().is_empty());
        }
    }
}
