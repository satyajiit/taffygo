// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The field-class table and the fill verdicts it produces, row by row.

use super::{
    fill_verdict, per_use_confirmation_required, FieldClassAllowance, FillRefusal, FillVerdict,
};

use crate::action_class::{ActionClass, ClassAvailability, PolicyMilestone};

use bip_types::sensitivity::{DestinationPolicy, Handling, NeverExtractClass, SensitivitySet};

use bip_types::snapshot::Sensitivity;

/// The whole table, written out, in the order the protocol schema declares
/// the classifications. A row changed without changing this list fails
/// here, which is the point: a table with no test is a table somebody edits
/// without knowing which rows are load-bearing.
const TABLE: &[(Sensitivity, FieldClassAllowance)] = &[
    (
        Sensitivity::NotSensitive,
        FieldClassAllowance::PreparedCommit,
    ),
    (Sensitivity::Personal, FieldClassAllowance::PreparedCommit),
    (Sensitivity::Account, FieldClassAllowance::PreparedCommit),
    (
        Sensitivity::Payment,
        FieldClassAllowance::CompletedByThePerson,
    ),
    (
        Sensitivity::Identity,
        FieldClassAllowance::PreparedCommitAndPerUseConfirmation,
    ),
    (
        Sensitivity::Health,
        FieldClassAllowance::CompletedByThePerson,
    ),
    (
        Sensitivity::Financial,
        FieldClassAllowance::CompletedByThePerson,
    ),
    (
        Sensitivity::Legal,
        FieldClassAllowance::CompletedByThePerson,
    ),
    (
        Sensitivity::PrivateCommunication,
        FieldClassAllowance::CompletedByThePerson,
    ),
    (
        Sensitivity::Administration,
        FieldClassAllowance::CompletedByThePerson,
    ),
    (Sensitivity::Credential, FieldClassAllowance::NeverFillable),
    (
        Sensitivity::UnknownSensitive,
        FieldClassAllowance::CompletedByThePerson,
    ),
    (
        Sensitivity::OneTimeCode,
        FieldClassAllowance::PreparedCommitAndPerUseConfirmation,
    ),
    (
        Sensitivity::ChallengeResponse,
        FieldClassAllowance::PreparedCommitAndPerUseConfirmation,
    ),
];

#[test]
fn the_table_covers_every_classification_exactly_once_and_says_what_it_says() {
    let covered: Vec<Sensitivity> = TABLE.iter().map(|(class, _)| *class).collect();
    assert_eq!(covered, Sensitivity::ALL.to_vec());
    for (class, expected) in TABLE {
        assert_eq!(
            FieldClassAllowance::of(*class),
            *expected,
            "{}",
            class.wire()
        );
    }
}

#[test]
fn exactly_the_three_per_use_classes_require_confirmation() {
    // The narrowing this record exists for, stated as a boundary rather
    // than as a row: exactly one class asks for a confirmation each time.
    let per_use: Vec<&str> = Sensitivity::ALL
        .iter()
        .filter(|class| FieldClassAllowance::of(**class).requires_per_use_confirmation())
        .map(|class| class.wire())
        .collect();
    assert_eq!(
        per_use,
        vec!["IDENTITY", "ONE_TIME_CODE", "CHALLENGE_RESPONSE"]
    );
}

#[test]
fn health_payment_financial_and_legal_are_closed() {
    for class in [
        Sensitivity::Health,
        Sensitivity::Payment,
        Sensitivity::Financial,
        Sensitivity::Legal,
        Sensitivity::PrivateCommunication,
        Sensitivity::Administration,
    ] {
        let allowance = FieldClassAllowance::of(class);
        assert_eq!(allowance, FieldClassAllowance::CompletedByThePerson);
        assert!(!allowance.taffy_may_carry_the_value(), "{}", class.wire());
        assert!(FieldClassAllowance::clearance_for(class).is_none());
    }
}

#[test]
fn a_credential_has_no_clearance_and_reaches_no_destination() {
    // The two halves of the permanent invariant, asserted together: never
    // fillable, and never in the data plane whatever the destination.
    assert_eq!(
        FieldClassAllowance::of(Sensitivity::Credential),
        FieldClassAllowance::NeverFillable
    );
    assert!(FieldClassAllowance::clearance_for(Sensitivity::Credential).is_none());

    for class in NeverExtractClass::ALL {
        for destination in [
            DestinationPolicy::LocalCoreService,
            DestinationPolicy::LocalRecord,
            DestinationPolicy::RemoteModel,
            DestinationPolicy::Telemetry,
        ] {
            assert_eq!(class.handling_for(destination), Handling::Withhold);
        }
        if matches!(
            class,
            NeverExtractClass::OneTimeCode | NeverExtractClass::ChallengeResponse
        ) {
            assert_eq!(
                FieldClassAllowance::of_never_extract(*class),
                FieldClassAllowance::PreparedCommitAndPerUseConfirmation
            );
            assert!(FieldClassAllowance::clearance_for(class.sensitivity()).is_some());
        } else {
            assert_eq!(
                FieldClassAllowance::of_never_extract(*class),
                FieldClassAllowance::NeverFillable
            );
            assert!(FieldClassAllowance::clearance_for(class.sensitivity()).is_none());
        }
    }
}

#[test]
fn a_never_extract_class_is_refused_permanently_at_every_milestone() {
    for milestone in PolicyMilestone::ALL {
        for class in NeverExtractClass::ALL.iter().filter(|class| {
            !matches!(
                class,
                NeverExtractClass::OneTimeCode | NeverExtractClass::ChallengeResponse
            )
        }) {
            let verdict = fill_verdict(class.sensitivity(), *milestone);
            assert_eq!(
                verdict,
                FillVerdict::Refused(FillRefusal::ClassNeverFillable)
            );
            let FillVerdict::Refused(refusal) = verdict else {
                unreachable!("just asserted refused")
            };
            assert!(!refusal.a_later_milestone_could_lift_this());
        }
    }
}

#[test]
fn an_unclassified_field_is_handled_at_least_as_strictly_as_an_identity_one() {
    // The fail-safe, stated as an order rather than as a row: whatever the
    // renderer's vocabulary misses lands here, and here is stricter.
    let unknown = FieldClassAllowance::of(Sensitivity::UnknownSensitive);
    let identity = FieldClassAllowance::of(Sensitivity::Identity);
    assert!(unknown > identity);
    assert!(!unknown.taffy_may_carry_the_value());
    assert!(FieldClassAllowance::clearance_for(Sensitivity::UnknownSensitive).is_none());
}

#[test]
fn filling_an_identity_field_opens_only_on_the_browser_bound_write_surface() {
    for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
        assert_eq!(
            fill_verdict(Sensitivity::Identity, milestone),
            FillVerdict::Refused(FillRefusal::WriteNotAuthorizedAtThisMilestone),
            "{}",
            milestone.label()
        );
    }
    for milestone in [
        PolicyMilestone::M5,
        PolicyMilestone::M6,
        PolicyMilestone::M7,
    ] {
        assert!(matches!(
            fill_verdict(Sensitivity::Identity, milestone),
            FillVerdict::Cleared(_)
        ));
    }
    let clearance = FieldClassAllowance::clearance_for(Sensitivity::Identity)
        .unwrap_or_else(|| unreachable!("identity retains its future field clearance"));
    assert_eq!(clearance.class(), Sensitivity::Identity);
    assert_eq!(
        clearance.allowance(),
        FieldClassAllowance::PreparedCommitAndPerUseConfirmation
    );
    assert!(clearance.requires_per_use_confirmation());
    assert!(per_use_confirmation_required(
        ActionClass::FillField,
        SensitivitySet::of(Sensitivity::Identity)
    ));
}

#[test]
fn only_cleared_field_classes_are_fillable_from_m5() {
    assert_eq!(
        ActionClass::FillField.availability(),
        ClassAvailability::WriteMilestone
    );
    for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
        for class in Sensitivity::ALL {
            let verdict = fill_verdict(*class, milestone);
            assert!(
                !verdict.is_cleared(),
                "{} at {}",
                class.wire(),
                milestone.label()
            );
        }
    }

    for milestone in [
        PolicyMilestone::M5,
        PolicyMilestone::M6,
        PolicyMilestone::M7,
    ] {
        for class in Sensitivity::ALL {
            assert_eq!(
                fill_verdict(*class, milestone).is_cleared(),
                FieldClassAllowance::clearance_for(*class).is_some(),
                "{} at {}",
                class.wire(),
                milestone.label()
            );
        }
    }
}

#[test]
fn a_fill_of_a_per_use_class_is_asked_about_and_a_stricter_join_does_not_take_that_back() {
    // The predicate both deciders read. Ordinary classes need no question
    // of their own; identity does; and a set that drew identity plus
    // something stricter still does, because the order is what is read
    // rather than an equality against the per-use value.
    for class in [
        Sensitivity::NotSensitive,
        Sensitivity::Personal,
        Sensitivity::Account,
    ] {
        assert!(
            !per_use_confirmation_required(ActionClass::FillField, SensitivitySet::of(class)),
            "{}",
            class.wire()
        );
    }
    for class in Sensitivity::ALL {
        let joined = SensitivitySet::of(Sensitivity::Identity).with(*class);
        assert!(
            per_use_confirmation_required(ActionClass::FillField, joined),
            "identity joined with {}",
            class.wire()
        );
    }
    // Payment, credential and an unclassified field are asked about too:
    // everything stricter than the ordinary answer is.
    for class in [
        Sensitivity::Payment,
        Sensitivity::Credential,
        Sensitivity::UnknownSensitive,
    ] {
        assert!(
            per_use_confirmation_required(ActionClass::FillField, SensitivitySet::of(class)),
            "{}",
            class.wire()
        );
    }
    // And it is a question about a fill. Nothing else is a fill, so nothing
    // else answers this question — the classes beside it on the M5 surface
    // are covered by the risk lattice's exact-approval rule instead.
    for class in ActionClass::ALL {
        if *class != ActionClass::FillField {
            assert!(
                !per_use_confirmation_required(*class, SensitivitySet::of(Sensitivity::Identity)),
                "{}",
                class.label()
            );
        }
    }
}

#[test]
fn a_permanent_refusal_is_never_reported_as_a_milestone_one() {
    for milestone in PolicyMilestone::ALL {
        assert_eq!(
            fill_verdict(Sensitivity::Credential, *milestone),
            FillVerdict::Refused(FillRefusal::ClassNeverFillable)
        );
        assert_eq!(
            fill_verdict(Sensitivity::Health, *milestone),
            FillVerdict::Refused(FillRefusal::ClassMustBeCompletedByThePerson)
        );
    }
}

#[test]
fn a_field_that_drew_two_classes_takes_the_stricter_answer() {
    // The join has to agree with the table, or a field classified both
    // identity and payment would take whichever member was read first.
    let identity_and_payment = SensitivitySet::of(Sensitivity::Identity).with(Sensitivity::Payment);
    assert_eq!(
        FieldClassAllowance::of_set(identity_and_payment),
        FieldClassAllowance::CompletedByThePerson
    );

    let identity_and_account = SensitivitySet::of(Sensitivity::Identity).with(Sensitivity::Account);
    assert_eq!(
        FieldClassAllowance::of_set(identity_and_account),
        FieldClassAllowance::PreparedCommitAndPerUseConfirmation
    );

    // Anything joined with credential is never fillable, whatever else it
    // drew and whichever order the members arrive in.
    for class in Sensitivity::ALL {
        let with_credential = SensitivitySet::of(*class).with(Sensitivity::Credential);
        assert_eq!(
            FieldClassAllowance::of_set(with_credential),
            FieldClassAllowance::NeverFillable,
            "{}",
            class.wire()
        );
    }

    // An empty element is exactly the ordinary answer.
    assert_eq!(
        FieldClassAllowance::of_set(SensitivitySet::EMPTY),
        FieldClassAllowance::PreparedCommit
    );
}

#[test]
fn the_allowances_order_from_permissive_to_strict_and_name_themselves() {
    assert!(
        FieldClassAllowance::PreparedCommit
            < FieldClassAllowance::PreparedCommitAndPerUseConfirmation
    );
    assert!(
        FieldClassAllowance::PreparedCommitAndPerUseConfirmation
            < FieldClassAllowance::CompletedByThePerson
    );
    assert!(FieldClassAllowance::CompletedByThePerson < FieldClassAllowance::NeverFillable);

    let mut labels: Vec<&str> = FieldClassAllowance::ALL
        .iter()
        .map(|allowance| allowance.label())
        .collect();
    let count = labels.len();
    labels.sort_unstable();
    labels.dedup();
    assert_eq!(labels.len(), count);

    let mut refusals: Vec<&str> = FillRefusal::ALL
        .iter()
        .map(|reason| reason.label())
        .collect();
    let refusal_count = refusals.len();
    refusals.sort_unstable();
    refusals.dedup();
    assert_eq!(refusals.len(), refusal_count);
}
