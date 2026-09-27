// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The user-visible projection of the task state (decision 0010).
//!
//! Separate from [`super::state::TaskState`] on purpose. The internal taxonomy
//! is durable and thirteen members wide; this one is what a person reads, it
//! has exactly seven members, and the mapping runs one way. Keeping the two in
//! one file is how an internal name ends up on a screen.
/// The seven task states a person sees (domain model section 9.2, decision
/// 0010).
///
/// There is no eighth. Setup and consent are surfaces, not task states, which
/// is why [`TaskState::display`] returns `None` for them rather than inventing
/// a member here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum DisplayState {
    /// Running.
    Running,
    /// Waiting for you.
    WaitingForYou,
    /// Paused.
    Paused,
    /// Done.
    Done,
    /// Partly done.
    PartlyDone,
    /// Stopped.
    Stopped,
    /// Failed.
    Failed,
}

impl DisplayState {
    /// Every user-visible state, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Running,
        Self::WaitingForYou,
        Self::Paused,
        Self::Done,
        Self::PartlyDone,
        Self::Stopped,
        Self::Failed,
    ];

    /// The words shown to a person, from a trusted local template.
    pub const fn user_visible_text(self) -> &'static str {
        match self {
            Self::Running => "Running",
            Self::WaitingForYou => "Waiting for you",
            Self::Paused => "Paused",
            Self::Done => "Done",
            Self::PartlyDone => "Partly done",
            Self::Stopped => "Stopped",
            Self::Failed => "Failed",
        }
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Running => "running",
            Self::WaitingForYou => "waiting_for_you",
            Self::Paused => "paused",
            Self::Done => "done",
            Self::PartlyDone => "partly_done",
            Self::Stopped => "stopped",
            Self::Failed => "failed",
        }
    }
}
