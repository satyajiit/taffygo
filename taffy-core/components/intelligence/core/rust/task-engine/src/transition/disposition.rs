// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the table says about one state and one command.
//!
//! Four shapes — move, record, record-or-move, refuse — and the const
//! constructors the table is written with. Separated from the table itself so
//! the *grammar* of a cell can be read without scrolling past nine hundred
//! cells that use it.

use super::guard::Guard;
use super::refusal::RefusalReason;
use crate::task::TaskState;
/// What the table says about one state and one command.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Disposition {
    /// The command is meaningful and moves the task, once every guard holds.
    Transition {
        /// Every state the command may reach from here.
        targets: &'static [TaskState],
        /// What has to hold first.
        guards: &'static [Guard],
    },
    /// The command is meaningful, changes data, and moves nothing.
    Recorded {
        /// What has to hold first.
        guards: &'static [Guard],
    },
    /// The command records in place for some payloads and moves for others.
    RecordedOrTransition {
        /// Every state a moving payload may reach from here.
        targets: &'static [TaskState],
        /// What has to hold first.
        guards: &'static [Guard],
    },
    /// The command is refused here, for this reason.
    Refused(RefusalReason),
}

impl Disposition {
    /// Whether the command is accepted at all.
    pub const fn is_accepted(self) -> bool {
        !matches!(self, Self::Refused(_))
    }

    /// The guards the reducer has to check.
    pub const fn guards(self) -> &'static [Guard] {
        match self {
            Self::Transition { guards, .. }
            | Self::Recorded { guards }
            | Self::RecordedOrTransition { guards, .. } => guards,
            Self::Refused(_) => &[],
        }
    }

    /// The states the command may reach, which is empty when it moves nothing.
    pub const fn targets(self) -> &'static [TaskState] {
        match self {
            Self::Transition { targets, .. } | Self::RecordedOrTransition { targets, .. } => {
                targets
            }
            Self::Recorded { .. } | Self::Refused(_) => &[],
        }
    }

    /// The refusal, when the cell is a refusal.
    pub const fn refusal(self) -> Option<RefusalReason> {
        match self {
            Self::Refused(reason) => Some(reason),
            Self::Transition { .. } | Self::Recorded { .. } | Self::RecordedOrTransition { .. } => {
                None
            }
        }
    }
}

pub(super) const fn moves(targets: &'static [TaskState]) -> Disposition {
    Disposition::Transition {
        targets,
        guards: &[],
    }
}

pub(super) const fn moves_if(
    targets: &'static [TaskState],
    guards: &'static [Guard],
) -> Disposition {
    Disposition::Transition { targets, guards }
}

pub(super) const fn records() -> Disposition {
    Disposition::Recorded { guards: &[] }
}

pub(super) const fn records_if(guards: &'static [Guard]) -> Disposition {
    Disposition::Recorded { guards }
}

pub(super) const fn records_or_moves_if(
    targets: &'static [TaskState],
    guards: &'static [Guard],
) -> Disposition {
    Disposition::RecordedOrTransition { targets, guards }
}

pub(super) const fn refuse(reason: RefusalReason) -> Disposition {
    Disposition::Refused(reason)
}
