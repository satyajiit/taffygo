// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The order the tasks are projected in (decision 0149).
//!
//! The bar showed "Couldn't finish — Taffy could not read enough to answer" on
//! a freshly installed build, for a task that had ended the previous day. Three
//! facts made it: every task ever committed comes back from the journal, the
//! runtime holds its sessions in a map keyed by identity, and nothing ordered
//! them — so the "current" task was whichever identity sorted first, forever.
//!
//! This file is the ordering half. The other half — which of them a surface is
//! told about — is one step later, and `restored_tasks` is where it is checked,
//! because the two must not be the same step: everything projected here also
//! produces the browser bindings a restored effect is routed by.

use super::super::{recency_order, RecencyKey};

fn key(updated_at_millis: u64, task_id: &str) -> RecencyKey<'_> {
    RecencyKey {
        updated_at_millis,
        task_id,
    }
}

/// Recency, not identity.
#[test]
fn the_most_recently_changed_task_leads() {
    let keys = [
        key(100, "zzz-oldest"),
        key(300, "aaa-newest"),
        key(200, "mmm-middle"),
    ];

    assert_eq!(recency_order(&keys), vec![1, 2, 0]);
}

/// The exact shape of the defect: an old task with an early identity, which
/// sorted first under the map's own ordering and led the surfaces for the life
/// of the profile.
#[test]
fn an_old_task_with_an_early_identity_does_not_lead() {
    let keys = [key(1_000, "aaa-yesterday"), key(9_000, "zzz-now")];

    assert_eq!(recency_order(&keys), vec![1, 0]);
}

/// Ties break on identity, so two tasks stamped in the same millisecond do not
/// change places between one projection and the next.
#[test]
fn a_tie_is_broken_by_identity_so_the_order_never_flickers() {
    let keys = [key(400, "b"), key(400, "a")];

    assert_eq!(recency_order(&keys), vec![1, 0]);
}

/// It orders and never omits.
///
/// The first attempt at decision 0149 dropped the ended tasks here, one step
/// before the browser bindings are built from the same list — so the browser
/// was handed a restored task effect whose task it had no revision for, and
/// answered `[taffy_core_state_projection_refused] at=task-effect/unknown-task`
/// and then refused to start the core at all.
#[test]
fn every_key_given_comes_back() {
    let keys = [key(1, "a"), key(2, "b"), key(3, "c"), key(4, "d")];

    let mut order = recency_order(&keys);
    order.sort_unstable();
    assert_eq!(order, vec![0, 1, 2, 3]);
}
