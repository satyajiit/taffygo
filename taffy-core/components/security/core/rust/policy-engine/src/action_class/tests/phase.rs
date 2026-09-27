// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The phase table: a prepare is never worth more than the commit it stages.

use crate::action_class::{
    authorized_classes, ActionClass, ActionPhase, ClassAvailability, PolicyMilestone,
};

#[test]
fn the_committed_column_of_the_phase_table_is_the_reading_the_crate_had_before() {
    // The phase table was introduced under a class-by-class reading that
    // already existed. If the commit column ever stops being that reading,
    // the phase will have lowered something for a single-step action, which
    // is the one thing decision 0022 refuses to allow.
    for class in ActionClass::ALL {
        assert_eq!(
            class.baseline_risk(),
            class.baseline_risk_in(ActionPhase::Commit),
            "{}",
            class.label()
        );
    }
}

#[test]
fn a_prepare_is_never_worth_more_than_the_commit_it_stages_and_never_less_than_nothing() {
    for class in ActionClass::ALL {
        let prepared = class.baseline_risk_in(ActionPhase::Prepare);
        let committed = class.baseline_risk_in(ActionPhase::Commit);
        assert!(prepared <= committed, "{}", class.label());
        // Preparing an excluded or prohibited class still lands where the
        // read-oriented surface cannot authorize it. This is decision
        // 0022's own consequence: the phase widens nothing.
        if !committed.can_be_authorized_today() {
            assert!(
                !prepared.can_be_authorized_today(),
                "preparing {} became authorizable",
                class.label()
            );
        }
        // The same claim at every ratified milestone, and it has to name
        // the other gate to stay true. `SendMessage` and `Purchase` stage
        // as a draft — a sensitive disclosure — and commit as an excluded
        // commitment, so from M5 the risk lattice by itself stops refusing
        // a *prepare* of one: M5 authorizes a sensitive disclosure. Nothing
        // became reachable, because no allowlist names either class and the
        // const assertion in this module is what keeps it that way. Stating
        // it here is what makes that dependence visible instead of the
        // property quietly ceasing to hold.
        for milestone in PolicyMilestone::ALL {
            if prepared.can_be_authorized_at(*milestone)
                && !committed.can_be_authorized_at(*milestone)
            {
                assert!(
                    !class.is_authorized_at(*milestone),
                    "preparing {} outruns committing it at {} and the class surface does not refuse it",
                    class.label(),
                    milestone.label()
                );
            }
        }
    }
}

#[test]
fn exactly_the_classes_a_person_is_asked_about_are_the_two_phase_ones() {
    let two_phase: Vec<&str> = ActionClass::ALL
        .iter()
        .filter(|class| class.is_two_phase())
        .map(|class| class.label())
        .collect();
    assert_eq!(
        two_phase,
        vec![
            "fill_field",
            "select_option",
            "toggle_control",
            "submit_form",
            "start_download",
            "upload_file",
            "send_message",
            "purchase",
            "extract_credential",
            "bypass_access_control",
            "library_write",
            "memory_write"
        ]
    );
    // Nothing on the read-oriented surface is two-phase, which is why the
    // mechanism was inert for as long as that was the whole surface.
    for class in ActionClass::ALL {
        if class.availability() == ClassAvailability::ReadOriented {
            assert!(!class.is_two_phase(), "{}", class.label());
        }
    }
    // M5 is where it stops being inert for the two classified writes: exact
    // field fill and download initiation remain prepared, shown and
    // committed. The remaining form mutations do not reach this branch.
    for class in authorized_classes(PolicyMilestone::M5) {
        if class.availability() == ClassAvailability::WriteMilestone {
            assert!(class.is_two_phase(), "{}", class.label());
        }
    }
}
