// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The four destinations really are strictly ordered.
//!
//! Protocol specification section 9 and data and privacy sections 7 and 14 say
//! the local observation, the model projection, the audit record, and the
//! telemetry record each carry less than the one before. This suite runs the
//! whole pipeline over a corpus of node shapes and over generated observations,
//! and proves the ordering rather than restating the field table that
//! implements it.
//!
//! It also proves the checker itself fails: a set assembled by hand with a
//! narrower projection carrying more than a wider one is rejected. A checker
//! that has only ever seen correct input has not been checked.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::identity::SemanticNodeId;
use bip_types::sensitivity::Handling;
use bip_types::snapshot::{NodeState, SemanticRole, Sensitivity};
use proptest::prelude::*;

use policy_engine::pipeline::{project_all, DestinationSet, NarrowingBreach};
use policy_engine::redaction::{
    classify_zone, redact_classified, AutocompleteSignal, AutocompleteToken, ContentField,
    InputType, InputTypeSignal, Projection,
};
use policy_engine::{FieldObservation, RedactionDestination};

fn node(role: SemanticRole) -> FieldObservation {
    FieldObservation::new(SemanticNodeId::new("n_1"), role)
}

/// Node shapes that between them exercise every branch of the field table.
fn corpus() -> Vec<(&'static str, FieldObservation)> {
    let mut cases = Vec::new();

    let mut ordinary = node(SemanticRole::Link);
    ordinary.name = Some("Your orders".to_owned());
    ordinary.description = Some("Opens the order history".to_owned());
    ordinary.text_runs = vec!["Order history".to_owned()];
    ordinary.destination_url = Some("https://example.test/orders?page=2#top".to_owned());
    ordinary.states = vec![NodeState::Visible, NodeState::Enabled];
    cases.push(("ordinary link with a query string", ordinary));

    let mut password = node(SemanticRole::TextField);
    password.name = Some("Password".to_owned());
    password.signals.input_type = InputTypeSignal::Known(InputType::Password);
    password.value = Some("hunter2-not-a-real-secret".to_owned());
    cases.push(("password control", password));

    let mut payment = node(SemanticRole::TextField);
    payment.name = Some("Card number".to_owned());
    payment.signals.autocomplete = AutocompleteSignal::Known(AutocompleteToken::CardNumber);
    payment.value = Some("4111111111111111".to_owned());
    cases.push(("payment field", payment));

    let mut bare = node(SemanticRole::Paragraph);
    bare.text_runs = vec!["Two hundred lumens.".to_owned()];
    cases.push(("paragraph with nothing but text", bare));

    let mut empty = node(SemanticRole::Heading);
    empty.states = vec![NodeState::Visible];
    cases.push(("node with no content at all", empty));

    let mut declared_safe = node(SemanticRole::TextField);
    declared_safe.name = Some("Enter the code we sent you".to_owned());
    declared_safe.declared_sensitivity = Sensitivity::NotSensitive;
    declared_safe.signals.autocomplete = AutocompleteSignal::Known(AutocompleteToken::OneTimeCode);
    declared_safe.value = Some("449182".to_owned());
    cases.push(("one-time code the page called ordinary", declared_safe));

    let mut cross_origin = node(SemanticRole::TextField);
    cross_origin.name = Some("Card number".to_owned());
    cross_origin.signals.cross_origin_embedded = true;
    cross_origin.value = Some("5555444433332226".to_owned());
    cases.push(("payment field in a cross-origin frame", cross_origin));

    cases
}

#[test]
fn every_node_shape_narrows_strictly_at_every_step() {
    for (name, observation) in corpus() {
        let set = project_all(&observation);
        assert_eq!(set.verify_narrowing(), Ok(()), "case: {name}");
    }
}

#[test]
fn the_field_sets_nest_strictly_from_the_local_observation_to_telemetry() {
    // Written out rather than derived, because this is the ordering the whole
    // pipeline rests on and it deserves to be readable.
    let expected: [(RedactionDestination, usize); 4] = [
        (RedactionDestination::LocalContext, ContentField::ALL.len()),
        (RedactionDestination::ModelProjection, 6),
        (RedactionDestination::Audit, 1),
        (RedactionDestination::Telemetry, 0),
    ];
    for (destination, count) in expected {
        assert_eq!(
            destination.permitted_content().len(),
            count,
            "{}",
            destination.label()
        );
    }

    let mut previous: Option<RedactionDestination> = None;
    for destination in RedactionDestination::ALL {
        if let Some(wider) = previous {
            let mut dropped = 0;
            for field in ContentField::ALL {
                assert!(
                    wider.permits_content(*field) || !destination.permits_content(*field),
                    "{} carries {} and {} does not",
                    destination.label(),
                    field.label(),
                    wider.label()
                );
                if wider.permits_content(*field) && !destination.permits_content(*field) {
                    dropped += 1;
                }
            }
            assert!(
                dropped > 0,
                "{} drops nothing that {} carries",
                destination.label(),
                wider.label()
            );
        }
        previous = Some(*destination);
    }
}

#[test]
fn the_full_destination_url_reaches_no_destination_but_the_local_one() {
    for destination in RedactionDestination::ALL {
        assert_eq!(
            destination.permits_content(ContentField::DestinationUrl),
            *destination == RedactionDestination::LocalContext,
            "{}",
            destination.label()
        );
    }

    let mut observation = node(SemanticRole::Link);
    observation.destination_url = Some("https://example.test/orders?session=abc123#top".to_owned());
    let set = project_all(&observation);
    let model = set.model().expect("the model projection is built");
    assert_eq!(
        model.destination.as_deref(),
        Some("https://example.test/orders")
    );
    let audit = set.audit().expect("the audit projection is built");
    assert_eq!(
        audit.destination_origin.as_deref(),
        Some("https://example.test")
    );
    assert!(set
        .fragments(RedactionDestination::Telemetry)
        .next()
        .is_none());
}

#[test]
fn the_classification_is_the_same_whoever_is_asking() {
    for (name, observation) in corpus() {
        let set = project_all(&observation);
        let decided = set.classification().sensitivity();
        for destination in RedactionDestination::ALL {
            assert_eq!(
                set.projection(*destination).sensitivity(),
                decided,
                "case {name} at {}",
                destination.label()
            );
        }
    }
}

#[test]
fn the_handling_verdict_tightens_in_the_same_direction_as_the_field_table() {
    for (name, observation) in corpus() {
        let classification = classify_zone(&observation);
        let mut previous: Option<Handling> = None;
        for destination in RedactionDestination::ALL {
            let handling = classification.handling_for(*destination);
            if let Some(previous) = previous {
                assert!(
                    handling >= previous,
                    "case {name}: {} is more permissive than the destination before it",
                    destination.label()
                );
            }
            previous = Some(handling);
        }
    }
}

#[test]
fn a_narrower_projection_that_carried_more_than_the_wider_one_is_rejected() {
    // Everything the model projection carries is derived from the observation,
    // so nothing is invented — there is simply more of it than the destination
    // before it carried. That is still a widening.
    let mut observation = node(SemanticRole::Link);
    observation.text_runs = vec!["example".to_owned()];
    observation.destination_url = Some("https://example.test".to_owned());
    let classification = classify_zone(&observation);

    let local = redact_classified(&observation, &classification, RedactionDestination::Audit);
    let model = redact_classified(
        &observation,
        &classification,
        RedactionDestination::ModelProjection,
    );
    let audit = redact_classified(&observation, &classification, RedactionDestination::Audit);
    let telemetry = redact_classified(
        &observation,
        &classification,
        RedactionDestination::Telemetry,
    );

    let broken = DestinationSet::from_projections(classification, local, model, audit, telemetry);
    assert_eq!(
        broken.verify_narrowing(),
        Err(NarrowingBreach::ContentGrew {
            wider: RedactionDestination::LocalContext,
            narrower: RedactionDestination::ModelProjection,
        })
    );
}

#[test]
fn a_normalized_destination_is_a_removal_and_not_an_invention() {
    // The model projection lowercases the host and drops the default port, so
    // the string it carries is not a substring of the observed URL. It is still
    // derived from it, and the checker has to say so.
    let mut observation = node(SemanticRole::Link);
    observation.destination_url = Some("https://Example.test:443/orders".to_owned());
    let set = project_all(&observation);
    assert_eq!(set.verify_narrowing(), Ok(()));
    let model = set.model().expect("the model projection is built");
    assert_eq!(
        model.destination.as_deref(),
        Some("https://example.test/orders")
    );
}

#[test]
fn a_projection_that_invented_content_is_rejected() {
    // A "local" observation with nothing in it, and a model projection built
    // from a node that had content: the narrower destination is carrying spans
    // the wider one never held.
    let bare = node(SemanticRole::Link);
    let mut rich = node(SemanticRole::Link);
    rich.name = Some("Your orders".to_owned());
    rich.text_runs = vec!["Order history".to_owned()];
    let classification = classify_zone(&rich);

    let local = redact_classified(&bare, &classification, RedactionDestination::LocalContext);
    let model = redact_classified(
        &rich,
        &classification,
        RedactionDestination::ModelProjection,
    );
    let audit = redact_classified(&bare, &classification, RedactionDestination::Audit);
    let telemetry = redact_classified(&bare, &classification, RedactionDestination::Telemetry);

    let broken = DestinationSet::from_projections(classification, local, model, audit, telemetry);
    assert_eq!(
        broken.verify_narrowing(),
        Err(NarrowingBreach::ContentInvented {
            destination: RedactionDestination::ModelProjection,
        })
    );
}

#[test]
fn a_telemetry_projection_that_carried_a_string_is_rejected() {
    // Telemetry has no field a string can travel in, so the only way to build
    // this set is to put another destination's projection where it belongs —
    // which is exactly what the check has to catch. The node carries only a
    // destination, so the counts still shrink and this is the check that fires.
    let mut observation = node(SemanticRole::Link);
    observation.destination_url = Some("https://example.test".to_owned());
    let classification = classify_zone(&observation);

    let local = redact_classified(
        &observation,
        &classification,
        RedactionDestination::LocalContext,
    );
    let model = redact_classified(
        &observation,
        &classification,
        RedactionDestination::ModelProjection,
    );
    let audit = redact_classified(&observation, &classification, RedactionDestination::Audit);
    let telemetry = redact_classified(
        &observation,
        &classification,
        RedactionDestination::ModelProjection,
    );

    let broken = DestinationSet::from_projections(classification, local, model, audit, telemetry);
    assert_eq!(
        broken.verify_narrowing(),
        Err(NarrowingBreach::TelemetryCarriesContent)
    );
}

// `HandlingRelaxed` and `FieldSetNotStrict` have no case of their own here on
// purpose. Both are guards against a future change: the sensitivity lattice in
// `bip-types` is monotone by construction, and the field table drops at least
// one field at every step, so neither can be provoked without first changing
// the thing it guards. They exist so that change fails a test rather than
// passing quietly.
#[test]
fn every_breach_has_a_compiled_in_name() {
    let breaches = [
        NarrowingBreach::FieldSetWidened {
            wider: RedactionDestination::LocalContext,
            narrower: RedactionDestination::Audit,
            field: ContentField::Name,
        },
        NarrowingBreach::FieldSetNotStrict {
            wider: RedactionDestination::LocalContext,
            narrower: RedactionDestination::Audit,
        },
        NarrowingBreach::ContentGrew {
            wider: RedactionDestination::LocalContext,
            narrower: RedactionDestination::Audit,
        },
        NarrowingBreach::ContentInvented {
            destination: RedactionDestination::Audit,
        },
        NarrowingBreach::HandlingRelaxed {
            wider: RedactionDestination::LocalContext,
            narrower: RedactionDestination::Audit,
        },
        NarrowingBreach::TelemetryCarriesContent,
    ];
    let mut labels: Vec<&str> = breaches.iter().map(|breach| breach.label()).collect();
    let count = labels.len();
    labels.sort_unstable();
    labels.dedup();
    assert_eq!(labels.len(), count);
}

// ---------------------------------------------------------------------------
// Generated observations.
// ---------------------------------------------------------------------------

/// Text a generated observation can carry, including a value that looks like a
/// secret so the masker has something to do.
fn text() -> impl Strategy<Value = String> {
    prop_oneof![
        Just("Your orders".to_owned()),
        Just("sk-live-9f2ba71c4d8e6503".to_owned()),
        Just("Security code".to_owned()),
        Just(String::new()),
        "[a-zA-Z0-9 ]{0,40}",
    ]
}

fn observation() -> impl Strategy<Value = FieldObservation> {
    (
        prop::option::of(text()),
        prop::option::of(text()),
        prop::collection::vec(text(), 0..3),
        prop::option::of(text()),
        prop::option::of(prop_oneof![
            Just("https://example.test/a?b=c#d".to_owned()),
            Just("https://Example.test:443/orders".to_owned()),
            Just("not a url".to_owned()),
        ]),
        prop::sample::select(Sensitivity::ALL.to_vec()),
        0usize..InputType::ALL.len(),
        any::<bool>(),
    )
        .prop_map(
            |(name, description, text_runs, value, url, declared, input, obscured)| {
                let mut observation = node(SemanticRole::TextField);
                observation.name = name;
                observation.description = description;
                observation.text_runs = text_runs;
                observation.value = value;
                observation.destination_url = url;
                observation.declared_sensitivity = declared;
                observation.signals.obscured = obscured;
                if let Some(kind) = InputType::ALL.get(input) {
                    observation.signals.input_type = InputTypeSignal::Known(*kind);
                }
                observation
            },
        )
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(512))]

    /// Whatever a node holds, the four destinations stay strictly ordered.
    #[test]
    fn generated_observations_never_break_the_ordering(observation in observation()) {
        let set = project_all(&observation);
        prop_assert_eq!(set.verify_narrowing(), Ok(()));
    }

    /// Telemetry never holds a string, whatever the node held.
    #[test]
    fn telemetry_never_holds_a_string(observation in observation()) {
        let set = project_all(&observation);
        prop_assert!(
            set.fragments(RedactionDestination::Telemetry)
                .next()
                .is_none()
        );
        let Some(telemetry) = set.telemetry() else {
            unreachable!("the telemetry projection is built")
        };
        // Rendering the whole record and looking for the node's own text proves
        // the shape rather than trusting the field list.
        let rendered = format!("{telemetry:?}");
        for fragment in set.fragments(RedactionDestination::LocalContext) {
            if fragment.len() >= 4 {
                prop_assert!(!rendered.contains(fragment));
            }
        }
    }

    /// A projection built for a destination is the projection for that
    /// destination, so the set cannot silently hold the wrong one.
    #[test]
    fn each_projection_is_the_one_its_destination_asked_for(observation in observation()) {
        let set = project_all(&observation);
        for destination in RedactionDestination::ALL {
            prop_assert_eq!(set.projection(*destination).destination(), *destination);
        }
        prop_assert!(matches!(set.projection(RedactionDestination::LocalContext), Projection::Local(_)));
    }
}
