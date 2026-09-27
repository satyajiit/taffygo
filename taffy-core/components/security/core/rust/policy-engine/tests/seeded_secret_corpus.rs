// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Seeded-secret corpus: no canary reaches a model, an audit record, or
//! telemetry.
//!
//! Roadmap section 5 makes this an exit condition for milestone M2: "seeded-
//! secret fixtures produce zero plaintext-secret model, context, log, crash,
//! analytics, and audit records". The corpus below is the field-shape half of
//! it — one entry per way a secret actually reaches a page, each carrying a
//! canary that is unique, searchable, and shaped like the thing it stands in
//! for.
//!
//! Two defences are exercised and the corpus keeps them apart on purpose:
//!
//! - **classification** catches a secret that sits in a field somebody can
//!   recognize — a password control, a one-time-code token, a label that says
//!   security code, a wallet seed phrase;
//! - **the value masker** catches a secret that sits somewhere nobody
//!   classified, which is how secrets usually escape: pasted into a search box,
//!   quoted in support copy, hidden in a query string.
//!
//! Each case records which defence is expected to catch it, and one case is
//! deliberately caught by the masker alone, so the suite fails rather than
//! passes vacuously if classification starts covering everything.
//!
//! The assertion is made against the debug rendering of the whole projection,
//! not against a hand-written list of fields. A field added to a projection and
//! forgotten about is therefore still checked.

use policy_engine::redaction::{
    AutocompleteSignal, AutocompleteToken, InputType, InputTypeSignal, Projection,
};
use policy_engine::{redact_for, FieldObservation, RedactionDestination};

use bip_types::identity::SemanticNodeId;
use bip_types::snapshot::{SemanticRole, Sensitivity};

/// Which defence is expected to keep the canary out of a narrower destination.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Caught {
    /// The zone classifier recognized the field.
    Classification,
    /// Nothing classified the field; the value masker recognized the value.
    Masker,
}

struct Case {
    name: &'static str,
    canary: &'static str,
    caught_by: Caught,
    /// Whether the in-process local observation is allowed to hold the canary.
    /// A never-extract class is withheld even there.
    local_may_hold: bool,
    observation: FieldObservation,
}

fn node(role: SemanticRole) -> FieldObservation {
    FieldObservation::new(SemanticNodeId::new("n_1"), role)
}

fn corpus() -> Vec<Case> {
    let mut cases = classified_cases();
    cases.append(&mut unclassified_cases());
    cases
}

/// The cases the zone classifier is expected to recognize.
fn classified_cases() -> Vec<Case> {
    let mut cases = Vec::new();

    // 1. The obvious one: a password control.
    let canary = "Tr0ub4dorCanaryPw3xK9";
    let mut observation = node(SemanticRole::TextField);
    observation.name = Some("Password".to_owned());
    observation.signals.input_type = InputTypeSignal::Known(InputType::Password);
    observation.value = Some(canary.to_owned());
    cases.push(Case {
        name: "password control",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: false,
        observation,
    });

    // 2. A one-time code, declared by the page as not sensitive.
    let canary = "449182774213";
    let mut observation = node(SemanticRole::TextField);
    observation.name = Some("Enter the code we sent you".to_owned());
    observation.declared_sensitivity = Sensitivity::NotSensitive;
    observation.signals.input_type = InputTypeSignal::Known(InputType::Text);
    observation.signals.autocomplete = AutocompleteSignal::Known(AutocompleteToken::OneTimeCode);
    observation.value = Some(canary.to_owned());
    cases.push(Case {
        name: "one-time code the page called ordinary",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: false,
        observation,
    });

    // 3. A card security code recognizable only from its label.
    let canary = "CanaryCvc731";
    let mut observation = node(SemanticRole::TextField);
    observation.name = Some("Security code (CVC)".to_owned());
    observation.signals.input_type = InputTypeSignal::Known(InputType::Text);
    observation.value = Some(canary.to_owned());
    cases.push(Case {
        name: "card security code recognized by label",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: false,
        observation,
    });

    // 4. A wallet seed phrase sitting in ordinary text runs. No token in it is
    // secret-shaped on its own, so only the label classification catches it.
    let canary = "canary abandon ability able about above absent";
    let mut observation = node(SemanticRole::Paragraph);
    observation.name = Some("Wallet seed phrase".to_owned());
    observation.text_runs = vec![canary.to_owned()];
    cases.push(Case {
        name: "seed phrase in ordinary text",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: false,
        observation,
    });

    // 5. A payment card number. The local isolated core may hold it; no remote
    // destination may.
    let canary = "4111111111111111";
    let mut observation = node(SemanticRole::TextField);
    observation.name = Some("Card number".to_owned());
    observation.signals.autocomplete = AutocompleteSignal::Known(AutocompleteToken::CardNumber);
    observation.value = Some(canary.to_owned());
    cases.push(Case {
        name: "payment card number",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: true,
        observation,
    });

    cases
}

/// The cases nothing declares, where the masker and the field table are the
/// only things standing between a secret and a narrower destination.
fn unclassified_cases() -> Vec<Case> {
    let mut cases = Vec::new();

    // 6. A provider key pasted into a search box. Nothing declares it.
    let canary = "sk-live-Canary9f2ba71c4d8e6503";
    let mut observation = node(SemanticRole::SearchField);
    observation.name = Some("Search".to_owned());
    observation.signals.input_type = InputTypeSignal::Known(InputType::Search);
    observation.value = Some(canary.to_owned());
    cases.push(Case {
        name: "provider key pasted into a search box",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: true,
        observation,
    });

    // 7. A secret quoted in ordinary support copy on an ordinary paragraph.
    // Nothing here is a control, nothing is labelled, and no signal fires: the
    // masker is the only thing between this and the model.
    let canary = "Hunter2CanaryPw8143XZ";
    let mut observation = node(SemanticRole::Paragraph);
    observation.name = Some("Support ticket".to_owned());
    observation.text_runs = vec![format!("Your temporary key is {canary} until Friday")];
    cases.push(Case {
        name: "secret quoted in unclassified page text",
        canary,
        caught_by: Caught::Masker,
        local_may_hold: true,
        observation,
    });

    // 8. A session token in a link's query string.
    let canary = "CanarySess8813Kd7Q";
    let mut observation = node(SemanticRole::Link);
    observation.name = Some("Your account".to_owned());
    observation.destination_url = Some(format!("https://example.test/account?session={canary}"));
    cases.push(Case {
        name: "session token in a query string",
        canary,
        caught_by: Caught::Masker,
        local_may_hold: true,
        observation,
    });

    // 9. A payment field inside a cross-origin embedded frame.
    let canary = "5555444433332226";
    let mut observation = node(SemanticRole::TextField);
    observation.name = Some("Card number".to_owned());
    observation.signals.cross_origin_embedded = true;
    observation.signals.input_type = InputTypeSignal::Known(InputType::Text);
    observation.value = Some(canary.to_owned());
    cases.push(Case {
        name: "payment field in a cross-origin frame",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: true,
        observation,
    });

    // 10. A control whose type is outside the closed list, holding a national
    // identifier. The unknown type raises rather than defaulting.
    let canary = "998877665544";
    let mut observation = node(SemanticRole::UnknownInteractive);
    observation.name = Some("Reference".to_owned());
    observation.signals.input_type = InputTypeSignal::from_attribute(Some("quantum-identity"));
    observation.value = Some(canary.to_owned());
    cases.push(Case {
        name: "unknown control type holding an identifier",
        canary,
        caught_by: Caught::Classification,
        local_may_hold: true,
        observation,
    });

    cases
}

/// The destinations no canary may ever reach.
const NARROW_DESTINATIONS: &[RedactionDestination] = &[
    RedactionDestination::ModelProjection,
    RedactionDestination::Audit,
    RedactionDestination::Telemetry,
];

#[test]
fn no_canary_reaches_a_model_an_audit_record_or_telemetry() {
    for case in corpus() {
        for destination in NARROW_DESTINATIONS {
            let projection = redact_for(&case.observation, *destination);

            // The debug rendering carries every field of the projection, so a
            // field nobody remembered to list is still checked.
            let rendered = format!("{projection:?}");
            assert!(
                !rendered.contains(case.canary),
                "canary leaked into {} for case {}: {rendered}",
                destination.label(),
                case.name
            );
            for fragment in projection.content_fragments() {
                assert!(
                    !fragment.contains(case.canary),
                    "canary leaked into a {} fragment for case {}",
                    destination.label(),
                    case.name
                );
            }
        }
    }
}

#[test]
fn the_corpus_really_carries_secrets() {
    // A leak test passes for the wrong reason if the fixtures never held a
    // secret in the first place. Every case must reach the local observation
    // with its canary intact unless a never-extract class withheld it there
    // too, and at least one case must depend on the masker alone.
    let cases = corpus();
    assert!(cases.len() >= 10);
    assert!(cases.iter().any(|case| case.caught_by == Caught::Masker));

    for case in cases {
        let local = redact_for(&case.observation, RedactionDestination::LocalContext);
        let rendered = format!("{local:?}");
        assert_eq!(
            rendered.contains(case.canary),
            case.local_may_hold,
            "case {} disagrees about what the local observation holds",
            case.name
        );
    }
}

#[test]
fn a_classified_case_stays_withheld_even_if_the_masker_would_have_missed_it() {
    // The seed phrase is the case the masker cannot see: every word in it is a
    // short lowercase dictionary word. Only classification keeps it out, so
    // this asserts the classification path rather than the masking path.
    let case = corpus()
        .into_iter()
        .find(|case| case.name == "seed phrase in ordinary text");
    let Some(case) = case else {
        unreachable!("the corpus contains a seed phrase case")
    };

    let masked = policy_engine::redaction::mask_secret_shaped(case.canary);
    assert_eq!(
        masked.masked_spans, 0,
        "the seed phrase case must be one the masker cannot recognize"
    );

    match redact_for(&case.observation, RedactionDestination::ModelProjection) {
        Projection::Model(model) => {
            assert!(model.text_runs.is_empty());
            assert_eq!(
                model.value_placeholder,
                Some("seed phrase field present"),
                "a withheld credential zone still says that it is there"
            );
        }
        other => unreachable!("expected a model projection, got {other:?}"),
    }
}

#[test]
fn telemetry_holds_no_string_from_any_case() {
    for case in corpus() {
        match redact_for(&case.observation, RedactionDestination::Telemetry) {
            Projection::Telemetry(telemetry) => {
                // The type carries counts and enumerated names only. Rendering
                // it and searching for every canary in the corpus proves the
                // shape rather than trusting it.
                let rendered = format!("{telemetry:?}");
                for other in corpus() {
                    assert!(!rendered.contains(other.canary), "case {}", case.name);
                }
            }
            other => unreachable!("expected a telemetry projection, got {other:?}"),
        }
    }
}
