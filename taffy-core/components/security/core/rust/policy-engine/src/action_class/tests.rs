// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The ratified surface of every milestone, written out once and shared.

mod phase;
mod resolve;
mod surface;

use super::PolicyMilestone;

/// The surface of every ratified milestone, written out.
///
/// The later rows make every widening visible. M5 adds browser-owned
/// exact-value fill and download flows; the remaining form operations stay
/// outside the policy surface.
pub(super) const SURFACES: &[(PolicyMilestone, &[&str])] = &[
    (
        PolicyMilestone::M2,
        &[
            "observe_page",
            "scroll_into_view",
            "open_link",
            "create_task_tab",
            "synthetic_click",
            "move_focus",
            "control_tab",
            "profile_store_read",
        ],
    ),
    (
        PolicyMilestone::M3,
        &[
            "observe_page",
            "scroll_into_view",
            "open_link",
            "create_task_tab",
            "synthetic_click",
            "move_focus",
            "control_tab",
            "profile_store_read",
        ],
    ),
    (
        PolicyMilestone::M5,
        &[
            "observe_page",
            "scroll_into_view",
            "open_link",
            "create_task_tab",
            "synthetic_click",
            "move_focus",
            "control_tab",
            "fill_field",
            "start_download",
            "profile_store_read",
        ],
    ),
    (
        PolicyMilestone::M6,
        &[
            "observe_page",
            "scroll_into_view",
            "open_link",
            "create_task_tab",
            "synthetic_click",
            "move_focus",
            "control_tab",
            "fill_field",
            "start_download",
            "library_read",
            "library_write",
            "memory_read",
            "memory_write",
            "profile_store_read",
        ],
    ),
    (
        PolicyMilestone::M7,
        &[
            "observe_page",
            "scroll_into_view",
            "open_link",
            "create_task_tab",
            "synthetic_click",
            "move_focus",
            "control_tab",
            "fill_field",
            "start_download",
            "library_read",
            "library_write",
            "memory_read",
            "memory_write",
            "execute_tool_job",
            "profile_store_read",
        ],
    ),
];

/// The written surface of one milestone.
pub(super) fn surface_of(milestone: PolicyMilestone) -> &'static [&'static str] {
    let Some((_, labels)) = SURFACES.iter().find(|(named, _)| *named == milestone) else {
        unreachable!("{} has no written surface", milestone.label())
    };
    labels
}
