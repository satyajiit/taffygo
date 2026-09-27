// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from taffy-core/build/product-capabilities.json. Do not edit.

use policy_engine::PolicyMilestone;
use task_engine::Milestone;

/// One compiled product exposure selected by the build profile.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Profile {
    /// Stable internal profile name recorded by release evidence.
    pub name: &'static str,
    /// Latest milestone whose exit has been formally accepted.
    pub accepted_milestone: Milestone,
    /// Whether the browser and core may admit a delegated task start.
    pub delegated_task_start: bool,
    /// Exact task vocabulary for starts, absent when starts are quarantined.
    pub task_milestone: Option<Milestone>,
    /// Exact policy surface paired with `task_milestone`.
    pub policy_milestone: Option<PolicyMilestone>,
}

/// Digest of the canonical validated authority document.
pub const CONFIGURATION_FINGERPRINT: &str = "sha256:f72e05085633c2d0a1ac32ecf34ec37c00ecd27d80668c61cee0276bf35e4723";

/// Candidate/release exposure. It exactly follows the accepted milestone.
pub const CANDIDATE: Profile = Profile {
    name: "candidate",
    accepted_milestone: Milestone::M7,
    delegated_task_start: true,
    task_milestone: Some(Milestone::M7),
    policy_milestone: Some(PolicyMilestone::M7),
};

/// Development-only bring-up exposure. It is never a promotable candidate.
pub const DEVELOPMENT: Profile = Profile {
    name: "development",
    accepted_milestone: Milestone::M7,
    delegated_task_start: true,
    task_milestone: Some(Milestone::M7),
    policy_milestone: Some(PolicyMilestone::M7),
};

#[cfg(taffy_candidate_capabilities)]
/// Capabilities selected by the current GN build.
pub const ACTIVE: Profile = CANDIDATE;

#[cfg(not(taffy_candidate_capabilities))]
/// Capabilities selected by the Cargo loop or a development GN build.
pub const ACTIVE: Profile = DEVELOPMENT;
