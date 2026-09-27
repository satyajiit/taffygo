// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Properties of the sensitivity lattice and the result taxonomy
//! (specification sections 9 and 11.7).
//!
//! The lattice laws are worth generating rather than asserting because the
//! safety claim rests on them: if `join` were not a least upper bound, evidence
//! from a page could combine into a *lower* classification than one of its
//! inputs, which is exactly the "page instructions cannot lower sensitivity"
//! rule failing.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::result_code::{ActionResultCode, SideEffectCertainty};
use bip_types::sensitivity::{
    DestinationPolicy, Handling, NeverExtractClass, Sensitivity, SensitivitySet, NAMED_SENSITIVE,
};
use proptest::prelude::*;

const DESTINATIONS: &[DestinationPolicy] = &[
    DestinationPolicy::LocalCoreService,
    DestinationPolicy::LocalRecord,
    DestinationPolicy::RemoteModel,
    DestinationPolicy::Telemetry,
];

fn classification() -> impl Strategy<Value = Sensitivity> {
    prop::sample::select(Sensitivity::ALL.to_vec())
}

fn lattice_element() -> impl Strategy<Value = SensitivitySet> {
    prop::collection::vec(classification(), 0..6).prop_map(SensitivitySet::from_iter)
}

fn destination() -> impl Strategy<Value = DestinationPolicy> {
    prop::sample::select(DESTINATIONS.to_vec())
}

proptest! {
    /// `join` is a least upper bound: commutative, associative, idempotent, and
    /// never below either input.
    #[test]
    fn join_is_a_least_upper_bound(
        a in lattice_element(),
        b in lattice_element(),
        c in lattice_element(),
    ) {
        prop_assert_eq!(a.join(b), b.join(a));
        prop_assert_eq!(a.join(b).join(c), a.join(b.join(c)));
        prop_assert_eq!(a.join(a), a);
        prop_assert_eq!(a.join(SensitivitySet::EMPTY), a);

        prop_assert!(a.join(b).is_at_least_as_strict_as(a));
        prop_assert!(a.join(b).is_at_least_as_strict_as(b));
    }

    /// Adding a classification never lowers the handling a value requires, at
    /// any destination. This is the executable form of "page content can never
    /// lower a classification".
    #[test]
    fn adding_evidence_never_lowers_handling(
        element in lattice_element(),
        added in classification(),
        destination in destination(),
    ) {
        let before = element.handling_for(destination);
        let after = element.with(added).handling_for(destination);
        prop_assert!(after >= before);
    }

    /// The partial order agrees with handling at every destination: a stricter
    /// element never asks for weaker handling.
    #[test]
    fn the_order_agrees_with_handling(
        a in lattice_element(),
        b in lattice_element(),
        destination in destination(),
    ) {
        if a.is_at_least_as_strict_as(b) {
            prop_assert!(a.handling_for(destination) >= b.handling_for(destination));
        }
    }

    /// An unclassified value is handled at least as strictly as any class the
    /// destination can accept (section 9.2). It is never resolved to the least
    /// restrictive member.
    #[test]
    fn unknown_sensitive_is_never_the_least_restrictive_choice(
        named in prop::sample::select(NAMED_SENSITIVE.to_vec()),
        destination in destination(),
    ) {
        let unknown = Sensitivity::UnknownSensitive.handling_for(destination);
        prop_assert!(unknown >= named.handling_for(destination));
        prop_assert!(unknown >= Sensitivity::NotSensitive.handling_for(destination));
    }

    /// A never-extract classification withholds the value everywhere, and
    /// nothing joined onto it can relax that.
    #[test]
    fn a_credential_is_withheld_at_every_destination(
        other in lattice_element(),
        destination in destination(),
    ) {
        let element = other.with(Sensitivity::Credential);
        prop_assert!(element.is_never_extract());
        prop_assert_eq!(element.handling_for(destination), Handling::Withhold);
        prop_assert!(element.handling_for(destination).withholds());
    }

    /// Membership survives the round trip through the set, and the empty set
    /// means exactly "not sensitive".
    #[test]
    fn membership_is_faithful(classifications in prop::collection::vec(classification(), 0..8)) {
        let element: SensitivitySet = classifications.iter().copied().collect();

        // Every classification that names something is retained. NotSensitive
        // names nothing: it is the bottom of the lattice, so it adds no member
        // and asking whether the set contains it asks whether the set is empty.
        for classification in &classifications {
            if *classification == Sensitivity::NotSensitive {
                continue;
            }
            prop_assert!(element.contains(*classification));
        }

        let only_not_sensitive = classifications
            .iter()
            .all(|c| *c == Sensitivity::NotSensitive);
        prop_assert_eq!(element.is_empty(), only_not_sensitive);
        prop_assert_eq!(element.contains(Sensitivity::NotSensitive), only_not_sensitive);

        let members = element.members();
        prop_assert_eq!(members.len(), members.iter().collect::<std::collections::HashSet<_>>().len());
        for member in members {
            prop_assert!(classifications.contains(&member));
        }
    }

    /// Only `VERIFIED` may be read as success, whatever the code.
    #[test]
    fn every_result_code_except_verified_fails_closed(
        index in 0usize..ActionResultCode::ALL.len(),
    ) {
        let code = ActionResultCode::ALL[index];
        prop_assert_eq!(code.fails_closed(), code != ActionResultCode::Verified);
        prop_assert_eq!(code.awaits_further_outcome(), !code.is_terminal());

        // A code that failed closed never claims the effect happened.
        if code.fails_closed() {
            prop_assert_ne!(code.side_effect(), SideEffectCertainty::Performed);
        }

        // A stale handle is always a refusal that touched nothing, so a fresh
        // observation is safe to take (section 12).
        if code.is_stale_handle() {
            prop_assert_eq!(code.side_effect(), SideEffectCertainty::NotPerformed);
            prop_assert!(code.fails_closed());
            prop_assert!(code.is_terminal());
        }
    }
}

#[test]
fn every_never_extract_class_is_withheld_and_says_nothing_about_the_value() {
    for class in NeverExtractClass::ALL {
        let expected = match class {
            NeverExtractClass::OneTimeCode => Sensitivity::OneTimeCode,
            NeverExtractClass::ChallengeResponse => Sensitivity::ChallengeResponse,
            NeverExtractClass::Password
            | NeverExtractClass::Passcode
            | NeverExtractClass::PersonalIdentificationNumber
            | NeverExtractClass::CardVerificationValue
            | NeverExtractClass::RecoveryCode
            | NeverExtractClass::Passkey
            | NeverExtractClass::PrivateKey
            | NeverExtractClass::SeedPhrase
            | NeverExtractClass::AuthenticationToken
            | NeverExtractClass::SessionCookie
            | NeverExtractClass::PasswordManagerSuggestion
            | NeverExtractClass::PlatformCredentialPayload => Sensitivity::Credential,
        };
        assert_eq!(class.sensitivity(), expected);
        assert!(class.sensitivity_set().is_never_extract());

        for destination in DESTINATIONS {
            assert_eq!(class.handling_for(*destination), Handling::Withhold);
            assert_eq!(
                class.sensitivity_set().handling_for(*destination),
                Handling::Withhold
            );
        }

        // The placeholder says a field is present and nothing more: no value,
        // no suggestion, and no digit a length could be read from.
        let placeholder = class.structural_placeholder();
        assert!(!placeholder.is_empty());
        assert!(
            !placeholder
                .chars()
                .any(|character| character.is_ascii_digit()),
            "{placeholder} carries a number, which could fingerprint the secret"
        );
    }
}

#[test]
fn the_local_observation_and_the_remote_projection_are_different_answers() {
    // Section 9.3: the local core projection is rich while the remote
    // projection is minimized. If these ever agreed for a sensitive class,
    // serializing the whole local snapshot to a provider would look correct.
    let element = SensitivitySet::of(Sensitivity::Payment);
    assert_eq!(
        element.handling_for(DestinationPolicy::LocalCoreService),
        Handling::Allowed
    );
    assert_eq!(
        element.handling_for(DestinationPolicy::RemoteModel),
        Handling::MinimizeAndRedact
    );
    assert_eq!(
        element.handling_for(DestinationPolicy::Telemetry),
        Handling::Withhold
    );
}

#[test]
fn no_observed_value_reaches_telemetry() {
    // Section 16 keeps page-derived values out of product analytics entirely,
    // including values nothing classified as sensitive.
    for classification in Sensitivity::ALL {
        assert_eq!(
            classification.handling_for(DestinationPolicy::Telemetry),
            Handling::Withhold
        );
    }
    assert_eq!(
        SensitivitySet::EMPTY.handling_for(DestinationPolicy::Telemetry),
        Handling::Withhold
    );
}

#[test]
fn the_result_taxonomy_is_classified_completely() {
    let codes = ActionResultCode::ALL;
    assert_eq!(
        codes.iter().filter(|code| !code.fails_closed()).count(),
        1,
        "exactly one code may be read as success"
    );
    assert_eq!(
        codes
            .iter()
            .filter(|code| code.side_effect() == SideEffectCertainty::Performed)
            .count(),
        1,
        "only a verified action has a confirmed effect"
    );
    assert_eq!(
        codes.iter().filter(|code| code.is_stale_handle()).count(),
        3,
        "section 12 names exactly three stale-handle outcomes"
    );
    assert!(
        codes.iter().filter(|code| !code.is_terminal()).count() < codes.len(),
        "some outcome must end the action"
    );
    for code in codes {
        // Every code has a side-effect reading; the match is exhaustive, so
        // this fails to compile rather than to run if one is forgotten.
        let _ = code.side_effect();
    }
}
