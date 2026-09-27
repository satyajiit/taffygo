// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! `EffectiveToolSet` and the reducer's own guard agree, driven through the
//! real reducer rather than through a copy of its rule.
//!
//! `EffectiveToolSet` is what a model is *shown*; `Guard::ToolAvailable` is
//! what a proposal is *checked against*. They apply the same milestone filter
//! and the same unconditional exception, and they read the allowlist
//! by the canonical registry row. A namespace member is therefore admitted
//! only when the allowlist names the whole row; naming a member cannot
//! accidentally grant a row that also owns three sibling operations.
//!
//! The unit tests beside `EffectiveToolSet` state the rule; this file is what
//! makes the statement about the *guard* true, because it asks the reducer
//! instead of reproducing its clause. A local reproduction of the guard's rule
//! goes on agreeing with itself after somebody changes the guard, which is
//! precisely the drift a pin about two modules exists to catch.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use common::{draft_from, plan_draft, preview, receipt, seed};
use task_engine::command::Command;
use task_engine::tool::{EffectiveToolSet, Milestone};

/// The registry's only namespace row, and so the only name this file is about.
const NAMESPACE_ROW: &str = "browser.tabs";
/// One member of it. The registry matches this; an exact-string list does not,
/// unless the list names this rather than the row.
const NAMESPACE_MEMBER: &str = "browser.tabs.open";

fn list(names: &[&str]) -> Vec<String> {
    names.iter().map(|name| (*name).to_owned()).collect()
}

/// Whether the reducer's guard admits a proposal for `tool` under `allowlist`.
///
/// A whole running task per question, because the guard is only reachable
/// through `apply` and the point of this file is to ask the real one.
fn guard_admits(allowlist: &[String], tool: &str) -> bool {
    let mut seeded = seed();
    seeded.snapshot.tool_allowlist = allowlist.to_vec();
    let mut fixture = draft_from(seeded);
    fixture.must_apply(Command::StartTask(preview()));
    fixture.must_apply(Command::AcceptInitialConsent(receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture.must_apply(Command::SetPlan(plan_draft()));
    fixture.reducer.admits_tool(tool)
}

fn set_admits(allowlist: &[String], tool: &str) -> bool {
    EffectiveToolSet::for_task(Milestone::M3, allowlist).admits(tool)
}

#[test]
fn naming_the_row_admits_its_member_in_both_places() {
    let by_row = list(&[NAMESPACE_ROW]);
    assert!(set_admits(&by_row, NAMESPACE_MEMBER));
    assert!(guard_admits(&by_row, NAMESPACE_MEMBER));
}

#[test]
fn naming_the_member_refuses_it_in_both_places() {
    // A namespace row is the smallest independently reviewed unit. Letting a
    // member name through here would make the guard looser than the set a
    // person consented to and the model was shown.
    let by_member = list(&[NAMESPACE_MEMBER]);
    assert!(!set_admits(&by_member, NAMESPACE_MEMBER));
    assert!(!guard_admits(&by_member, NAMESPACE_MEMBER));
}

#[test]
fn over_an_exact_row_the_two_answer_the_same_way() {
    // What keeps the divergence to one namespace row rather than making it a
    // second rule. Both directions are checked for each name — admitted when
    // the list carries it, refused when the list carries something else — so a
    // change that made either rule uniformly permissive fails here.
    for name in ["browser.dom.read", "browser.navigate", "browser.search"] {
        let narrowed = list(&[name]);
        assert!(set_admits(&narrowed, name), "{name}");
        assert!(guard_admits(&narrowed, name), "{name}");
        let elsewhere = list(&["browser.dom.scroll"]);
        assert_eq!(
            set_admits(&elsewhere, name),
            guard_admits(&elsewhere, name),
            "{name} under a list that omits it"
        );
    }
}

#[test]
fn the_unconditional_exception_is_the_same_exception_on_both_sides() {
    // `user.handover` survives every narrowing in both places. If it stopped
    // surviving in one of them, a task narrowed by a skill would be shown a
    // way out it could not propose, or could propose one it was not shown.
    let narrowed = list(&["browser.dom.read"]);
    assert!(set_admits(&narrowed, "user.handover"));
    assert!(guard_admits(&narrowed, "user.handover"));
    // Asking is a capability and not the exit, so it is narrowed away in both.
    assert!(!set_admits(&narrowed, "user.ask"));
    assert!(!guard_admits(&narrowed, "user.ask"));
}

#[test]
fn the_milestone_filter_is_the_same_filter_on_both_sides() {
    // A tool this build's milestone has not reached is absent from the set and
    // refused by the guard, and no allowlist changes either answer. The seed
    // runs at M3, so a write tool is the case.
    let asking_for_it = list(&["browser.form.fill"]);
    assert!(!set_admits(&asking_for_it, "browser.form.fill"));
    assert!(!guard_admits(&asking_for_it, "browser.form.fill"));
    // And an excluded name is refused whatever the milestone or the list says.
    let clipboard = list(&["device.clipboard.read"]);
    assert!(!set_admits(&clipboard, "device.clipboard.read"));
    assert!(!guard_admits(&clipboard, "device.clipboard.read"));

    let activation = "browser.dom.click";
    let explicitly_named = list(&[activation]);
    assert!(set_admits(&explicitly_named, activation), "{activation}");
    assert!(guard_admits(&explicitly_named, activation), "{activation}");

    let query = list(&["browser.dom.query"]);
    assert!(set_admits(&query, "browser.dom.query"));
    assert!(guard_admits(&query, "browser.dom.query"));
}
