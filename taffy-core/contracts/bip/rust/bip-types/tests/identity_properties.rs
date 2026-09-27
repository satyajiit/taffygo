// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Properties of the identity and lifetime model (specification section 5).
//!
//! Two invariants are proved here over generated inputs rather than asserted
//! over examples, because both are the kind of rule an example test passes by
//! accident:
//!
//! 1. a handle carrying a different page epoch never equals a current one, and
//!    never resolves to the same node — even when every other field, including
//!    the node identifier, is identical;
//! 2. an allocation sequence never reuses a semantic node identifier inside one
//!    frame and page epoch.
//!
//! A third check is structural rather than generated: [`PageEpoch`] must not be
//! orderable at all. See `page_epoch_is_not_orderable`.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use std::collections::{HashMap, HashSet};
use std::marker::PhantomData;

use bip_types::identity::{
    FrameId, GraphRevision, NodeHandle, NodeIdAllocator, NodeIdScope, Origin, OriginKind,
    PageEpoch, ScopedGraphRevision, SemanticNodeId, TabId,
};
use proptest::prelude::*;

/// Opaque identifiers are arbitrary bytes to a consumer, so the generator makes
/// no assumption about their shape beyond being non-empty.
fn token() -> impl Strategy<Value = String> {
    "[a-zA-Z0-9_:.-]{1,16}"
}

fn origin() -> impl Strategy<Value = Origin> {
    prop_oneof![
        "[a-z]{2,8}".prop_map(|host| Origin {
            kind: OriginKind::Tuple,
            serialization: Some(format!("https://{host}.test")),
            opaque_id: None,
        }),
        token().prop_map(|id| Origin {
            kind: OriginKind::Opaque,
            serialization: None,
            opaque_id: Some(id),
        }),
    ]
}

fn handle(
    tab: &str,
    frame: &str,
    epoch: &str,
    revision: u64,
    node: &str,
    expected_origin: Origin,
) -> NodeHandle {
    NodeHandle::new(
        TabId::new(tab),
        FrameId::new(frame),
        PageEpoch::new(epoch),
        GraphRevision(revision),
        SemanticNodeId::new(node),
        expected_origin,
    )
}

proptest! {
    /// The strongest form: two handles that agree on tab, frame, revision, node
    /// identifier, and origin, and differ only in page epoch. Nothing about
    /// them may compare as the same node.
    #[test]
    fn a_handle_differing_only_in_page_epoch_is_never_the_same_node(
        tab in token(),
        frame in token(),
        node in token(),
        revision in any::<u64>(),
        expected_origin in origin(),
        epoch_a in token(),
        epoch_b in token(),
    ) {
        prop_assume!(epoch_a != epoch_b);

        let current = handle(&tab, &frame, &epoch_a, revision, &node, expected_origin.clone());
        let other = handle(&tab, &frame, &epoch_b, revision, &node, expected_origin);

        prop_assert_ne!(&current, &other);
        prop_assert!(!current.is_same_document(&other));
        prop_assert!(!current.identifies_same_node(&other));
        prop_assert!(!other.identifies_same_node(&current));
        prop_assert_ne!(current.scope(), other.scope());
    }

    /// The general form: any two handles whose epochs differ. The node
    /// identifier, revision, and origin are free to collide.
    #[test]
    fn no_handle_from_another_epoch_is_current(
        tab_a in token(),
        tab_b in token(),
        frame_a in token(),
        frame_b in token(),
        node_a in token(),
        node_b in token(),
        revision_a in any::<u64>(),
        revision_b in any::<u64>(),
        origin_a in origin(),
        origin_b in origin(),
        epoch_a in token(),
        epoch_b in token(),
    ) {
        prop_assume!(epoch_a != epoch_b);

        let current = handle(&tab_a, &frame_a, &epoch_a, revision_a, &node_a, origin_a);
        let other = handle(&tab_b, &frame_b, &epoch_b, revision_b, &node_b, origin_b);

        prop_assert_ne!(&current, &other);
        prop_assert!(!current.identifies_same_node(&other));

        // The broker reports the current binding; the stale handle must not
        // pass the check, whichever tab and frame it names.
        prop_assert!(!other.is_current(
            &TabId::new(&tab_b),
            &FrameId::new(&frame_b),
            &PageEpoch::new(&epoch_a),
        ));
    }

    /// Revisions observed under different epochs or frames are not comparable,
    /// so a stale handle can never satisfy a freshness requirement by carrying a
    /// larger number.
    #[test]
    fn revisions_from_different_scopes_are_incomparable(
        frame_a in token(),
        frame_b in token(),
        epoch_a in token(),
        epoch_b in token(),
        revision_a in any::<u64>(),
        revision_b in any::<u64>(),
    ) {
        prop_assume!(frame_a != frame_b || epoch_a != epoch_b);

        let left = ScopedGraphRevision::new(
            NodeIdScope::new(FrameId::new(&frame_a), PageEpoch::new(&epoch_a)),
            GraphRevision(revision_a),
        );
        let right = ScopedGraphRevision::new(
            NodeIdScope::new(FrameId::new(&frame_b), PageEpoch::new(&epoch_b)),
            GraphRevision(revision_b),
        );

        prop_assert!(left.partial_cmp(&right).is_none());
        prop_assert!(!left.satisfies(&right));
        prop_assert!(!right.satisfies(&left));
    }

    /// Inside one scope the revision is a total order, so a freshness
    /// requirement is satisfied by any observation that is not older.
    #[test]
    fn revisions_within_one_scope_order_normally(
        frame in token(),
        epoch in token(),
        revision_a in any::<u64>(),
        revision_b in any::<u64>(),
    ) {
        let scope = NodeIdScope::new(FrameId::new(&frame), PageEpoch::new(&epoch));
        let left = ScopedGraphRevision::new(scope.clone(), GraphRevision(revision_a));
        let right = ScopedGraphRevision::new(scope, GraphRevision(revision_b));

        prop_assert_eq!(left.satisfies(&right), revision_a >= revision_b);
        prop_assert!(left.partial_cmp(&right).is_some());
    }

    /// An allocation sequence issues no identifier twice inside one scope, no
    /// matter how the scopes interleave.
    ///
    /// The generated program is a list of steps, each naming one scope and how
    /// many identifiers to draw from it. Scopes are revisited, so an allocator
    /// that reset its counter on re-entry — the mistake this rule exists to
    /// forbid — would be caught.
    #[test]
    fn a_node_id_is_never_reused_within_a_frame_and_epoch(
        frames in prop::collection::vec(token(), 1..4),
        epochs in prop::collection::vec(token(), 1..4),
        first in any::<u64>(),
        steps in prop::collection::vec((0usize..12, 1usize..16), 1..24),
    ) {
        let scopes: Vec<NodeIdScope> = frames
            .iter()
            .flat_map(|frame| {
                epochs.iter().map(move |epoch| {
                    NodeIdScope::new(FrameId::new(frame), PageEpoch::new(epoch))
                })
            })
            .collect();

        let mut allocators: HashMap<NodeIdScope, NodeIdAllocator> = HashMap::new();
        let mut issued: HashSet<(NodeIdScope, SemanticNodeId)> = HashSet::new();
        let mut total = 0usize;

        for (scope_choice, draws) in steps {
            let scope = scopes[scope_choice % scopes.len()].clone();
            let allocator = allocators
                .entry(scope.clone())
                .or_insert_with(|| NodeIdAllocator::new(scope.clone(), first));

            for _ in 0..draws {
                let Some(node_id) = allocator.allocate() else {
                    // The counter is exhausted. Refusing to issue is the
                    // correct behaviour, so the sequence simply stops here.
                    break;
                };
                prop_assert!(
                    issued.insert((scope.clone(), node_id.clone())),
                    "{node_id} was issued twice inside one frame and page epoch",
                );
                total += 1;
            }
        }

        prop_assert_eq!(issued.len(), total);
    }

    /// The same identifier text under two different epochs describes unrelated
    /// nodes, which is why non-reuse is scoped rather than global.
    #[test]
    fn the_same_identifier_under_two_epochs_is_two_nodes(
        tab in token(),
        frame in token(),
        epoch_a in token(),
        epoch_b in token(),
        first in any::<u64>(),
        expected_origin in origin(),
    ) {
        prop_assume!(epoch_a != epoch_b);

        let mut old = NodeIdAllocator::new(
            NodeIdScope::new(FrameId::new(&frame), PageEpoch::new(&epoch_a)),
            first,
        );
        let mut fresh = NodeIdAllocator::new(
            NodeIdScope::new(FrameId::new(&frame), PageEpoch::new(&epoch_b)),
            first,
        );

        let (Some(old_id), Some(fresh_id)) = (old.allocate(), fresh.allocate()) else {
            return Ok(());
        };
        prop_assert_eq!(&old_id, &fresh_id);

        let old_handle = handle(&tab, &frame, &epoch_a, 0, old_id.as_str(), expected_origin.clone());
        let fresh_handle = handle(&tab, &frame, &epoch_b, 0, fresh_id.as_str(), expected_origin);
        prop_assert!(!old_handle.identifies_same_node(&fresh_handle));
    }
}

/// `PageEpoch` must not be orderable, and that has to be checked rather than
/// trusted: it is the kind of derive someone adds while making an unrelated
/// type sortable.
///
/// The probe resolves an inherent method that exists only when the type
/// implements [`Ord`], and falls back to a blanket trait method when it does
/// not. Rust prefers the inherent one, so the answer is the compiler's, not the
/// test's.
struct OrdProbe<T>(PhantomData<T>);

trait NotOrdered {
    #[allow(clippy::unused_self, reason = "the receiver is what selects the impl")]
    fn implements_ord(&self) -> bool {
        false
    }
}

impl<T> NotOrdered for OrdProbe<T> {}

impl<T: Ord> OrdProbe<T> {
    #[allow(clippy::unused_self, reason = "the receiver is what selects the impl")]
    fn implements_ord(&self) -> bool {
        true
    }
}

#[test]
fn page_epoch_is_not_orderable() {
    // The control: an identifier that is deliberately orderable, so a broken
    // probe fails loudly instead of passing everything.
    assert!(
        OrdProbe::<TabId>(PhantomData).implements_ord(),
        "TabId should be orderable for deterministic collections",
    );

    assert!(
        !OrdProbe::<PageEpoch>(PhantomData).implements_ord(),
        "section 5.2 forbids inferring ordering across frames or sessions, so \
         PageEpoch must implement no ordering at all",
    );
    assert!(
        !OrdProbe::<GraphRevision>(PhantomData).implements_ord(),
        "section 5.3 makes a revision meaningless outside its epoch, so \
         GraphRevision orders only through ScopedGraphRevision",
    );
    assert!(
        !OrdProbe::<NodeHandle>(PhantomData).implements_ord(),
        "a handle carries a page epoch, so it cannot be orderable either",
    );
    assert!(
        !OrdProbe::<ScopedGraphRevision>(PhantomData).implements_ord(),
        "revisions from different scopes are incomparable, so the ordering is \
         partial and Ord must not exist",
    );
}
