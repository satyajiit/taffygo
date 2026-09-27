// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The canonical observer: content-free counters over settled facts.
//!
//! The counters are the seed of the M3 observability work — counts of durable
//! commits keyed by the closed state vocabulary, never content. A richer sink
//! replaces the accumulation, not the port.

use std::collections::BTreeMap;

use task_engine::TaskState;

use crate::ports::{SettledFact, TurnObserverPort};

/// Counts settled facts by shape. Holds no identifier and no content: the
/// key vocabulary is the closed [`TaskState`] enumeration's own names.
#[derive(Debug, Default)]
pub struct ProductionTurnObserver {
    committed_by_state: BTreeMap<&'static str, u64>,
}

const fn state_name(state: TaskState) -> &'static str {
    match state {
        TaskState::Draft => "draft",
        TaskState::AwaitingConsent => "awaiting-consent",
        TaskState::Queued => "queued",
        TaskState::Running => "running",
        TaskState::WaitingUser => "waiting-user",
        TaskState::Pausing => "pausing",
        TaskState::Paused => "paused",
        TaskState::Cancelling => "cancelling",
        TaskState::Completing => "completing",
        TaskState::Cancelled => "cancelled",
        TaskState::Completed => "completed",
        TaskState::Partial => "partial",
        TaskState::Failed => "failed",
    }
}

impl ProductionTurnObserver {
    /// The commit counts, keyed by the state the committed command reached.
    pub fn committed_by_state(&self) -> impl Iterator<Item = (&'static str, u64)> + '_ {
        self.committed_by_state
            .iter()
            .map(|(name, count)| (*name, *count))
    }
}

impl TurnObserverPort for ProductionTurnObserver {
    fn observe(&mut self, fact: &SettledFact<'_>) {
        match fact {
            SettledFact::CommandCommitted { to, .. } => {
                let counter = self.committed_by_state.entry(state_name(*to)).or_insert(0);
                *counter = counter.saturating_add(1);
            }
        }
    }
}
