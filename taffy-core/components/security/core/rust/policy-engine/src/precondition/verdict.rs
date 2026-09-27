// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The outcome of the pre-dispatch sequence.
//!
//! Two members, and the asymmetry between them is the point: proceeding says
//! nothing about the page, while a refusal carries the exact
//! [`ActionResultCode`] and the [`StaleNodeStep`] it stopped at. Every code a
//! refusal carries is a refusal *before* anything reached the page, which is
//! what makes re-observation and a fresh proposal safe after one.

use bip_types::ActionResultCode;

use crate::step::StaleNodeStep;
/// The outcome of the pre-dispatch sequence.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DispatchVerdict {
    /// Every check passed. The broker may journal the intent and dispatch.
    Proceed,
    /// The sequence stopped. The code is the terminal result for this action.
    Refuse {
        /// Which step stopped it.
        step: StaleNodeStep,
        /// The result code the action ends with.
        code: ActionResultCode,
    },
}

impl DispatchVerdict {
    /// Builds a refusal.
    pub const fn refuse(step: StaleNodeStep, code: ActionResultCode) -> Self {
        Self::Refuse { step, code }
    }

    /// Whether the broker may dispatch.
    pub const fn proceeds(self) -> bool {
        matches!(self, Self::Proceed)
    }

    /// The result code, for a refusal.
    pub const fn code(self) -> Option<ActionResultCode> {
        match self {
            Self::Proceed => None,
            Self::Refuse { code, .. } => Some(code),
        }
    }
}
