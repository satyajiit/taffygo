// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Post-hoc observation of settled, content-free facts (decision 0073).
//!
//! An observer is called after the commit that made a fact durable and
//! returns nothing, so it cannot refuse, retry, or reorder anything —
//! replaying the same journal with every observer removed reaches the same
//! state and proposes the same next command, and a test holds that. The fact
//! vocabulary is closed and carries shapes the journal already speaks; never
//! text, never a page, never an argument value.

use task_engine::TaskState;

/// One settled, content-free fact the runtime can attest.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SettledFact<'a> {
    /// One durable command committed for a task: the state it left, the state
    /// it reached, and the revision the journal now holds.
    CommandCommitted {
        task_id: &'a str,
        from: TaskState,
        to: TaskState,
        revision: u64,
    },
}

/// The observation port. Implementations count and forward; they decide
/// nothing.
pub trait TurnObserverPort {
    /// Observes one settled fact. Called after the commit is durable.
    fn observe(&mut self, fact: &SettledFact<'_>);
}

impl<T> TurnObserverPort for Box<T>
where
    T: TurnObserverPort + ?Sized,
{
    fn observe(&mut self, fact: &SettledFact<'_>) {
        self.as_mut().observe(fact);
    }
}
