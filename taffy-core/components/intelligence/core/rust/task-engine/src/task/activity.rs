// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a task did, in order (decision 0148).
//!
//! The task view promises a timeline in plain language — "Read croma.com —
//! found 2 prices", "Skipped reddit.com — page didn't load" (UX spec section
//! 6). It was derived on the surface from the workspace's own sources and
//! facts, so a step existed only where a fact landed: an errand's timeline was
//! empty, a failed task's timeline was empty in exactly the case it matters
//! most, and a page read three times appeared once, in the order facts landed
//! rather than the order Taffy acted.
//!
//! This is the record itself. Three properties are the whole of its design.
//!
//! **A closed kind, a host, a count and a time — and nothing else.** No page
//! text, no model text, no tool name, no address beyond the host. What a step
//! *says* is composed on the surface from a compiled-in template, which is what
//! makes the plain-language rule mechanical rather than editorial: a step can
//! only ever render one of a fixed set of sentences, and those sentences can be
//! translated (parity row PAR-L10N-001).
//!
//! **Bounded, and the bound is part of the record.** A task that loops is
//! exactly the task whose record matters, and an unbounded list is a durable
//! structure whose size is a function of a model's behaviour.
//!
//! **Appended where transitions are committed.** The reducer is the only thing
//! that commits a transition, so a step appended anywhere else — in a port, in
//! a projection, in the browser — is a step a restored task does not have, and
//! a timeline that empties when a person reopens the browser is worse than one
//! that was never there.

use crate::time::UtcMillis;

/// How many steps one task keeps.
///
/// Thirty-two, oldest dropped. Enough to show a loop starting — the failed task
/// that provoked this record had read the same page eighty-five times, and
/// thirty-two of those is already the whole story — and small enough that the
/// list is a fact about the task rather than a second journal.
pub const MAX_TASK_ACTIVITY: usize = 32;

/// One thing a task did.
///
/// The kinds are the ones UX spec section 6's own examples name, and no others.
/// `sign_in_needed` and `authority_revoked` stood in an earlier set and do not
/// return: a sign-in is a hand-back and reads as one, and revoked authority is
/// a task state the header already says.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum TaskActivityKind {
    /// A page was opened.
    OpenedPage,
    /// A page was read and produced facts.
    ReadPage,
    /// A page would not load, or could not be read.
    PageUnavailable,
    /// A move was refused, by policy or by the browser.
    MoveRefused,
    /// The person was asked for a decision, a value or a permission.
    AskedYou,
    /// The person answered.
    YouAnswered,
    /// Taffy handed the page over for the person to work.
    HandedBack,
    /// The person took the page over.
    YouTookOver,
    /// The output was assembled from the accepted facts.
    BuiltOutput,
}

impl TaskActivityKind {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::OpenedPage => "opened_page",
            Self::ReadPage => "read_page",
            Self::PageUnavailable => "page_unavailable",
            Self::MoveRefused => "move_refused",
            Self::AskedYou => "asked_you",
            Self::YouAnswered => "you_answered",
            Self::HandedBack => "handed_back",
            Self::YouTookOver => "you_took_over",
            Self::BuiltOutput => "built_output",
        }
    }

    /// Whether this kind is about a page, and so carries a host worth drawing.
    ///
    /// Not a rule the record enforces — a hand-back has a host and an answer
    /// does not, and both are legitimate — but the honest place to write down
    /// which is which, so a surface drawing "Opened" with nothing after it is
    /// a defect rather than a shrug.
    pub const fn names_a_host(self) -> bool {
        matches!(
            self,
            Self::OpenedPage
                | Self::ReadPage
                | Self::PageUnavailable
                | Self::MoveRefused
                | Self::AskedYou
                | Self::HandedBack
                | Self::YouTookOver
        )
    }
}

/// One step of what a task did.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskActivityStep {
    /// Which step this is, counted from one, over the task's whole life.
    ///
    /// It does not restart when the front of the list is dropped, so a surface
    /// can tell "the first thing that happened" from "the oldest thing still
    /// kept" — and two steps of the same kind on the same host in the same
    /// millisecond are still two steps.
    pub sequence: u64,
    /// What happened.
    pub kind: TaskActivityKind,
    /// The host it happened to, where the kind is about a page.
    ///
    /// A host and never a full address: the host is what a sentence needs, and
    /// a path is where a query string and a session token live.
    pub host: Option<String>,
    /// How many — of facts, for a read; of the facts an output was built from.
    /// Nought where the kind counts nothing.
    pub count: u32,
    /// When, on the wall clock, so a surface can group by day.
    pub at: UtcMillis,
}

/// A task's bounded, ordered record of what it did.
///
/// Oldest first, which is the order things happened; a surface that wants
/// newest first reverses it, and that is a presentation choice rather than a
/// fact about the task.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct TaskActivity {
    steps: Vec<TaskActivityStep>,
    appended: u64,
}

impl TaskActivity {
    /// The steps still kept, oldest first.
    pub fn steps(&self) -> &[TaskActivityStep] {
        &self.steps
    }

    /// How many steps have ever been appended, including those since dropped.
    pub const fn appended(&self) -> u64 {
        self.appended
    }

    /// Appends one step, dropping the oldest when the record is full.
    ///
    /// `pub(crate)` because the reducer is the only thing that may write one:
    /// a step appended from outside the fold is a step replay does not
    /// reproduce.
    pub(crate) fn append(
        &mut self,
        kind: TaskActivityKind,
        host: Option<String>,
        count: u32,
        at: UtcMillis,
    ) {
        self.appended = self.appended.saturating_add(1);
        if self.steps.len() >= MAX_TASK_ACTIVITY {
            self.steps.remove(0);
        }
        self.steps.push(TaskActivityStep {
            sequence: self.appended,
            kind,
            host,
            count,
            at,
        });
    }
}

/// The host of a normalized tuple origin, for a step to name.
///
/// A step carries a host and never a full address: the host is what the
/// sentence needs, and a path is where a query string and a session token
/// live. The input is already normalized by the browser — scheme, host and
/// optional port, nothing else — so this is a trim rather than a parser, and
/// anything it does not recognize it declines to name rather than guessing.
#[must_use]
pub fn host_of_origin(origin: &str) -> Option<String> {
    let after_scheme = origin.split_once("://").map_or(origin, |(_, rest)| rest);
    let authority = after_scheme
        .split(['/', '?', '#'])
        .next()
        .unwrap_or(after_scheme);
    // An IPv6 literal keeps its brackets, so the colon inside one is not a
    // port separator; every other authority splits at the first colon.
    let host = if authority.starts_with('[') {
        authority
            .split_once(']')
            .map_or(authority, |(inside, _)| inside)
    } else {
        authority.split(':').next().unwrap_or(authority)
    };
    let host = host.trim();
    if host.is_empty() {
        None
    } else {
        Some(host.to_owned())
    }
}
