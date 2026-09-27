// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Identity and lifetime rules for protocol handles (specification section 5).
//!
//! The generated types in [`crate::generated::identity`] carry the shape of an
//! identifier. This module carries the rules a schema cannot state: how a
//! handle is built, which identifiers may be ordered, and how a semantic node
//! identifier is allocated so it is never reused.
//!
//! # Opacity
//!
//! Every identifier here is an opaque domain value (section 5.1). It is not a
//! database row number, a Chromium pointer, a renderer routing identifier, a
//! URL, a title, an accessible name, a CSS selector, or an array index, and a
//! consumer must not parse one for structure. Construct with `new` and read
//! with `as_str`; the tuple field exists only so the generated serde
//! implementation can be transparent.
//!
//! # Ordering
//!
//! Ordering is granted by hand, one identifier at a time, and only where a
//! stable order is genuinely defined.
//!
//! - The container identifiers — [`ProfileId`], [`BrowserWindowId`],
//!   [`TabId`], [`FrameId`], [`SemanticNodeId`] — implement [`Ord`] over their
//!   opaque bytes, which is total, stable, and deterministic. That makes them
//!   usable as keys in an ordered collection whose iteration order must not
//!   vary between runs. It is not creation order, not hierarchy, and not
//!   anything a consumer may reason about. The [`DeterministicOrder`] marker
//!   records exactly which identifiers carry this and what it does not mean.
//!
//! - [`PageEpoch`] implements no ordering. Section 5.2 requires code to compare
//!   epochs for equality and forbids inferring ordering across frames or
//!   sessions, so the type offers no way to do it: no [`Ord`], no
//!   [`PartialOrd`], and no comparison helper. An epoch answers "is this handle
//!   still about the document I observed?" and refuses to answer anything else.
//!   Absence is the enforcement, so a reviewer never has to catch the misuse.
//!
//! - [`GraphRevision`] is monotonic inside one active epoch and meaningless
//!   across epochs or frames (section 5.3), so it also implements no ordering
//!   on its own. Pair it with the scope it was observed in — see
//!   [`ScopedGraphRevision`], which implements [`PartialOrd`] and returns
//!   `None` for two revisions from different scopes.
//!
//! # Copy
//!
//! The counted values ([`GraphRevision`], [`EventSequence`], [`Count`],
//! [`ByteCount`], [`DurationMillis`], [`MonotonicMillis`]) are `Copy`. The
//! opaque string identifiers are not, because copying them is not cheap; they
//! are `Clone`.

use core::cmp::Ordering;
use core::fmt;

pub use crate::generated::identity::ProtocolVersion as WireProtocolVersion;
pub use crate::generated::identity::{
    ActionId, ApprovalReceiptReference, BrowserWindowId, ByteCount, CapabilityReference, CommandId,
    ContentDigest, Count, DigestAlgorithm, DispatchId, DocumentLifecycleState, DurationMillis,
    EventSequence, FrameId, GraphRevision, MonotonicMillis, NodeHandle, Origin, OriginKind,
    OriginMetadata, PageEpoch, ProfileId, RequestId, SemanticNodeId, SensitivityPolicyId,
    SkillVersionId, SnapshotId, SubscriptionId, TabId, TaskId,
};

/// An opaque identifier whose ordering is total, stable, and free of protocol
/// meaning.
///
/// Implementing this says one thing only: sorting values of this type gives the
/// same sequence on every run, so an ordered collection keyed by it iterates
/// deterministically. It does not say that a smaller value was created first,
/// sits higher in the identity hierarchy, or belongs to the same session as a
/// larger one.
///
/// [`PageEpoch`] and [`GraphRevision`] are deliberately absent, and cannot be
/// added without first implementing [`Ord`] for them — which sections 5.2 and
/// 5.3 forbid.
pub trait DeterministicOrder: Ord {}

macro_rules! opaque_string_id {
    ($($ty:ident),+ $(,)?) => { $(
        impl $ty {
            /// Wraps an opaque identifier minted by the browser broker.
            ///
            /// The value is stored as given. Nothing in this crate parses it,
            /// and no consumer may infer structure, order, or provenance from
            /// its bytes.
            pub fn new(value: impl Into<String>) -> Self {
                Self(value.into())
            }

            /// The opaque value, for transport, logging under policy, and
            /// equality only.
            pub fn as_str(&self) -> &str {
                &self.0
            }
        }

        impl fmt::Display for $ty {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                formatter.write_str(&self.0)
            }
        }
    )+ };
}

macro_rules! deterministic_order {
    ($($ty:ident),+ $(,)?) => { $(
        impl Ord for $ty {
            /// Byte order of the opaque value. See [`DeterministicOrder`] for
            /// what this order does and does not mean.
            fn cmp(&self, other: &Self) -> Ordering {
                self.0.cmp(&other.0)
            }
        }

        impl PartialOrd for $ty {
            fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
                Some(self.cmp(other))
            }
        }

        impl DeterministicOrder for $ty {}
    )+ };
}

opaque_string_id!(
    ActionId,
    ApprovalReceiptReference,
    BrowserWindowId,
    CapabilityReference,
    CommandId,
    DispatchId,
    FrameId,
    PageEpoch,
    ProfileId,
    RequestId,
    SemanticNodeId,
    SensitivityPolicyId,
    SkillVersionId,
    SnapshotId,
    SubscriptionId,
    TabId,
    TaskId,
);

// The identity hierarchy of section 5.1, minus the two lifetime values that
// section 5.2 and section 5.3 forbid ordering.
deterministic_order!(ProfileId, BrowserWindowId, TabId, FrameId, SemanticNodeId);

/// The namespace inside which a [`SemanticNodeId`] is unique and never reused.
///
/// Section 5.4 scopes node identity to one `FrameId` plus one `PageEpoch`.
/// Removing a node permanently retires its identifier inside that scope, and a
/// replacement element receives a new identifier even when its selector, text,
/// role, and bounds are unchanged. Two identical [`SemanticNodeId`] values from
/// different scopes describe unrelated nodes, which is why every comparison in
/// this module carries the scope with it.
///
/// The type is deliberately not orderable: it contains a [`PageEpoch`].
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct NodeIdScope {
    frame_id: FrameId,
    page_epoch: PageEpoch,
}

impl NodeIdScope {
    /// Builds the scope a node identifier is unique within.
    pub fn new(frame_id: FrameId, page_epoch: PageEpoch) -> Self {
        Self {
            frame_id,
            page_epoch,
        }
    }

    /// The frame this scope belongs to.
    pub fn frame_id(&self) -> &FrameId {
        &self.frame_id
    }

    /// The page epoch this scope belongs to.
    pub fn page_epoch(&self) -> &PageEpoch {
        &self.page_epoch
    }
}

/// A [`GraphRevision`] together with the scope it was observed in.
///
/// Section 5.3 makes a revision monotonic within one active page epoch and
/// meaningless outside it. This pairing is what makes the rule executable: two
/// scoped revisions compare when their scopes are equal and are incomparable
/// otherwise, so [`PartialOrd::partial_cmp`] returns `None` rather than an
/// answer nobody may act on. The type implements no [`Ord`], because no total
/// order over revisions from different documents exists.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct ScopedGraphRevision {
    scope: NodeIdScope,
    revision: GraphRevision,
}

impl ScopedGraphRevision {
    /// Binds a revision to the scope it was observed in.
    pub fn new(scope: NodeIdScope, revision: GraphRevision) -> Self {
        Self { scope, revision }
    }

    /// The scope this revision was observed in.
    pub fn scope(&self) -> &NodeIdScope {
        &self.scope
    }

    /// The revision value.
    pub fn revision(&self) -> GraphRevision {
        self.revision
    }

    /// Whether this observation satisfies an action's freshness requirement
    /// (section 5.5).
    ///
    /// It does only when both readings come from the same frame and page epoch
    /// and this one is not older. A cross-scope comparison is never satisfied,
    /// because there is nothing to compare.
    pub fn satisfies(&self, required: &Self) -> bool {
        matches!(
            self.partial_cmp(required),
            Some(Ordering::Equal | Ordering::Greater)
        )
    }
}

impl PartialOrd for ScopedGraphRevision {
    /// `None` when the two revisions come from different frames or page epochs.
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        if self.scope == other.scope {
            Some(self.revision.0.cmp(&other.revision.0))
        } else {
            None
        }
    }
}

impl NodeHandle {
    /// Builds the complete reference to one observed semantic node.
    ///
    /// Every field of section 5.4's handle is a parameter, so a handle cannot
    /// be assembled from a partial observation: there is no default epoch, no
    /// inferred origin, and no builder that can be finished early. A caller who
    /// has lost one of these values has lost the handle and must observe again.
    pub fn new(
        tab_id: TabId,
        frame_id: FrameId,
        page_epoch: PageEpoch,
        graph_revision: GraphRevision,
        node_id: SemanticNodeId,
        expected_origin: Origin,
    ) -> Self {
        Self {
            tab_id,
            frame_id,
            page_epoch,
            graph_revision,
            node_id,
            expected_origin,
        }
    }

    /// The frame and page epoch this handle's node identifier is unique within.
    pub fn scope(&self) -> NodeIdScope {
        NodeIdScope::new(self.frame_id.clone(), self.page_epoch.clone())
    }

    /// This handle's revision, bound to the scope it was observed in.
    pub fn scoped_revision(&self) -> ScopedGraphRevision {
        ScopedGraphRevision::new(self.scope(), self.graph_revision)
    }

    /// Whether both handles describe the same document instance.
    ///
    /// Equality of tab, frame, and page epoch, in that order. Ordering is never
    /// consulted (section 5.2).
    pub fn is_same_document(&self, other: &Self) -> bool {
        self.tab_id == other.tab_id
            && self.frame_id == other.frame_id
            && self.page_epoch == other.page_epoch
    }

    /// Whether both handles name the same node of the same document.
    ///
    /// The graph revision is not compared, because a node keeps its identity
    /// across revisions of one epoch. Everything else must match, including the
    /// expected origin: a node identifier repeated under a different epoch or a
    /// different origin describes an unrelated node, never a refreshed one.
    pub fn identifies_same_node(&self, other: &Self) -> bool {
        self.is_same_document(other)
            && self.node_id == other.node_id
            && self.expected_origin == other.expected_origin
    }

    /// Whether this handle still belongs to the tab, frame, and epoch the
    /// broker currently reports as active.
    ///
    /// A `false` result means the handle is stale and the runtime must observe
    /// again. Section 12 forbids retrying the old handle, and forbids
    /// re-resolving the target by selector, text, ordinal, or coordinates.
    pub fn is_current(&self, tab_id: &TabId, frame_id: &FrameId, page_epoch: &PageEpoch) -> bool {
        self.tab_id == *tab_id && self.frame_id == *frame_id && self.page_epoch == *page_epoch
    }
}

/// Allocates semantic node identifiers inside one scope without ever reusing
/// one.
///
/// Section 5.4 is a lifetime rule, and this makes it executable: the browser
/// broker mints the identifiers that ship, but consumers, fixtures, and tests
/// need a conforming sequence to check themselves against. Non-reuse follows
/// from strict monotonicity of the counter rather than from a retired-identifier
/// set, so retirement costs no memory and cannot be defeated by forgetting to
/// record a removal.
///
/// The allocator is deterministic by construction: the counter is supplied by
/// the caller, and nothing here reads a clock, a random source, or global
/// state.
#[derive(Clone, Debug)]
pub struct NodeIdAllocator {
    scope: NodeIdScope,
    next: u64,
}

impl NodeIdAllocator {
    /// Starts an allocator for `scope` at the given counter value.
    ///
    /// A fresh page epoch starts a fresh namespace, so the usual first value is
    /// zero. Resuming above a known-issued value is also safe; resuming below
    /// one is what the type exists to prevent, so the counter is never lowered
    /// after construction.
    pub fn new(scope: NodeIdScope, first: u64) -> Self {
        Self { scope, next: first }
    }

    /// The scope every identifier from this allocator belongs to.
    pub fn scope(&self) -> &NodeIdScope {
        &self.scope
    }

    /// The counter value the next call to [`Self::allocate`] would use.
    pub fn next_counter(&self) -> u64 {
        self.next
    }

    /// Issues the next identifier, or `None` once the counter is exhausted.
    ///
    /// Exhaustion returns `None` rather than wrapping, because wrapping is
    /// exactly the reuse this type forbids. A caller that reaches it must
    /// invalidate the epoch and start a new one.
    ///
    /// The last representable counter is never issued: the allocator would have
    /// no way to record that it had been consumed, and an identifier it cannot
    /// prove it will not issue again is one it must not issue at all.
    pub fn allocate(&mut self) -> Option<SemanticNodeId> {
        let counter = self.next;
        self.next = self.next.checked_add(1)?;
        Some(SemanticNodeId::new(counter.to_string()))
    }
}

#[cfg(test)]
mod tests {
    use super::{
        FrameId, GraphRevision, NodeIdAllocator, NodeIdScope, Origin, OriginKind, PageEpoch,
        ScopedGraphRevision, SemanticNodeId, TabId,
    };
    use crate::identity::NodeHandle;

    fn scope() -> NodeIdScope {
        NodeIdScope::new(FrameId::new("frame_main"), PageEpoch::new("epoch_a"))
    }

    fn tuple_origin() -> Origin {
        Origin {
            kind: OriginKind::Tuple,
            serialization: Some("https://example.test".to_owned()),
            opaque_id: None,
        }
    }

    #[test]
    fn an_allocator_issues_a_strictly_increasing_sequence() {
        let mut allocator = NodeIdAllocator::new(scope(), 0);
        let first = allocator.allocate();
        let second = allocator.allocate();

        assert_eq!(first, Some(SemanticNodeId::new("0")));
        assert_eq!(second, Some(SemanticNodeId::new("1")));
        assert_eq!(allocator.next_counter(), 2);
        assert_eq!(allocator.scope(), &scope());
    }

    #[test]
    fn an_exhausted_allocator_refuses_rather_than_wrapping() {
        let mut allocator = NodeIdAllocator::new(scope(), u64::MAX - 1);
        assert_eq!(
            allocator.allocate(),
            Some(SemanticNodeId::new("18446744073709551614"))
        );

        // The last representable counter is withheld: the allocator could not
        // record having consumed it, so issuing it would risk reuse.
        assert_eq!(allocator.allocate(), None);
        // Exhaustion is permanent, not a transient refusal.
        assert_eq!(allocator.allocate(), None);
    }

    #[test]
    fn a_handle_carries_the_scope_its_node_id_belongs_to() {
        let handle = NodeHandle::new(
            TabId::new("tab_01"),
            FrameId::new("frame_main"),
            PageEpoch::new("epoch_a"),
            GraphRevision(7),
            SemanticNodeId::new("n_link"),
            tuple_origin(),
        );

        assert_eq!(handle.scope(), scope());
        assert_eq!(handle.scoped_revision().revision(), GraphRevision(7));
        assert!(handle.is_current(
            &TabId::new("tab_01"),
            &FrameId::new("frame_main"),
            &PageEpoch::new("epoch_a"),
        ));
        assert!(!handle.is_current(
            &TabId::new("tab_01"),
            &FrameId::new("frame_main"),
            &PageEpoch::new("epoch_b"),
        ));
    }

    #[test]
    fn the_same_node_across_revisions_is_still_the_same_node() {
        let at_three = NodeHandle::new(
            TabId::new("tab_01"),
            FrameId::new("frame_main"),
            PageEpoch::new("epoch_a"),
            GraphRevision(3),
            SemanticNodeId::new("n_link"),
            tuple_origin(),
        );
        let at_nine = NodeHandle::new(
            TabId::new("tab_01"),
            FrameId::new("frame_main"),
            PageEpoch::new("epoch_a"),
            GraphRevision(9),
            SemanticNodeId::new("n_link"),
            tuple_origin(),
        );

        assert!(at_three.identifies_same_node(&at_nine));
        assert!(at_nine
            .scoped_revision()
            .satisfies(&at_three.scoped_revision()));
        assert!(!at_three
            .scoped_revision()
            .satisfies(&at_nine.scoped_revision()));
    }

    #[test]
    fn a_different_expected_origin_is_a_different_node() {
        let opaque = Origin {
            kind: OriginKind::Opaque,
            serialization: None,
            opaque_id: Some("op_1".to_owned()),
        };
        let tuple = NodeHandle::new(
            TabId::new("tab_01"),
            FrameId::new("frame_main"),
            PageEpoch::new("epoch_a"),
            GraphRevision(1),
            SemanticNodeId::new("n_1"),
            tuple_origin(),
        );
        let sandboxed = NodeHandle::new(
            TabId::new("tab_01"),
            FrameId::new("frame_main"),
            PageEpoch::new("epoch_a"),
            GraphRevision(1),
            SemanticNodeId::new("n_1"),
            opaque,
        );

        // An opaque origin is never broadened to a predecessor origin
        // (section 5.5), so these are unrelated targets.
        assert!(!tuple.identifies_same_node(&sandboxed));
    }

    #[test]
    fn container_identifiers_sort_deterministically() {
        let mut tabs = vec![
            TabId::new("tab_b"),
            TabId::new("tab_a"),
            TabId::new("tab_c"),
        ];
        tabs.sort();
        assert_eq!(
            tabs,
            vec![
                TabId::new("tab_a"),
                TabId::new("tab_b"),
                TabId::new("tab_c")
            ]
        );
    }

    #[test]
    fn a_revision_from_another_scope_never_satisfies_a_requirement() {
        let required = ScopedGraphRevision::new(scope(), GraphRevision(1));
        let elsewhere = ScopedGraphRevision::new(
            NodeIdScope::new(FrameId::new("frame_main"), PageEpoch::new("epoch_b")),
            GraphRevision(u64::MAX),
        );

        assert!(elsewhere.partial_cmp(&required).is_none());
        assert!(!elsewhere.satisfies(&required));
    }
}
