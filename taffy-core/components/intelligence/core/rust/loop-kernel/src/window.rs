// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Context-budget planning: the one decision point for what a turn may carry.
//!
//! Decision 0074. The eviction *mechanics* deliberately do not live here:
//! the unit that is dropped, the pairing rule that keeps a `Called` with its
//! `Returned`, and the oldest-first order are all
//! [`crate::context::transcript`]'s, where the pairing rule is unrepresentable
//! rather than checked. This module owns the *decision* the walk reads —
//! whether the conversation fits, must durably shrink first, or is at the
//! ladder's honest end — and the seam a future summarizer plugs into.
//!
//! The budget today is the byte bound OD-110 owns. When a measured
//! per-model context window replaces it, the number changes inside
//! [`crate::context::TranscriptBudget`] and this decision point is already
//! where every consumer reads the verdict.

pub mod plan;
pub mod summarizer;

pub use plan::{plan_context_window, WindowPlan};
pub use summarizer::TurnSummarizer;
