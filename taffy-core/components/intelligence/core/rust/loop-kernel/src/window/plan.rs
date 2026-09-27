// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The verdict one composed conversation gets before its turn is requested.

use crate::context::TaskTranscript;

/// What the walk does about the context budget before requesting a turn.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WindowPlan {
    /// Everything the transcript kept fits, and nothing was dropped beyond
    /// the durable boundary. The turn may be requested as composed.
    Fits,
    /// The byte ladder dropped turns past the journaled boundary. The walk
    /// records the new boundary as [`RecordContextEviction`] first, so the
    /// shrunken conversation is a durable fact before any request carries it
    /// — an audit derives what the model could no longer see from the
    /// journal, and a replay composes the same conversation this run did.
    ///
    /// [`RecordContextEviction`]: task_engine::Command::RecordContextEviction
    Evict {
        /// The highest turn ordinal to evict; every earlier turn goes too.
        through_turn: u64,
    },
    /// The goal and the newest exchange together exceed the budget, and the
    /// ladder has nothing left it is allowed to drop. The turn proceeds
    /// anyway at the same price — degrading is never fatal — and this verdict
    /// names the fact for the caller that wants to observe or test it.
    OverBudget,
}

/// Plans the context window for one composed transcript.
///
/// Pure, and derived only from what the transcript's own ladder already
/// decided, which is itself a pure function of the durable journal, the
/// journaled eviction boundary and the compiled-in budget — so the verdict is
/// replay-stable, and two composes at the same durable revision reach the
/// same one.
#[must_use]
pub fn plan_context_window(transcript: &TaskTranscript) -> WindowPlan {
    if let Some(through_turn) = transcript.pending_eviction_through() {
        return WindowPlan::Evict { through_turn };
    }
    if transcript.over_budget() {
        return WindowPlan::OverBudget;
    }
    WindowPlan::Fits
}

#[cfg(test)]
mod tests {
    use model_router::json::JsonValue;
    use task_engine::ActionState;

    use crate::context::{RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange};

    use super::{plan_context_window, WindowPlan};

    fn exchange(ordinal: u64, padding: usize) -> TurnExchange {
        let call = RecordedCall::new(
            format!("call-{ordinal}"),
            "a".repeat(padding),
            JsonValue::Object(std::collections::BTreeMap::new()),
            ActionState::Proposed,
        );
        let Some(exchange) = TurnExchange::new(ordinal, vec![call]) else {
            unreachable!()
        };
        exchange
    }

    #[test]
    fn a_fitting_conversation_needs_nothing() {
        let transcript = TaskTranscript::new(
            "goal".to_owned(),
            vec![exchange(0, 8), exchange(1, 8)],
            TranscriptBudget::new(4096),
        );
        assert_eq!(plan_context_window(&transcript), WindowPlan::Fits);
    }

    #[test]
    fn a_ladder_drop_past_the_floor_demands_a_durable_boundary() {
        let transcript = TaskTranscript::new(
            "goal".to_owned(),
            vec![exchange(0, 600), exchange(1, 600), exchange(2, 600)],
            TranscriptBudget::new(1400),
        );
        assert_eq!(
            plan_context_window(&transcript),
            WindowPlan::Evict { through_turn: 1 }
        );
    }

    #[test]
    fn the_same_drop_under_the_journaled_floor_is_settled() {
        // The same conversation after the boundary is durable: the floor
        // drops what the ladder dropped, the ladder has nothing left to
        // decide, and the verdict is the stable one.
        let transcript = TaskTranscript::with_floor(
            "goal".to_owned(),
            vec![exchange(0, 600), exchange(1, 600), exchange(2, 600)],
            TranscriptBudget::new(1400),
            Some(1),
        );
        assert_eq!(plan_context_window(&transcript), WindowPlan::Fits);
        assert_eq!(transcript.elided(), 2);
    }

    #[test]
    fn the_honest_end_of_the_ladder_is_named_not_fatal() {
        let transcript = TaskTranscript::new(
            "goal".to_owned(),
            vec![exchange(0, 600)],
            TranscriptBudget::new(64),
        );
        assert_eq!(plan_context_window(&transcript), WindowPlan::OverBudget);
        assert_eq!(transcript.exchanges().len(), 1);
    }
}
