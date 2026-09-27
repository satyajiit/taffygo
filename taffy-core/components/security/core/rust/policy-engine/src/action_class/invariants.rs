// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The compile-time proofs over the taxonomy and the ratified surfaces.
//!
//! Each is a `const fn` walked without indexing, so the workspace's
//! `indexing_slicing` denial holds in a const context, and each is asserted
//! at build time rather than only under test: the values they defend are the
//! ones an approval can never reach.

use super::{authorized_classes, ActionClass, ClassAvailability, PolicyMilestone};

/// Whether [`ActionClass::ALL`] lists every class exactly once, in ordinal
/// order.
///
/// Walks the slice without indexing so the workspace's `indexing_slicing`
/// denial holds in a const context too.
const fn all_is_dense(mut rest: &[ActionClass], mut expected: usize) -> bool {
    while let Some((first, tail)) = rest.split_first() {
        if first.ordinal() != expected {
            return false;
        }
        rest = tail;
        expected += 1;
    }
    expected == ActionClass::COUNT
}

// The build fails here when a class exists but is not listed, when one is
// listed twice, or when the list and the ordinals disagree about the order.
const _: () = assert!(all_is_dense(ActionClass::ALL, 0));

/// Whether no milestone's allowlist names a class the release excludes or
/// prohibits (decision 0089 section 1).
///
/// The promise that a purchase, a message send, a credential extraction and a
/// bot-check bypass are unreachable used to rest on nobody typing them into
/// [`authorized_classes`]. It rests on this instead. Walking the slices without
/// indexing keeps the workspace's `indexing_slicing` denial in a const context.
const fn no_allowlist_names_an_unavailable_class(mut milestones: &[PolicyMilestone]) -> bool {
    while let Some((milestone, rest)) = milestones.split_first() {
        let mut classes = authorized_classes(*milestone);
        while let Some((class, tail)) = classes.split_first() {
            if matches!(
                class.availability(),
                ClassAvailability::ExcludedFromRelease | ClassAvailability::Prohibited
            ) {
                return false;
            }
            classes = tail;
        }
        milestones = rest;
    }
    true
}

/// Whether every authorized class has a browser-owned closed consequence.
///
/// Protocol support is deliberately not consulted here. A generated enum and
/// an executable renderer action prove implementation only; neither can tell
/// policy that a form control is a search rather than a purchase.
const fn no_allowlist_names_an_unclassified_page_write(mut milestones: &[PolicyMilestone]) -> bool {
    while let Some((milestone, rest)) = milestones.split_first() {
        let mut classes = authorized_classes(*milestone);
        while let Some((class, tail)) = classes.split_first() {
            if !class.page_consequence_is_classified() {
                return false;
            }
            classes = tail;
        }
        milestones = rest;
    }
    true
}

// The build fails here when any milestone's allowlist names a class this
// release excludes or permanently prohibits. It is a const assertion rather
// than only a test because the value it defends is the one an approval can
// never reach: a class that is prohibited is prohibited whatever else is true
// of a request, and a table that named one would be wrong before any test ran.
const _: () = assert!(no_allowlist_names_an_unavailable_class(
    PolicyMilestone::ALL
));
const _: () = assert!(no_allowlist_names_an_unclassified_page_write(
    PolicyMilestone::ALL
));
