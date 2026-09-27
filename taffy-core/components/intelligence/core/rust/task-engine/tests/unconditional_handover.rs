// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A narrowed allowlist may take capabilities away. It may not take the exit.
//!
//! Decision 0055 lets a skill or a recorded procedure narrow the tool surface,
//! and narrowing is safe because it can only remove authority. `user.handover`
//! is the one name that argument does not cover: it is how a task *stops* and
//! gives the page back to the person, and decision 0054 section 6 makes it
//! what remains once everything else has been refused. Taffy never learns to
//! recognise a challenge meant to prove a person is present — the class is
//! prohibited — so exhaustion is the mechanism, and exhaustion only works
//! while something is left at the end of it.
//!
//! A procedure that merely forgot to list the handover would otherwise strand
//! every task it ran: refused each remaining tool, reaching for the way out,
//! and refused that too, with no way to tell the person whose page it is.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use common::{draft_from, plan_draft, preview, receipt, seed};
use task_engine::command::Command;

/// A task whose allowlist admits reading and nothing else.
fn narrowed_to_reading() -> common::Fixture {
    let mut seeded = seed();
    seeded.snapshot.tool_allowlist = vec!["browser.dom.read".to_owned()];
    let mut fixture = draft_from(seeded);
    fixture.must_apply(Command::StartTask(preview()));
    fixture.must_apply(Command::AcceptInitialConsent(receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture.must_apply(Command::SetPlan(plan_draft()));
    fixture
}

#[test]
fn a_narrowed_allowlist_still_admits_the_handover() {
    let fixture = narrowed_to_reading();
    assert!(
        fixture.reducer.admits_tool("user.handover"),
        "an allowlist that omits the handover removed the way out, not a capability"
    );
}

#[test]
fn a_narrowed_allowlist_still_refuses_an_ordinary_absent_tool() {
    // The contrast that makes the test above mean something. `browser.navigate`
    // is available at this milestone exactly as `user.handover` is, so the
    // allowlist is the only thing separating the two verdicts.
    let fixture = narrowed_to_reading();
    assert!(
        !fixture.reducer.admits_tool("browser.navigate"),
        "a tool the allowlist omits must stay refused"
    );
}
