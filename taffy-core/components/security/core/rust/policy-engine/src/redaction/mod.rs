// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Sensitive-zone classification and per-destination redaction (protocol
//! specification section 9, data and privacy sections 7 and 14).
//!
//! # Classification uses more than the input type
//!
//! [`classify_zone`] joins every signal that fired: the classification the page
//! declared, the control type, the `autocomplete` token, the role, keywords in
//! the accessible name and description, whether the control is obscured,
//! whether it is embedded cross-origin, whether the context is a secure one,
//! organization policy, a user's own label, and bounded pattern detectors over
//! the value. No single signal is authoritative and none of them can subtract:
//! the result is a join over the sensitivity lattice, so a page that declares
//! `NOT_SENSITIVE` on a password field raises to credential all the same. That
//! is the executable form of "page instructions cannot lower sensitivity".
//!
//! # Each destination is strictly narrower
//!
//! [`RedactionDestination`] is ordered, and the ordering is a real containment:
//! every content field the model projection may carry, the local observation
//! may carry; every field the audit record may carry, the model projection may
//! carry; and the telemetry projection carries no page content at all. A test
//! walks the pairs and asserts strict containment, so a field added to a
//! narrower destination without adding it to the wider ones fails.
//!
//! Telemetry is content-free by construction rather than by policy:
//! [`TelemetryProjection`] has no field that can hold a string taken from a
//! page, so there is nothing for a careless caller to fill in.
//!
//! # Two layers, not one
//!
//! Field selection decides what may be carried. The value masker decides what
//! survives inside what is carried, because a secret does not always land in a
//! field anybody classified — somebody pastes a token into a search box. Both
//! run, and the masker runs on every string that leaves the local observation.

pub mod classify;
pub mod destination;
pub mod mask;
pub mod projection;
pub mod signals;

use bip_types::sensitivity::{Handling, NeverExtractClass};

pub use crate::redaction::classify::{classify_zone, FieldObservation, ZoneClassification};
pub use crate::redaction::destination::{ContentField, RedactionDestination};
pub use crate::redaction::mask::{
    mask_secret_shaped, split_url, MaskedText, UrlParts, MASK_MARKER,
};
pub use crate::redaction::projection::{
    AuditProjection, LocalProjection, ModelProjection, Projection, TelemetryProjection,
};
pub use crate::redaction::signals::{
    AutocompleteSignal, AutocompleteToken, InputType, InputTypeSignal, ZoneSignalKind, ZoneSignals,
};

/// Projects one observation for one destination.
///
/// Classification runs first and the classification is the same whatever the
/// destination — only what may be carried changes. The two layers are separate
/// on purpose: a bug in the field table cannot lower a classification, and a
/// bug in the classifier cannot widen a destination.
pub fn redact_for(observation: &FieldObservation, destination: RedactionDestination) -> Projection {
    let classification = classify_zone(observation);
    redact_classified(observation, &classification, destination)
}

/// Projects an observation whose classification is already known.
pub fn redact_classified(
    observation: &FieldObservation,
    classification: &ZoneClassification,
    destination: RedactionDestination,
) -> Projection {
    let handling = classification.handling_for(destination);
    let placeholder = classification
        .never_extract()
        .map(NeverExtractClass::structural_placeholder);
    let url = observation
        .destination_url
        .as_deref()
        .and_then(|url| split_url(url).ok());

    match destination {
        RedactionDestination::LocalContext => Projection::Local(LocalProjection {
            node_id: observation.node_id.clone(),
            role: observation.role,
            name: observation.name.clone(),
            description: observation.description.clone(),
            // A never-extract value reaches no destination at all, this one
            // included: section 9.1 forbids the plaintext value, a suggested
            // value, and anything derived from it, and a text run inside a
            // credential zone is one of the places a secret hides.
            text_runs: if handling.withholds() {
                Vec::new()
            } else {
                observation.text_runs.clone()
            },
            value: if handling.withholds() {
                None
            } else {
                observation.value.clone()
            },
            destination_url: observation.destination_url.clone(),
            states: observation.states.clone(),
            sensitivity: classification.sensitivity(),
            value_placeholder: placeholder,
        }),

        RedactionDestination::ModelProjection => project_for_model(
            observation,
            classification,
            handling,
            placeholder,
            url.as_ref(),
        ),

        RedactionDestination::Audit => {
            project_for_audit(observation, classification, handling, url.as_ref())
        }

        RedactionDestination::Telemetry => Projection::Telemetry(TelemetryProjection {
            role: observation.role,
            sensitivity: classification.sensitivity(),
            state_count: u64::try_from(observation.states.len()).unwrap_or(u64::MAX),
            text_run_count: u64::try_from(observation.text_runs.len()).unwrap_or(u64::MAX),
            value_present: observation.value.is_some(),
            destination_present: observation.destination_url.is_some(),
            withheld_fields: count_content_fields(observation),
        }),
    }
}

/// Builds the projection a model provider receives.
fn project_for_model(
    observation: &FieldObservation,
    classification: &ZoneClassification,
    handling: Handling,
    placeholder: Option<&'static str>,
    url: Option<&UrlParts>,
) -> Projection {
    let destination = RedactionDestination::ModelProjection;

    let mut masked_spans = 0;
    let mut withheld = 0;
    let mut mask = |text: Option<&String>, field: ContentField| -> Option<String> {
        if !destination.permits_content(field) || handling.withholds() {
            if text.is_some() {
                withheld += 1;
            }
            return None;
        }
        let masked = mask_secret_shaped(text?);
        masked_spans += masked.masked_spans;
        Some(masked.text)
    };

    let name = mask(observation.name.as_ref(), ContentField::Name);
    let description = mask(observation.description.as_ref(), ContentField::Description);
    let text_runs = if handling.withholds() {
        withheld += u64::try_from(observation.text_runs.len()).unwrap_or(u64::MAX);
        Vec::new()
    } else {
        observation
            .text_runs
            .iter()
            .map(|run| {
                let masked = mask_secret_shaped(run);
                masked_spans += masked.masked_spans;
                masked.text
            })
            .collect()
    };
    // A value survives only when nothing classified the field. Minimize
    // and redact means the shape may travel and the content may not.
    let value = if handling == Handling::Allowed {
        let masked = observation.value.as_deref().map(mask_secret_shaped);
        if let Some(masked) = masked {
            masked_spans += masked.masked_spans;
            Some(masked.text)
        } else {
            None
        }
    } else {
        if observation.value.is_some() {
            withheld += 1;
        }
        None
    };
    let destination_text = match (url, handling.withholds()) {
        (Some(parts), false) => Some(match parts.path.as_deref() {
            Some(path) => format!("{}{path}", parts.origin.display()),
            None => parts.origin.display(),
        }),
        (Some(_), true) => {
            withheld += 1;
            None
        }
        (None, _) => None,
    };

    Projection::Model(ModelProjection {
        role: observation.role,
        name,
        description,
        text_runs,
        value,
        destination: destination_text,
        states: observation.states.clone(),
        sensitivity: classification.sensitivity(),
        value_placeholder: placeholder,
        masked_spans,
        withheld_fields: withheld,
    })
}

/// Builds the durable audit record for one node.
fn project_for_audit(
    observation: &FieldObservation,
    classification: &ZoneClassification,
    handling: Handling,
    url: Option<&UrlParts>,
) -> Projection {
    let mut withheld = count_content_fields(observation);
    let destination_origin = match (url, handling.withholds()) {
        (Some(parts), false) => {
            withheld = withheld.saturating_sub(1);
            Some(parts.origin.display())
        }
        _ => None,
    };
    Projection::Audit(AuditProjection {
        node_id: observation.node_id.clone(),
        role: observation.role,
        states: observation.states.clone(),
        sensitivity: classification.sensitivity(),
        destination_origin,
        name_present: observation.name.is_some(),
        value_present: observation.value.is_some(),
        withheld_fields: withheld,
    })
}

/// How many content fields the observation actually held.
fn count_content_fields(observation: &FieldObservation) -> u64 {
    let present = usize::from(observation.name.is_some())
        + usize::from(observation.description.is_some())
        + usize::from(observation.value.is_some())
        + usize::from(observation.destination_url.is_some())
        + observation.text_runs.len();
    u64::try_from(present).unwrap_or(u64::MAX)
}

#[cfg(test)]
mod tests {
    use super::{
        classify_zone, mask_secret_shaped, redact_for, split_url, AutocompleteSignal,
        AutocompleteToken, ContentField, FieldObservation, InputType, InputTypeSignal, Projection,
        RedactionDestination, ZoneSignalKind,
    };
    use bip_types::identity::SemanticNodeId;
    use bip_types::sensitivity::{NeverExtractClass, Sensitivity};
    use bip_types::snapshot::SemanticRole;

    fn field(role: SemanticRole) -> FieldObservation {
        FieldObservation::new(SemanticNodeId::new("n_1"), role)
    }

    #[test]
    fn each_destination_carries_strictly_fewer_content_fields_than_the_last() {
        let mut previous: Option<(RedactionDestination, Vec<ContentField>)> = None;
        for destination in RedactionDestination::ALL {
            let permitted = destination.permitted_content();
            if let Some((wider, wider_fields)) = previous {
                assert!(
                    permitted.iter().all(|field| wider_fields.contains(field)),
                    "{} carries a field {} does not",
                    destination.label(),
                    wider.label()
                );
                assert!(
                    permitted.len() < wider_fields.len(),
                    "{} is not strictly narrower than {}",
                    destination.label(),
                    wider.label()
                );
            }
            previous = Some((*destination, permitted));
        }
        assert!(RedactionDestination::Telemetry
            .permitted_content()
            .is_empty());
    }

    #[test]
    fn a_page_cannot_lower_a_classification_it_declared_wrongly() {
        let mut observation = field(SemanticRole::TextField);
        observation.declared_sensitivity = Sensitivity::NotSensitive;
        observation.signals.input_type = InputTypeSignal::Known(InputType::Password);

        let classification = classify_zone(&observation);
        assert!(classification.is_never_extract());
        assert_eq!(
            classification.never_extract(),
            Some(NeverExtractClass::Password)
        );
        assert!(classification
            .sensitivity()
            .contains(Sensitivity::Credential));
    }

    #[test]
    fn classification_uses_more_than_the_control_type() {
        // An ordinary text control whose autocomplete token gives it away.
        let mut observation = field(SemanticRole::TextField);
        observation.signals.input_type = InputTypeSignal::Known(InputType::Text);
        observation.signals.autocomplete =
            AutocompleteSignal::Known(AutocompleteToken::OneTimeCode);
        let classification = classify_zone(&observation);
        assert_eq!(
            classification.never_extract(),
            Some(NeverExtractClass::OneTimeCode)
        );
        assert!(classification
            .fired_signals()
            .contains(&ZoneSignalKind::AutocompleteToken));

        // An ordinary text control whose label gives it away.
        let mut labelled = field(SemanticRole::TextField);
        labelled.signals.input_type = InputTypeSignal::Known(InputType::Text);
        labelled.name = Some("Card security code (CVV)".to_owned());
        let classification = classify_zone(&labelled);
        assert_eq!(
            classification.never_extract(),
            Some(NeverExtractClass::CardVerificationValue)
        );

        // An ordinary paragraph inside a cross-origin frame on a page served
        // without a secure context.
        let mut embedded = field(SemanticRole::Paragraph);
        embedded.signals.cross_origin_embedded = true;
        embedded.signals.origin_is_potentially_trustworthy = false;
        let classification = classify_zone(&embedded);
        assert!(classification
            .sensitivity()
            .contains(Sensitivity::UnknownSensitive));

        // Every keyword contributes. Stopping at the first would make the
        // table's order decide which of two independent data classes survives.
        let mut multi_label = field(SemanticRole::TextField);
        multi_label.signals.input_type = InputTypeSignal::Known(InputType::Text);
        multi_label.name = Some("Bank account number for a diagnosis refund".to_owned());
        let classification = classify_zone(&multi_label);
        assert!(classification
            .sensitivity()
            .contains(Sensitivity::Financial));
        assert!(classification.sensitivity().contains(Sensitivity::Health));
    }

    #[test]
    fn an_unrecognized_control_type_or_token_raises_rather_than_defaults() {
        let mut observation = field(SemanticRole::UnknownInteractive);
        observation.signals.input_type = InputTypeSignal::from_attribute(Some("quantum-field"));
        assert_eq!(
            observation.signals.input_type,
            InputTypeSignal::Unrecognized
        );
        assert!(classify_zone(&observation)
            .sensitivity()
            .contains(Sensitivity::UnknownSensitive));

        let mut token = field(SemanticRole::UnknownInteractive);
        token.signals.autocomplete = AutocompleteSignal::from_attribute(Some("cc-quantum"));
        assert_eq!(token.signals.autocomplete, AutocompleteSignal::Unrecognized);
        assert!(classify_zone(&token)
            .sensitivity()
            .contains(Sensitivity::UnknownSensitive));
    }

    #[test]
    fn the_masker_replaces_secret_shaped_tokens_and_keeps_ordinary_words() {
        let masked = mask_secret_shaped("your code is 449182774213 now");
        assert_eq!(masked.text, "your code is [redacted] now");
        assert_eq!(masked.masked_spans, 1);

        let untouched = mask_secret_shaped("the quick brown fox");
        assert_eq!(untouched.text, "the quick brown fox");
        assert_eq!(untouched.masked_spans, 0);

        let grouped = mask_secret_shaped("pay 4111 1111 1111 1111 now");
        assert_eq!(grouped.text, "pay [redacted] now");
        assert_eq!(grouped.masked_spans, 1);
    }

    #[test]
    fn a_grouped_card_value_is_still_classified_as_payment() {
        let mut observation = field(SemanticRole::TextField);
        observation.signals.input_type = InputTypeSignal::Known(InputType::Text);
        observation.value = Some("4111 1111 1111 1111".to_owned());

        assert!(classify_zone(&observation)
            .sensitivity()
            .contains(Sensitivity::Payment));
    }

    #[test]
    fn a_url_loses_its_query_and_fragment_before_anything_else_sees_it() {
        let parts = match split_url("https://Example.test:443/orders/42?token=abc#section") {
            Ok(parts) => parts,
            Err(error) => unreachable!("fixture url must split: {error}"),
        };
        assert_eq!(parts.origin.display(), "https://example.test");
        assert_eq!(parts.path.as_deref(), Some("/orders/42"));
        assert!(parts.has_query);
        assert!(parts.has_fragment);
    }

    #[test]
    fn the_telemetry_projection_carries_no_page_content() {
        let mut observation = field(SemanticRole::Link);
        observation.name = Some("Order history".to_owned());
        observation.destination_url = Some("https://example.test/orders?token=abc".to_owned());
        observation.text_runs = vec!["one".to_owned(), "two".to_owned()];

        let projection = redact_for(&observation, RedactionDestination::Telemetry);
        assert!(projection.content_fragments().next().is_none());
        match projection {
            Projection::Telemetry(telemetry) => {
                assert_eq!(telemetry.text_run_count, 2);
                assert!(telemetry.destination_present);
            }
            _ => unreachable!("the telemetry destination builds a telemetry projection"),
        }
    }

    #[test]
    fn the_audit_projection_carries_an_origin_and_never_a_path() {
        let mut observation = field(SemanticRole::Link);
        observation.name = Some("Order history".to_owned());
        observation.destination_url = Some("https://example.test/orders/42?token=abc".to_owned());

        let projection = redact_for(&observation, RedactionDestination::Audit);
        assert_eq!(
            projection.content_fragments().collect::<Vec<_>>(),
            vec!["https://example.test"]
        );
    }
}
