// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Compiled product capability selection.
//!
//! The JSON authority and its generator live under `taffy-core/build/`.
//! Callers see one immutable [`Profile`]; they do not choose a milestone or
//! reconstruct the candidate-versus-development rule themselves.

// The generator emits one compact table. Keep rustfmt from rewriting it into
// bytes that disagree with `product_capabilities.py --check`.
#[rustfmt::skip]
mod generated;

pub use generated::{Profile, ACTIVE, CANDIDATE, CONFIGURATION_FINGERPRINT, DEVELOPMENT};

use task_engine::Milestone;

/// Closed result of asking the active profile to admit a delegated task.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskStartAdmission {
    /// The selected profile carries this exact task milestone.
    Admit,
    /// Delegated task start is absent from the selected profile.
    Disabled,
    /// The sender and isolated core were compiled for different surfaces.
    MilestoneMismatch,
}

impl Profile {
    /// Applies the whole task-start profile rule at one seam.
    pub fn task_start_admission(self, requested: Milestone) -> TaskStartAdmission {
        if !self.delegated_task_start {
            return TaskStartAdmission::Disabled;
        }
        if self.task_milestone != Some(requested) {
            return TaskStartAdmission::MilestoneMismatch;
        }
        TaskStartAdmission::Admit
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn candidate_exactly_tracks_the_accepted_surface() {
        assert_eq!(CANDIDATE.name, "candidate");
        assert_eq!(
            CANDIDATE.delegated_task_start,
            CANDIDATE.task_milestone.is_some()
        );
        match CANDIDATE.accepted_milestone {
            Milestone::M0 | Milestone::M1 => {
                assert_eq!(CANDIDATE.task_milestone, None);
                assert_eq!(CANDIDATE.policy_milestone, None);
                assert_eq!(
                    CANDIDATE.task_start_admission(CANDIDATE.accepted_milestone),
                    TaskStartAdmission::Disabled
                );
            }
            accepted => {
                assert_eq!(CANDIDATE.task_milestone, Some(accepted));
                assert!(CANDIDATE.policy_milestone.is_some());
                assert_eq!(
                    CANDIDATE.task_start_admission(accepted),
                    TaskStartAdmission::Admit
                );
            }
        }
    }

    #[test]
    fn development_keeps_one_aligned_m7_surface() {
        assert_eq!(DEVELOPMENT.name, "development");
        assert_eq!(
            DEVELOPMENT.task_start_admission(Milestone::M7),
            TaskStartAdmission::Admit
        );
        assert_eq!(
            DEVELOPMENT.task_start_admission(Milestone::M5),
            TaskStartAdmission::MilestoneMismatch
        );
        assert_eq!(
            DEVELOPMENT.policy_milestone,
            Some(policy_engine::PolicyMilestone::M7)
        );
        assert!(CONFIGURATION_FINGERPRINT.starts_with("sha256:"));
        assert_eq!(CONFIGURATION_FINGERPRINT.len(), 71);
    }

    #[cfg(not(taffy_candidate_capabilities))]
    #[test]
    fn cargo_selects_the_development_adapter() {
        assert_eq!(ACTIVE, DEVELOPMENT);
    }

    #[cfg(taffy_candidate_capabilities)]
    #[test]
    fn candidate_cfg_selects_the_candidate_adapter() {
        assert_eq!(ACTIVE, CANDIDATE);
    }
}
