// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The internal build milestone a tool name belongs to.
//!
//! Ordered, because "available from" is a comparison. Its own module because
//! it is the one type in this area that is not about tools at all: it is the
//! build ladder. Policy adapters translate their own ratified surface into
//! this contract at the composition boundary.

use core::fmt;

/// An internal build milestone (roadmap sections 3 to 11).
///
/// Ordered, because "available from" is a comparison. The order is the build
/// order and nothing else.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Milestone {
    /// Decisions and toolchain.
    M0,
    /// Browser core.
    M1,
    /// Page intelligence.
    M2,
    /// The assistant and workspaces.
    M3,
    /// Daily-browser completeness.
    M4,
    /// Write actions.
    M5,
    /// The knowledge store, memory, and media.
    M6,
    /// Computation, files, and skills.
    M7,
    /// Sync, ecosystem, and release.
    M8,
}

impl Milestone {
    /// Every milestone, in build order.
    pub const ALL: &'static [Self] = &[
        Self::M0,
        Self::M1,
        Self::M2,
        Self::M3,
        Self::M4,
        Self::M5,
        Self::M6,
        Self::M7,
        Self::M8,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::M0 => "M0",
            Self::M1 => "M1",
            Self::M2 => "M2",
            Self::M3 => "M3",
            Self::M4 => "M4",
            Self::M5 => "M5",
            Self::M6 => "M6",
            Self::M7 => "M7",
            Self::M8 => "M8",
        }
    }
}

impl fmt::Display for Milestone {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.label())
    }
}
