// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The numbers the model designates a node by, and what each one is bound to.
//!
//! A model never receives a `SemanticNodeId`, a frame identity, a page epoch or
//! a graph revision (decision 0053 section 3). It receives a number. This
//! module owns those numbers: it issues them, records the world each was issued
//! against, and answers what one refers to — or that it refers to nothing.
//!
//! The whole of the design is in one property. **A number is issued once, means
//! one node, and is never reused.** The obvious alternative — numbering each
//! fresh view from one — is wrong in a way that stays invisible until it is
//! expensive: a model that decides to act on item 7, and issues the call after
//! the page has moved and a new view has been built, would find that `7` now
//! names a different node and that *every precondition passes*. Right tab,
//! right frame, often the same epoch, node present, visible, enabled, role
//! permits it. The action lands on the wrong element with a full set of
//! satisfied checks behind it.
//!
//! Task-global numbering turns that same late reference into "I do not know
//! that number", which the model can act on.
//!
//! This repository has already paid for the general form of the mistake once,
//! in the delivery plane: an effect identifier derived from an artifact's
//! *position* in a plan collided when the artifact ahead of it finished, and
//! the collision was silent in both directions. An identifier must name what it
//! is about, never where it landed in the batch that produced it. A per-view
//! ordinal is that hazard wearing a different noun.

use std::{collections::VecDeque, sync::Arc};

use bip_types::identity::{NodeHandle, SemanticNodeId};
use bip_types::snapshot::Sensitivity;

/// How many bindings one task keeps.
///
/// Two full observations' worth of the Core Service task observation node
/// ceiling (1500, decision 0144). That is the smallest bound that lets a model
/// act on a handle it was given in an earlier turn — the case decision 0053
/// asks for by name — after a page that actually filled the ceiling. At 256,
/// a 1500-node results page would print handles for every result and then
/// forget the ones at the top of the page, which are the ones worth opening.
///
/// Forgetting is safe in a way that remembering wrongly is not. An evicted
/// handle answers exactly as an unissued one does, so the cost of a table too
/// small is a wasted turn, and the cost of one that recycled a number would be
/// an action on a node nobody chose.
pub const MAX_RETAINED_BINDINGS: usize = 3_000;

/// A number the model uses to designate one node.
///
/// A `u32` because a task that has offered four billion nodes to a model has a
/// different problem, and because the value is printed into a page projection
/// where every byte is paid for twice — once going out and once in the reply.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ModelHandle(u32);

impl ModelHandle {
    /// The number as the model sees it.
    pub const fn value(self) -> u32 {
        self.0
    }
}

/// What a person's values could go into on the line a number was printed on
/// (decisions 0192 and 0195).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ValueTarget {
    /// Nothing: a heading, a link, a button, or a field nobody can type into.
    #[default]
    None,
    /// A block that may be a form: fields name it as theirs, or it is a region
    /// or a node of no known kind. `user.request_values` may name it.
    Container,
    /// A field that can set text. `user.request_values` and
    /// `browser.form.fill` may name it.
    Field,
}

/// Whether a value of this class is one only the person can supply.
///
/// An identity number, a one-time code, and the answer to a challenge: the
/// three classes `user.request_values` exists for (decisions 0088 and 0234).
/// A password or a card number is not one of them — a sign-in is handed over,
/// and a payment is never Taffy's to supply. The one definition: the snapshot
/// footer that tells the model which lines these are and the request that asks
/// the person for them together (decision 0238) both read it here.
pub const fn only_the_person_supplies(sensitivity: Sensitivity) -> bool {
    matches!(
        sensitivity,
        Sensitivity::Identity | Sensitivity::OneTimeCode | Sensitivity::ChallengeResponse
    )
}

/// One issued number and the six facts frozen when it was issued.
///
/// The six are [`NodeHandle`]'s own — tab, frame, page epoch, graph revision,
/// node identifier and expected origin — because they are the six the stale
/// node algorithm already checks. Freezing them at issue is what makes that
/// check a comparison rather than an inference: the handle carries the world it
/// was issued against.
#[derive(Clone, Debug, PartialEq)]
pub struct HandleBinding {
    handle: ModelHandle,
    node: NodeHandle,
    value_target: ValueTarget,
    person_only: bool,
    challenge: bool,
    opens_as_link: bool,
}

impl HandleBinding {
    /// The number the model was given.
    pub const fn handle(&self) -> ModelHandle {
        self.handle
    }

    /// The node this number names, with the world it was issued against.
    pub const fn node(&self) -> &NodeHandle {
        &self.node
    }

    /// What a person's values could go into on the line this number was
    /// printed on.
    pub const fn value_target(&self) -> ValueTarget {
        self.value_target
    }

    /// Whether `user.request_values` may name this line: a form, or a field
    /// that can take text.
    pub fn takes_values(&self) -> bool {
        self.value_target != ValueTarget::None
    }

    /// Whether `browser.form.fill` may name this line: a field that can take
    /// text, and not the form around it.
    pub fn sets_text(&self) -> bool {
        self.value_target == ValueTarget::Field
    }

    /// Whether this line takes a value only the person can supply: it takes
    /// values, and its class is one [`only_the_person_supplies`] names.
    pub const fn only_the_person_supplies(&self) -> bool {
        self.person_only
    }

    /// Whether this line is where a challenge's answer goes: a field whose
    /// class is [`Sensitivity::ChallengeResponse`]. The sheet can ask for it
    /// only while the picture beside it is in view (decision 0240).
    pub const fn is_challenge_answer(&self) -> bool {
        self.challenge
    }

    /// Whether `browser.link.open` may name this line: the reading printed it
    /// as a link that leads somewhere ("this site" or "another site"). A
    /// number issued without a rendered line says yes, and the browser's own
    /// link table decides (decision 0241).
    pub const fn opens_as_link(&self) -> bool {
        self.opens_as_link
    }
}

/// What a reading says about a line when its number is issued.
#[derive(Clone, Copy)]
struct IssueMarks {
    person_only: bool,
    challenge: bool,
    opens_as_link: bool,
}

/// Every handle one task has issued, and what the retained ones point at.
///
/// The counter and the retained bindings are deliberately separate. The counter
/// only ever advances, so a number cannot come back around; the bindings are
/// bounded, so a long task cannot grow this without limit. Dropping a binding
/// therefore loses the answer and never the guarantee.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct HandleTable {
    next: u32,
    // A composed turn and the live page both need the same accepted table.
    // Sharing that immutable snapshot makes the final commit constant-time;
    // the next preview pays for exactly one bounded copy when it first issues
    // a new handle instead of cloning all bindings both before and after every
    // model request.
    retained: Arc<VecDeque<HandleBinding>>,
}

impl HandleTable {
    /// An empty table, having issued nothing.
    pub fn new() -> Self {
        Self {
            next: 0,
            retained: Arc::new(VecDeque::new()),
        }
    }

    /// Issues the next number for `node`, or `None` once the counter is spent.
    ///
    /// Refusing at exhaustion rather than wrapping is the same rule
    /// [`crate::ids`] follows and for the same reason: a repeated number would
    /// let one node impersonate another, and every check downstream would agree
    /// with the impersonation.
    ///
    /// A node offered again in a later view receives a *new* number, even when
    /// it is the same element of the same page. That is not an oversight to be
    /// optimised away later. A later view carries a later graph revision, so the
    /// facts frozen here genuinely differ, and a node that survived a mutation
    /// is indistinguishable from a node that replaced one. Reusing the earlier
    /// number would be a guess with an action behind it.
    pub fn issue(&mut self, node: NodeHandle) -> Option<ModelHandle> {
        self.issue_marked(node, ValueTarget::None)
    }

    /// [`Self::issue`], recording what a person's values could go into on
    /// the line.
    ///
    /// The mark is frozen with the number for the same reason the six facts
    /// are: a model may name a number from an earlier reading of the same
    /// document (decision 0188), and whether that line was a field is a fact
    /// about the reading it came from.
    pub fn issue_marked(
        &mut self,
        node: NodeHandle,
        value_target: ValueTarget,
    ) -> Option<ModelHandle> {
        self.issue_with(
            node,
            value_target,
            IssueMarks {
                person_only: false,
                challenge: false,
                opens_as_link: true,
            },
        )
    }

    /// [`Self::issue_marked`], recording as well whether the line takes a
    /// value only the person can supply (decision 0238).
    ///
    /// Frozen with the number for the reason the mark is: whether a line was
    /// an identity field is a fact about the reading it was printed from. A
    /// line that takes no value is never one, whatever its class, because
    /// nothing could be asked for there.
    pub fn issue_classified(
        &mut self,
        node: NodeHandle,
        value_target: ValueTarget,
        sensitivity: Sensitivity,
    ) -> Option<ModelHandle> {
        self.issue_rendered(node, value_target, sensitivity, true)
    }

    /// [`Self::issue_classified`], recording as well whether the reading
    /// printed the line as a link that leads somewhere (decision 0241).
    ///
    /// Frozen with the number for the reason the class is: whether a line was
    /// a link with an address is a fact about the reading it was printed
    /// from, and a model may name a number from an earlier reading.
    pub fn issue_rendered(
        &mut self,
        node: NodeHandle,
        value_target: ValueTarget,
        sensitivity: Sensitivity,
        opens_as_link: bool,
    ) -> Option<ModelHandle> {
        let person_only =
            value_target != ValueTarget::None && only_the_person_supplies(sensitivity);
        let challenge =
            value_target == ValueTarget::Field && sensitivity == Sensitivity::ChallengeResponse;
        self.issue_with(
            node,
            value_target,
            IssueMarks {
                person_only,
                challenge,
                opens_as_link,
            },
        )
    }

    fn issue_with(
        &mut self,
        node: NodeHandle,
        value_target: ValueTarget,
        marks: IssueMarks,
    ) -> Option<ModelHandle> {
        let handle = ModelHandle(self.next);
        self.next = self.next.checked_add(1)?;
        let retained = Arc::make_mut(&mut self.retained);
        if retained.len() >= MAX_RETAINED_BINDINGS {
            // Oldest first. `pop_front` is constant time: after the retained
            // window fills, issuing a node must not shift every binding that
            // remains. The model-visible number still only advances and is
            // never recycled.
            retained.pop_front();
        }
        retained.push_back(HandleBinding {
            handle,
            node,
            value_target,
            person_only: marks.person_only,
            challenge: marks.challenge,
            opens_as_link: marks.opens_as_link,
        });
        Some(handle)
    }

    /// What `handle` names, or `None`.
    ///
    /// `None` is one answer covering three situations — never issued, issued
    /// and evicted, and a number beyond anything this task has reached — and
    /// they are deliberately not distinguished. A caller can do exactly one
    /// thing with any of them, which is observe again; and telling a model
    /// whether a number it invented had ever been issued would answer a
    /// question about the task's history that it has no business asking.
    pub fn resolve(&self, handle: ModelHandle) -> Option<&NodeHandle> {
        self.binding(handle).map(HandleBinding::node)
    }

    /// The whole binding `handle` names, or `None`, under the same contract
    /// as [`Self::resolve`].
    pub fn binding(&self, handle: ModelHandle) -> Option<&HandleBinding> {
        // Retained handles are a contiguous suffix of the monotonic issue
        // sequence. That makes the model's number an index into the deque,
        // without a map allocation and without a linear scan on every tool
        // call. A number before the window or beyond its end fails closed.
        let first = self.retained.front()?.handle.0;
        let offset = usize::try_from(handle.0.checked_sub(first)?).ok()?;
        self.retained.get(offset).filter(|binding| {
            // Keep the equality check even though contiguity makes it
            // redundant today. If issuance ever gains a refused gap, lookup
            // remains fail-closed instead of silently rebinding the number.
            binding.handle == handle
        })
    }

    /// What the bare number `value` names, or `None`.
    ///
    /// The same three-situations-one-answer contract as [`Self::resolve`].
    /// It exists because a number arriving in a model's reply is a `u32` and
    /// nothing else: there is deliberately no way to mint a [`ModelHandle`]
    /// from one, because the table is the only issuer, so a caller holding a
    /// reply has to ask the table rather than construct the question.
    pub fn resolve_value(&self, value: u32) -> Option<&NodeHandle> {
        self.resolve(ModelHandle(value))
    }

    /// Whether the bare number `value` names a line that takes a person's
    /// values. An unknown number does not.
    pub fn takes_values(&self, value: u32) -> bool {
        self.binding(ModelHandle(value))
            .is_some_and(HandleBinding::takes_values)
    }

    /// Whether the bare number `value` names a field that can take text. An
    /// unknown number does not.
    pub fn sets_text(&self, value: u32) -> bool {
        self.binding(ModelHandle(value))
            .is_some_and(HandleBinding::sets_text)
    }

    /// The other fields of `value`'s document that only the person can
    /// supply, in the order their numbers were first issued, at most `limit`
    /// of them (decision 0238).
    ///
    /// Empty unless `value` itself names such a field, or a block. A field
    /// named on its own was the only one the sheet asked about, and on the
    /// myAadhaar form — whose fields stand in no form element — that asked the
    /// person for their identity number and left the CAPTCHA beside it for
    /// another turn. A block brings every such field of its document: the
    /// browser expands a form itself, from the fields that name it as theirs,
    /// and puts these on the sheet only when the block turns out to have none
    /// (decision 0243).
    ///
    /// "The same document" is the named node's own tab, frame and page epoch,
    /// so a line from a page the tab has left is never one. Every reading of
    /// the document counts, not only the one the named number came from: a
    /// CAPTCHA drawn after the first reading is on the page all the same. A
    /// node read more than once keeps the place of its first number, and none
    /// is named twice. Which of them still needs a person is the browser's
    /// judgement at dispatch, never this table's.
    pub fn person_only_companions(&self, value: u32, limit: usize) -> Vec<SemanticNodeId> {
        let Some(named) = self.binding(ModelHandle(value)).filter(|binding| {
            binding.value_target() == ValueTarget::Container
                || (binding.sets_text() && binding.only_the_person_supplies())
        }) else {
            return Vec::new();
        };
        let named = named.node();
        let mut companions: Vec<SemanticNodeId> = Vec::new();
        for binding in self.retained.iter() {
            if companions.len() >= limit {
                break;
            }
            let node = binding.node();
            let same_document = node.tab_id == named.tab_id
                && node.frame_id == named.frame_id
                && node.page_epoch == named.page_epoch;
            if same_document
                && binding.sets_text()
                && binding.only_the_person_supplies()
                && node.node_id != named.node_id
                && !companions.contains(&node.node_id)
            {
                companions.push(node.node_id.clone());
            }
        }
        companions
    }

    /// The challenge answer among the field `value` names and `companions`,
    /// in that order, when there is one (decision 0240).
    ///
    /// The sheet copies a challenge's picture from the part of the page in
    /// view and leaves the line off when it cannot, so this is the node the
    /// page has to show before the ask. Only a line of the named field's own
    /// document counts, as for [`Self::person_only_companions`].
    pub fn challenge_answer_among(
        &self,
        value: u32,
        companions: &[SemanticNodeId],
    ) -> Option<&NodeHandle> {
        let named = self.binding(ModelHandle(value))?;
        if named.challenge {
            return Some(named.node());
        }
        let document = named.node();
        self.retained
            .iter()
            .find(|binding| {
                let node = binding.node();
                binding.challenge
                    && node.tab_id == document.tab_id
                    && node.frame_id == document.frame_id
                    && node.page_epoch == document.page_epoch
                    && companions.contains(&node.node_id)
            })
            .map(HandleBinding::node)
    }

    /// Whether the bare number `value` names a line the reading printed as a
    /// link that leads somewhere. An unknown number does not.
    pub fn opens_as_link(&self, value: u32) -> bool {
        self.binding(ModelHandle(value))
            .is_some_and(HandleBinding::opens_as_link)
    }

    /// How many numbers this task has issued, ever.
    pub const fn issued(&self) -> u32 {
        self.next
    }

    /// How many bindings are still answerable.
    pub fn retained(&self) -> usize {
        self.retained.len()
    }

    /// The retained bindings, oldest first.
    pub fn bindings(&self) -> impl Iterator<Item = &HandleBinding> {
        self.retained.iter()
    }
}

#[cfg(test)]
mod tests {
    use super::{HandleTable, ModelHandle, ValueTarget, MAX_RETAINED_BINDINGS};
    use bip_types::identity::{
        FrameId, GraphRevision, NodeHandle, Origin, OriginKind, PageEpoch, SemanticNodeId, TabId,
    };
    use bip_types::snapshot::Sensitivity;

    fn origin() -> Origin {
        Origin {
            kind: OriginKind::Tuple,
            serialization: Some("https://example.test".to_owned()),
            opaque_id: None,
        }
    }

    fn node(node_id: &str, revision: u64) -> NodeHandle {
        NodeHandle::new(
            TabId::new("tab-1"),
            FrameId::new("frame-1"),
            PageEpoch::new("epoch-1"),
            GraphRevision(revision),
            SemanticNodeId::new(node_id),
            origin(),
        )
    }

    #[test]
    fn a_number_is_issued_once_and_names_what_it_was_issued_for() {
        let mut table = HandleTable::new();
        let first = table.issue(node("n-1", 4)).expect("a handle");
        let second = table.issue(node("n-2", 4)).expect("a handle");

        assert_ne!(first, second);
        assert_eq!(
            table.resolve(first).map(|node| &node.node_id),
            Some(&SemanticNodeId::new("n-1"))
        );
        assert_eq!(
            table.resolve(second).map(|node| &node.node_id),
            Some(&SemanticNodeId::new("n-2"))
        );
        assert_eq!(table.issued(), 2);
    }

    #[test]
    fn accepted_snapshots_share_storage_until_a_new_handle_is_issued() {
        let mut accepted = HandleTable::new();
        let first = accepted.issue(node("n-1", 4)).expect("a handle");
        let mut next_preview = accepted.clone();
        assert!(std::sync::Arc::ptr_eq(
            &accepted.retained,
            &next_preview.retained
        ));

        let second = next_preview.issue(node("n-2", 5)).expect("a handle");

        assert!(!std::sync::Arc::ptr_eq(
            &accepted.retained,
            &next_preview.retained
        ));
        assert_eq!(accepted.retained(), 1);
        assert_eq!(next_preview.retained(), 2);
        assert!(accepted.resolve(first).is_some());
        assert!(accepted.resolve(second).is_none());
        assert!(next_preview.resolve(first).is_some());
        assert!(next_preview.resolve(second).is_some());
    }

    #[test]
    fn a_later_view_renumbers_rather_than_remapping() {
        // The defect this whole module exists to prevent. The model is offered
        // one node, the page moves, and a fresh view offers a different node in
        // the same position. The first number must still name the first node.
        let mut table = HandleTable::new();
        let before = table.issue(node("n-old", 4)).expect("a handle");
        let after = table.issue(node("n-new", 5)).expect("a handle");

        assert_ne!(before, after);
        assert_eq!(
            table.resolve(before).map(|node| &node.node_id),
            Some(&SemanticNodeId::new("n-old"))
        );
        assert_eq!(
            table.resolve(before).map(|node| node.graph_revision),
            Some(GraphRevision(4))
        );
    }

    #[test]
    fn the_same_node_offered_again_receives_a_new_number() {
        let mut table = HandleTable::new();
        let first = table.issue(node("n-1", 4)).expect("a handle");
        let again = table.issue(node("n-1", 5)).expect("a handle");

        assert_ne!(first, again);
        assert_eq!(
            table.resolve(first).map(|node| node.graph_revision),
            Some(GraphRevision(4))
        );
        assert_eq!(
            table.resolve(again).map(|node| node.graph_revision),
            Some(GraphRevision(5))
        );
    }

    #[test]
    fn a_number_that_was_never_issued_names_nothing() {
        let table = HandleTable::new();
        assert!(table.resolve(ModelHandle(0)).is_none());
        assert!(table.resolve(ModelHandle(u32::MAX)).is_none());
    }

    #[test]
    fn eviction_forgets_the_answer_and_never_recycles_the_number() {
        let mut table = HandleTable::new();
        let mut issued = Vec::new();
        for index in 0..MAX_RETAINED_BINDINGS + 8 {
            let handle = table
                .issue(node(&format!("n-{index}"), 4))
                .expect("a handle");
            issued.push(handle);
        }

        assert_eq!(table.retained(), MAX_RETAINED_BINDINGS);
        assert_eq!(table.issued() as usize, MAX_RETAINED_BINDINGS + 8);

        // The oldest eight are gone, and they answer exactly as an unissued
        // number does rather than as somebody else's node.
        for handle in issued.iter().take(8) {
            assert!(table.resolve(*handle).is_none());
        }
        // Every number is still distinct, so nothing was recycled into the gap.
        let unique: std::collections::BTreeSet<_> = issued.iter().collect();
        assert_eq!(unique.len(), issued.len());
        // And what survived still names what it was issued for.
        let newest = issued.last().copied().expect("a handle");
        assert_eq!(
            table.resolve(newest).map(|node| &node.node_id),
            Some(&SemanticNodeId::new(format!(
                "n-{}",
                MAX_RETAINED_BINDINGS + 7
            )))
        );
    }

    fn node_in(node_id: &str, frame: &str, epoch: &str) -> NodeHandle {
        NodeHandle::new(
            TabId::new("tab-1"),
            FrameId::new(frame),
            PageEpoch::new(epoch),
            GraphRevision(4),
            SemanticNodeId::new(node_id),
            origin(),
        )
    }

    fn person_only(table: &mut HandleTable, node: NodeHandle) -> ModelHandle {
        table
            .issue_classified(node, ValueTarget::Field, Sensitivity::OneTimeCode)
            .expect("a handle")
    }

    fn ids(nodes: &[SemanticNodeId]) -> Vec<&str> {
        nodes.iter().map(SemanticNodeId::as_str).collect()
    }

    #[test]
    fn a_field_only_the_person_supplies_names_the_others_in_its_document() {
        let mut table = HandleTable::new();
        let left = person_only(&mut table, node_in("left-page", "frame-1", "epoch-0"));
        let named = person_only(&mut table, node_in("id", "frame-1", "epoch-1"));
        person_only(&mut table, node_in("in-a-frame", "frame-2", "epoch-1"));
        let open = table
            .issue_classified(
                node_in("name", "frame-1", "epoch-1"),
                ValueTarget::Field,
                Sensitivity::NotSensitive,
            )
            .expect("a handle");
        let region = table
            .issue_classified(
                node_in("region", "frame-1", "epoch-1"),
                ValueTarget::Container,
                Sensitivity::Identity,
            )
            .expect("a handle");
        person_only(&mut table, node_in("captcha", "frame-1", "epoch-1"));
        // Read again: it keeps its first place and is named once.
        person_only(&mut table, node_in("id", "frame-1", "epoch-1"));
        person_only(&mut table, node_in("captcha", "frame-1", "epoch-1"));
        person_only(&mut table, node_in("otp", "frame-1", "epoch-1"));

        assert_eq!(
            ids(&table.person_only_companions(named.value(), 7)),
            ["captcha", "otp"]
        );
        assert_eq!(
            ids(&table.person_only_companions(named.value(), 1)),
            ["captcha"]
        );
        // A block is not a field of its own, and brings all of them: the
        // browser asks about these only when it has no fields itself.
        assert_eq!(
            ids(&table.person_only_companions(region.value(), 7)),
            ["id", "captcha", "otp"]
        );
        // A page the tab has left is its own document.
        assert!(table.person_only_companions(left.value(), 7).is_empty());
        // A field anybody could fill, or a number never issued, brings none.
        assert!(table.person_only_companions(open.value(), 7).is_empty());
        assert!(table.person_only_companions(u32::MAX, 7).is_empty());
    }

    #[test]
    fn an_exhausted_counter_refuses_rather_than_wrapping() {
        let mut table = HandleTable::new();
        table.next = u32::MAX;
        assert!(table.issue(node("n-last", 4)).is_none());
        assert_eq!(table.issued(), u32::MAX);
    }
}
