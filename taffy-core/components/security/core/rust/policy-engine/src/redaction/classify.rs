// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Classifying one node from every signal that fired (protocol specification
//! section 9.2).
//!
//! The result is a join over the sensitivity lattice. Adding a signal can raise
//! a classification and can never lower it, so the order the signals are
//! examined in does not change the answer, and a page that declares
//! `NOT_SENSITIVE` on a password field raises to credential all the same. That
//! is the executable form of "page instructions cannot lower sensitivity".
//!
//! The keyword table reads page text as a *signal*, never as an instruction. It
//! is compiled in, bounded, and case-insensitive, and nothing it matches can
//! make a value less sensitive than another signal already made it.

use bip_types::identity::SemanticNodeId;
use bip_types::sensitivity::{Handling, NeverExtractClass, SensitivitySet};
use bip_types::snapshot::{NodeState, SemanticRole, Sensitivity};

use crate::redaction::destination::RedactionDestination;
use crate::redaction::mask::{is_secret_shaped, truncate_chars, MAX_PROJECTED_CHARS};
use crate::redaction::signals::{
    AutocompleteSignal, AutocompleteToken, InputType, InputTypeSignal, ZoneSignalKind, ZoneSignals,
};

/// What one node was classified as, and by what.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ZoneClassification {
    sensitivity: SensitivitySet,
    never_extract: Option<NeverExtractClass>,
    fired: Vec<ZoneSignalKind>,
}

impl ZoneClassification {
    /// The lattice element every signal joined to.
    pub fn sensitivity(&self) -> SensitivitySet {
        self.sensitivity
    }

    /// The never-extract class, when the node holds one.
    pub fn never_extract(&self) -> Option<NeverExtractClass> {
        self.never_extract
    }

    /// Which signals raised the classification, deduplicated and ordered.
    pub fn fired_signals(&self) -> &[ZoneSignalKind] {
        &self.fired
    }

    /// Whether the value must never be emitted in plaintext anywhere.
    pub fn is_never_extract(&self) -> bool {
        self.never_extract.is_some() || self.sensitivity.is_never_extract()
    }

    /// The handling this classification requires at `destination`.
    pub fn handling_for(&self, destination: RedactionDestination) -> Handling {
        if self.is_never_extract() {
            return Handling::Withhold;
        }
        self.sensitivity
            .handling_for(destination.sensitivity_policy())
    }

    /// Adds a classification and records what raised it.
    fn raise(&mut self, classification: Sensitivity, signal: ZoneSignalKind) {
        self.sensitivity = self.sensitivity.with(classification);
        if !self.fired.contains(&signal) {
            self.fired.push(signal);
        }
    }

    /// Records a never-extract class. The first one found wins; every one of
    /// them withholds identically, so the choice only affects the placeholder.
    fn withhold(&mut self, class: NeverExtractClass, signal: ZoneSignalKind) {
        self.raise(class.sensitivity(), signal);
        if self.never_extract.is_none() {
            self.never_extract = Some(class);
        }
    }
}

/// One node as the broker observed it, before any destination is chosen.
#[derive(Clone, Debug, PartialEq)]
pub struct FieldObservation {
    /// The node this observation is about.
    pub node_id: SemanticNodeId,
    /// Its role.
    pub role: SemanticRole,
    /// The accessible name, as observed.
    pub name: Option<String>,
    /// The description, as observed.
    pub description: Option<String>,
    /// Text runs, as observed.
    pub text_runs: Vec<String>,
    /// The control's current value, as observed.
    pub value: Option<String>,
    /// The destination URL, as the browser committed it.
    pub destination_url: Option<String>,
    /// The states the adapter asserts.
    pub states: Vec<NodeState>,
    /// The classification the page declared. It is joined with the derived
    /// classification and can only raise the result.
    pub declared_sensitivity: Sensitivity,
    /// Everything else the classifier looks at.
    pub signals: ZoneSignals,
}

impl FieldObservation {
    /// An observation of an ordinary, unclassified node.
    pub fn new(node_id: SemanticNodeId, role: SemanticRole) -> Self {
        Self {
            node_id,
            role,
            name: None,
            description: None,
            text_runs: Vec::new(),
            value: None,
            destination_url: None,
            states: Vec::new(),
            declared_sensitivity: Sensitivity::NotSensitive,
            signals: ZoneSignals::default(),
        }
    }
}

/// Classifies one node from every available signal.
///
/// The result is a join. Adding a signal can raise the classification and can
/// never lower it, so the order the signals are examined in does not change the
/// answer.
pub fn classify_zone(observation: &FieldObservation) -> ZoneClassification {
    let mut classification = ZoneClassification {
        sensitivity: SensitivitySet::EMPTY,
        never_extract: None,
        fired: Vec::new(),
    };

    // The page's own claim. It is evidence, joined like any other: a page that
    // declares NOT_SENSITIVE contributes nothing and lowers nothing.
    if observation.declared_sensitivity != Sensitivity::NotSensitive {
        classification.raise(
            observation.declared_sensitivity,
            ZoneSignalKind::DeclaredByPage,
        );
        if observation.declared_sensitivity.is_never_extract() {
            classification.withhold(NeverExtractClass::Password, ZoneSignalKind::DeclaredByPage);
        }
    }

    classify_control_signals(observation, &mut classification);

    // A control that takes a value but says nothing about what kind is not an
    // ordinary paragraph: it is an unclassified value.
    if matches!(observation.role, SemanticRole::TextField)
        && observation.signals.autocomplete == AutocompleteSignal::Absent
    {
        classification.raise(Sensitivity::UnknownSensitive, ZoneSignalKind::Role);
    }

    for text in observation
        .name
        .iter()
        .chain(observation.description.iter())
    {
        classify_label(text, &mut classification);
    }

    if observation.signals.obscured {
        classification.raise(Sensitivity::UnknownSensitive, ZoneSignalKind::ObscuredState);
    }
    if observation.signals.cross_origin_embedded {
        classification.raise(
            Sensitivity::UnknownSensitive,
            ZoneSignalKind::CrossOriginEmbedding,
        );
    }
    if !observation.signals.origin_is_potentially_trustworthy {
        classification.raise(
            Sensitivity::UnknownSensitive,
            ZoneSignalKind::InsecureContext,
        );
    }
    if let Some(imposed) = observation.signals.organization_policy {
        classification.raise(imposed, ZoneSignalKind::OrganizationPolicy);
    }
    if let Some(labelled) = observation.signals.user_label {
        classification.raise(labelled, ZoneSignalKind::UserLabel);
    }

    if let Some(value) = observation.value.as_deref() {
        if let Some(detected) = detect_value_pattern(value) {
            classification.raise(detected, ZoneSignalKind::ValuePattern);
        }
    }

    classification
}

/// Adds what the control type and the `autocomplete` token say.
///
/// Both are closed lists. A token outside either one raises the classification
/// rather than resolving to the nearest member, which is the fail-closed rule
/// of specification section 6.2 applied to a signal rather than to a message.
fn classify_control_signals(
    observation: &FieldObservation,
    classification: &mut ZoneClassification,
) {
    match observation.signals.input_type {
        InputTypeSignal::NotAControl => {}
        InputTypeSignal::Unrecognized => {
            classification.raise(Sensitivity::UnknownSensitive, ZoneSignalKind::ControlType);
        }
        InputTypeSignal::Known(input_type) => match input_type {
            InputType::Password => {
                classification.withhold(NeverExtractClass::Password, ZoneSignalKind::ControlType);
            }
            InputType::Email | InputType::Tel => {
                classification.raise(Sensitivity::Personal, ZoneSignalKind::ControlType);
            }
            InputType::Hidden | InputType::File => {
                classification.raise(Sensitivity::UnknownSensitive, ZoneSignalKind::ControlType);
            }
            InputType::Text
            | InputType::Number
            | InputType::Search
            | InputType::Url
            | InputType::Date
            | InputType::Checkbox
            | InputType::Radio => {}
        },
    }

    match observation.signals.autocomplete {
        AutocompleteSignal::Absent => {}
        AutocompleteSignal::Unrecognized => {
            classification.raise(
                Sensitivity::UnknownSensitive,
                ZoneSignalKind::AutocompleteToken,
            );
        }
        AutocompleteSignal::Known(token) => match token {
            AutocompleteToken::CurrentPassword | AutocompleteToken::NewPassword => {
                classification.withhold(
                    NeverExtractClass::Password,
                    ZoneSignalKind::AutocompleteToken,
                );
            }
            AutocompleteToken::OneTimeCode => {
                classification.withhold(
                    NeverExtractClass::OneTimeCode,
                    ZoneSignalKind::AutocompleteToken,
                );
            }
            AutocompleteToken::CardSecurityCode => {
                classification.withhold(
                    NeverExtractClass::CardVerificationValue,
                    ZoneSignalKind::AutocompleteToken,
                );
            }
            AutocompleteToken::CardNumber | AutocompleteToken::CardExpiry => {
                classification.raise(Sensitivity::Payment, ZoneSignalKind::AutocompleteToken);
            }
            AutocompleteToken::Username => {
                classification.raise(Sensitivity::Account, ZoneSignalKind::AutocompleteToken);
            }
            AutocompleteToken::Birthday => {
                classification.raise(Sensitivity::Identity, ZoneSignalKind::AutocompleteToken);
                classification.raise(Sensitivity::Personal, ZoneSignalKind::AutocompleteToken);
            }
            AutocompleteToken::Name
            | AutocompleteToken::Email
            | AutocompleteToken::Telephone
            | AutocompleteToken::StreetAddress
            | AutocompleteToken::PostalCode => {
                classification.raise(Sensitivity::Personal, ZoneSignalKind::AutocompleteToken);
            }
        },
    }
}

/// The compiled-in keyword table for label-derived classification.
///
/// Bounded, local, and case-insensitive. It reads page text as a signal, never
/// as an instruction, and it can only raise a classification.
const LABEL_KEYWORDS: &[(&str, Sensitivity, Option<NeverExtractClass>)] = &[
    (
        "password",
        Sensitivity::Credential,
        Some(NeverExtractClass::Password),
    ),
    (
        "passcode",
        Sensitivity::Credential,
        Some(NeverExtractClass::Passcode),
    ),
    (
        "passphrase",
        Sensitivity::Credential,
        Some(NeverExtractClass::Password),
    ),
    (
        "seed phrase",
        Sensitivity::Credential,
        Some(NeverExtractClass::SeedPhrase),
    ),
    (
        "recovery code",
        Sensitivity::Credential,
        Some(NeverExtractClass::RecoveryCode),
    ),
    (
        "one-time code",
        Sensitivity::Credential,
        Some(NeverExtractClass::OneTimeCode),
    ),
    (
        "verification code",
        Sensitivity::Credential,
        Some(NeverExtractClass::OneTimeCode),
    ),
    (
        "security code",
        Sensitivity::Credential,
        Some(NeverExtractClass::CardVerificationValue),
    ),
    (
        "cvv",
        Sensitivity::Credential,
        Some(NeverExtractClass::CardVerificationValue),
    ),
    (
        "cvc",
        Sensitivity::Credential,
        Some(NeverExtractClass::CardVerificationValue),
    ),
    (
        "private key",
        Sensitivity::Credential,
        Some(NeverExtractClass::PrivateKey),
    ),
    (
        "api key",
        Sensitivity::Credential,
        Some(NeverExtractClass::AuthenticationToken),
    ),
    (
        "session token",
        Sensitivity::Credential,
        Some(NeverExtractClass::AuthenticationToken),
    ),
    (
        "access token",
        Sensitivity::Credential,
        Some(NeverExtractClass::AuthenticationToken),
    ),
    (
        "session cookie",
        Sensitivity::Credential,
        Some(NeverExtractClass::SessionCookie),
    ),
    (
        "passkey",
        Sensitivity::Credential,
        Some(NeverExtractClass::Passkey),
    ),
    ("card number", Sensitivity::Payment, None),
    ("account number", Sensitivity::Financial, None),
    ("sort code", Sensitivity::Financial, None),
    ("routing number", Sensitivity::Financial, None),
    ("diagnosis", Sensitivity::Health, None),
    ("prescription", Sensitivity::Health, None),
    ("passport", Sensitivity::Identity, None),
    ("national id", Sensitivity::Identity, None),
    ("tax id", Sensitivity::Identity, None),
    ("date of birth", Sensitivity::Personal, None),
    ("home address", Sensitivity::Personal, None),
];

/// Joins every compiled-in keyword found in a label.
fn classify_label(text: &str, classification: &mut ZoneClassification) {
    let lowered = truncate_chars(text, MAX_PROJECTED_CHARS).to_ascii_lowercase();
    for (keyword, classification_from_label, never_extract) in LABEL_KEYWORDS {
        if !lowered.contains(keyword) {
            continue;
        }
        classification.raise(*classification_from_label, ZoneSignalKind::LabelKeyword);
        if let Some(class) = never_extract {
            classification.withhold(*class, ZoneSignalKind::LabelKeyword);
        }
    }
}

/// A bounded pattern detector over a value (protocol specification section 9.2).
///
/// It reports a classification, never the match, and it examines a bounded
/// prefix. It is a signal among others: a negative result classifies nothing.
fn detect_value_pattern(value: &str) -> Option<Sensitivity> {
    let bounded = truncate_chars(value, MAX_PROJECTED_CHARS);
    let digits = bounded.chars().filter(char::is_ascii_digit).count();

    if (13..=19).contains(&digits) {
        return Some(Sensitivity::Payment);
    }
    if digits >= 9 {
        return Some(Sensitivity::Identity);
    }
    if is_secret_shaped(bounded) {
        return Some(Sensitivity::UnknownSensitive);
    }
    None
}
