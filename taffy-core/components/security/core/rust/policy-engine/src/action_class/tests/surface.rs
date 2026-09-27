// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which classes each ratified milestone authorizes, and which it refuses.

use super::{surface_of, SURFACES};
use crate::action_class::{authorized_classes, ActionClass, ClassAvailability, PolicyMilestone};

#[test]
fn each_milestones_ratified_surface_is_exactly_the_list_written_here() {
    for (milestone, expected) in SURFACES {
        let labels: Vec<&str> = authorized_classes(*milestone)
            .iter()
            .map(|class| class.label())
            .collect();
        assert_eq!(labels, expected.to_vec(), "{} surface", milestone.label());
    }
    // A milestone added without a row here is not skipped, it fails.
    for milestone in PolicyMilestone::ALL {
        assert!(
            SURFACES.iter().any(|(named, _)| named == milestone),
            "{} has no written surface",
            milestone.label()
        );
    }
}

#[test]
fn every_class_outside_the_surface_is_denied_at_every_ratified_milestone() {
    for class in ActionClass::ALL {
        for milestone in PolicyMilestone::ALL {
            let expected = match milestone {
                // M2 and M3, verbatim: on the read-oriented surface the
                // availability reading and the allowlist are one statement.
                PolicyMilestone::M2 | PolicyMilestone::M3 => {
                    class.availability().can_be_authorized_today()
                }
                // M5 is where the two part, so the expectation is the
                // written list rather than a reading of availability.
                // Six classes share the reservation, while only the
                // browser-owned fill and download flows are authorized.
                PolicyMilestone::M5 => surface_of(PolicyMilestone::M5).contains(&class.label()),
                PolicyMilestone::M6 => surface_of(PolicyMilestone::M6).contains(&class.label()),
                PolicyMilestone::M7 => surface_of(PolicyMilestone::M7).contains(&class.label()),
            };
            assert_eq!(
                class.is_authorized_at(*milestone),
                expected,
                "{} at {}",
                class.label(),
                milestone.label()
            );
        }
    }
}

#[test]
fn no_milestones_allowlist_names_an_excluded_or_prohibited_class() {
    // Decision 0089 section 1, as an assertion over every ratified
    // milestone rather than a sentence in a doc comment. The const check in
    // this module refuses the build for the same reason; this one names the
    // offending class and milestone when it goes wrong.
    for milestone in PolicyMilestone::ALL {
        for class in authorized_classes(*milestone) {
            assert!(
                !matches!(
                    class.availability(),
                    ClassAvailability::ExcludedFromRelease | ClassAvailability::Prohibited
                ),
                "{} is on the {} surface at availability {:?}",
                class.label(),
                milestone.label(),
                class.availability()
            );
        }
    }
    // The claim would be vacuous over an empty pair of groups, so both are
    // asserted to be inhabited.
    for availability in [
        ClassAvailability::ExcludedFromRelease,
        ClassAvailability::Prohibited,
    ] {
        assert!(
            ActionClass::ALL
                .iter()
                .any(|class| class.availability() == availability),
            "{availability:?} covers no class"
        );
    }
}

#[test]
fn no_milestone_authorizes_an_unclassified_page_write() {
    let unclassified = [
        ActionClass::SelectOption,
        ActionClass::ToggleControl,
        ActionClass::SubmitForm,
    ];
    for milestone in PolicyMilestone::ALL {
        for class in unclassified {
            assert!(
                !class.is_authorized_at(*milestone),
                "{} is on the {} surface without a closed browser consequence",
                class.label(),
                milestone.label()
            );
        }
        for class in authorized_classes(*milestone) {
            assert!(
                class.page_consequence_is_classified(),
                "{} is on the {} surface without a closed browser consequence",
                class.label(),
                milestone.label()
            );
        }
    }
}

#[test]
fn every_write_milestone_class_is_present_and_refused_until_a_milestone_names_it() {
    for class in ActionClass::WRITE_MILESTONE {
        assert_eq!(class.availability(), ClassAvailability::WriteMilestone);
        assert!(class.mutates());
        for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
            assert!(!class.is_authorized_at(milestone));
        }
        // At M5 the reservation and the allowlist part company: the
        // browser-owned fill and download flows were taken, while select,
        // toggle, submit, and upload remain refused.
        assert_eq!(
            class.is_authorized_at(PolicyMilestone::M5),
            matches!(*class, ActionClass::FillField | ActionClass::StartDownload),
            "{} at M5",
            class.label()
        );
        assert_eq!(
            class.is_authorized_at(PolicyMilestone::M6),
            matches!(*class, ActionClass::FillField | ActionClass::StartDownload),
            "{} at M6",
            class.label()
        );
    }
    for class in [
        ActionClass::SelectOption,
        ActionClass::ToggleControl,
        ActionClass::SubmitForm,
        ActionClass::UploadFile,
    ] {
        for milestone in PolicyMilestone::ALL {
            assert!(!class.is_authorized_at(*milestone), "{}", class.label());
        }
    }
}

#[test]
fn a_class_whose_risk_is_too_high_is_never_on_the_ratified_surface() {
    // The implication runs one way. Risk that cannot be authorized keeps a
    // class off the surface; being off the surface says nothing about risk,
    // because a class can be low-risk and simply not built yet.
    for class in ActionClass::ALL {
        if !class.baseline_risk().can_be_authorized_today() {
            assert!(
                !class.availability().can_be_authorized_today(),
                "{} is on the read-oriented surface at a risk it does not authorize",
                class.label()
            );
        }
    }
    // The same implication, per milestone, which is the form that survives
    // a widening: at M5 a sensitive disclosure is authorizable, while an
    // unclassified submit remains off the surface and an excluded commitment
    // is neither authorizable nor on the surface.
    for class in ActionClass::ALL {
        for milestone in PolicyMilestone::ALL {
            if !class.baseline_risk().can_be_authorized_at(*milestone) {
                assert!(
                    !class.is_authorized_at(*milestone),
                    "{} is on the {} surface at a risk that milestone does not authorize",
                    class.label(),
                    milestone.label()
                );
            }
        }
    }
}

#[test]
fn a_tool_job_is_authorized_only_at_the_computation_milestone() {
    assert!(ActionClass::ExecuteToolJob
        .baseline_risk()
        .can_be_authorized_today());
    assert_eq!(
        ActionClass::ExecuteToolJob.availability(),
        ClassAvailability::ComputationMilestone
    );
    for milestone in PolicyMilestone::ALL {
        assert_eq!(
            ActionClass::ExecuteToolJob.is_authorized_at(*milestone),
            *milestone == PolicyMilestone::M7
        );
    }
}

#[test]
fn the_enumeration_and_its_list_agree_about_every_class() {
    assert_eq!(ActionClass::ALL.len(), ActionClass::COUNT);
    for (index, class) in ActionClass::ALL.iter().enumerate() {
        assert_eq!(class.ordinal(), index, "{}", class.label());
    }
}
