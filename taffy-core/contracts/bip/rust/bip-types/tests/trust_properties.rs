// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Properties of the content-trust lattice (specification section 9.4).
//!
//! The lattice laws are worth generating rather than asserting because the
//! safety claim rests on them. Content trust is what the egress check reads
//! before it lets free text reach a destination, so if `join` were not a least
//! upper bound, evidence from a page could combine into an element carrying
//! *fewer* authors than one of its inputs — which is the "nothing raises a
//! label" rule failing, and it would fail silently.
//!
//! Two properties here have no counterpart in the sensitivity suite, because
//! they are about propagation rather than about the lattice: that folding a
//! sequence of observations is monotone at every prefix, so no ordering of
//! hops loses an author, and that a value a model derived from a page carries
//! both of them.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::trust::{ContentTrust, TrustSet, NAMED_UNTRUSTED};
use proptest::prelude::*;

fn label() -> impl Strategy<Value = ContentTrust> {
    prop::sample::select(ContentTrust::ALL)
}

fn element() -> impl Strategy<Value = TrustSet> {
    prop::collection::vec(label(), 0..6).prop_map(TrustSet::from_iter)
}

proptest! {
    #[test]
    fn join_is_a_least_upper_bound(a in element(), b in element(), c in element()) {
        prop_assert_eq!(a.join(b), b.join(a));
        prop_assert_eq!(a.join(b).join(c), a.join(b.join(c)));
        prop_assert_eq!(a.join(a), a);
        prop_assert!(a.join(b).is_at_least_as_untrusted_as(a));
        prop_assert!(a.join(b).is_at_least_as_untrusted_as(b));
    }

    #[test]
    fn the_empty_element_is_the_identity(a in element()) {
        prop_assert_eq!(a.join(TrustSet::EMPTY), a);
        prop_assert!(a.is_at_least_as_untrusted_as(TrustSet::EMPTY));
    }

    #[test]
    fn membership_is_faithful(labels in prop::collection::vec(label(), 0..8)) {
        let element = TrustSet::from_iter(labels.clone());

        // Every label that names an author is retained. UserAuthored names
        // none: it is the bottom of the lattice, so it adds no member, and
        // asking whether the set contains it asks whether the set is empty.
        for one in &labels {
            if *one == ContentTrust::UserAuthored {
                continue;
            }
            prop_assert!(element.contains(*one));
        }
        for member in element.members() {
            prop_assert!(labels.contains(&member));
        }
    }

    /// No sequence of public operations ever loses an author.
    ///
    /// Asserted at every prefix rather than only at the end, because a
    /// propagation bug that dropped a label and re-added it later would satisfy
    /// an end-state check and still have leaked in the middle.
    #[test]
    fn accumulating_observations_is_monotone_at_every_prefix(
        labels in prop::collection::vec(label(), 1..12)
    ) {
        let mut accumulated = TrustSet::EMPTY;
        for one in labels {
            let before = accumulated;
            accumulated = accumulated.with(one);
            prop_assert!(accumulated.is_at_least_as_untrusted_as(before));
            if one != ContentTrust::UserAuthored {
                prop_assert!(accumulated.contains(one));
            }
        }
    }

    /// A value a model wrote from some evidence was written by both.
    ///
    /// This is the hop the whole design turns on: an answer built from a page
    /// must not become the product's own words merely by passing through a
    /// model.
    #[test]
    fn model_output_carries_the_evidence_it_was_built_from(evidence in element()) {
        let answer = evidence.with(ContentTrust::ModelAuthored);
        prop_assert!(answer.is_model_derived());
        prop_assert!(answer.is_at_least_as_untrusted_as(evidence));
        for member in evidence.members() {
            prop_assert!(answer.contains(member));
        }
    }

    /// An unlabelled observation is handled at least as strictly as a labelled
    /// one, whatever the labelled one said.
    #[test]
    fn an_unknown_author_is_never_the_lenient_answer(known in prop::sample::select(NAMED_UNTRUSTED)) {
        let unknown = TrustSet::of_optional(None);
        prop_assert!(unknown.carries_foreign_instructions());
        prop_assert!(unknown.has_unknown_author());
        prop_assert!(TrustSet::of(known).carries_foreign_instructions());
    }
}
