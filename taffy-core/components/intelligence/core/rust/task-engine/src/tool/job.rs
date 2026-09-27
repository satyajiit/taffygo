// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One tool job's identity and its one durable completion (decision 0075).
//!
//! A tool job is an ordinary action: proposed by the loop, decided by
//! `policy-engine`, dispatched once, and settled by exactly one recorded
//! outcome. What is different is where it runs — an isolated, capability-free
//! runtime the core supervises — and what its outcome is: not a page result
//! code but a closed status, the digest of what it produced, and counts. The
//! output *content* never enters the journal; like a page observation it stays
//! resident, and the digest is what makes the durable record checkable.

use bip_types::identity::{ActionId, TaskId};

use crate::ids::ToolJobId;

/// Derives the one job identity a dispatched tool-job action carries.
///
/// Derived, never minted: the action identity is already task-scoped, unique
/// and replay-stable, so the job named after it is too. An identifier must
/// name what the effect is about — the delivery plane paid for the general
/// form of the opposite choice when a position-derived identity collided the
/// moment the batch ahead of it moved.
pub fn job_id_for_action(task_id: &TaskId, action_id: &ActionId) -> ToolJobId {
    ToolJobId::new(format!("job-{}-{}", task_id.as_str(), action_id.as_str()))
}

/// How one tool job ended, as the broker reported it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ToolJobStatus {
    /// The job ran and its validated output was accepted.
    Succeeded,
    /// The job ran and failed, or its output failed validation.
    Failed,
    /// The job was cancelled before it finished.
    Cancelled,
    /// The supervisor cannot say what happened; the job is reconciled, never
    /// retried blind.
    OutcomeUnknown,
    /// No executor could run the job; it never started. The attempt is spent
    /// — the dispatch journaled the intent — and the answer is a correlated
    /// terminal rather than silence, per the executor rule: never acknowledge
    /// work that did not run, and never choose another effect family as a
    /// fallback.
    Unavailable,
}

impl ToolJobStatus {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Succeeded => "succeeded",
            Self::Failed => "failed",
            Self::Cancelled => "cancelled",
            Self::OutcomeUnknown => "outcome_unknown",
            Self::Unavailable => "unavailable",
        }
    }
}

/// The durable record of what one tool job produced.
///
/// Counts and a digest, never content — the journal keeps the shape of the
/// work the way it keeps the shape of a model turn, and the bytes themselves
/// stay resident where the completion delivered them.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ToolJobOutcome {
    /// How the job ended.
    pub status: ToolJobStatus,
    /// Lowercase hexadecimal SHA-256 of the produced output, when any was.
    pub output_digest: Option<String>,
    /// Total produced output bytes.
    pub output_bytes: u64,
    /// How many bounded chunks carried it.
    pub output_chunks: u32,
}
