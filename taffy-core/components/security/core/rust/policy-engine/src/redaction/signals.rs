// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The signals a classifier reads from a control, other than its text
//! (protocol specification section 9.2).
//!
//! Every closed list here decodes to an explicit unknown rather than to a
//! nearest member: an `input` type nobody recognizes and an `autocomplete`
//! token nobody recognizes both raise the classification rather than defaulting
//! it, because a page that wants a field treated as ordinary can always spell
//! its type wrong.

use bip_types::snapshot::Sensitivity;

/// The control type signal (protocol specification section 9.2).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum InputType {
    /// A free text control.
    Text,
    /// A password control.
    Password,
    /// An email control.
    Email,
    /// A telephone control.
    Tel,
    /// A numeric control.
    Number,
    /// A search control.
    Search,
    /// A URL control.
    Url,
    /// A date control.
    Date,
    /// A checkbox.
    Checkbox,
    /// A radio button.
    Radio,
    /// A hidden control.
    Hidden,
    /// A file chooser.
    File,
}

impl InputType {
    /// Every control type this module recognizes, in declaration order.
    ///
    /// A test walks it, so a type added without a classification rule is
    /// noticed rather than silently treated as ordinary text.
    pub const ALL: &'static [Self] = &[
        Self::Text,
        Self::Password,
        Self::Email,
        Self::Tel,
        Self::Number,
        Self::Search,
        Self::Url,
        Self::Date,
        Self::Checkbox,
        Self::Radio,
        Self::Hidden,
        Self::File,
    ];

    /// Parses a control type token. `None` means the token is outside the list.
    pub fn from_token(token: &str) -> Option<Self> {
        match token.to_ascii_lowercase().as_str() {
            "text" => Some(Self::Text),
            "password" => Some(Self::Password),
            "email" => Some(Self::Email),
            "tel" => Some(Self::Tel),
            "number" => Some(Self::Number),
            "search" => Some(Self::Search),
            "url" => Some(Self::Url),
            "date" => Some(Self::Date),
            "checkbox" => Some(Self::Checkbox),
            "radio" => Some(Self::Radio),
            "hidden" => Some(Self::Hidden),
            "file" => Some(Self::File),
            _ => None,
        }
    }
}

/// What the control type says, including "the token was not one we know".
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum InputTypeSignal {
    /// The node is not a form control, so there is no signal here.
    NotAControl,
    /// A recognized control type.
    Known(InputType),
    /// A control whose type is outside the closed list. It is never coerced to
    /// the nearest or least restrictive type; it raises the classification
    /// instead.
    Unrecognized,
}

impl InputTypeSignal {
    /// Classifies a control type token as it arrived on the wire.
    ///
    /// `None` means no attribute was present. A present but unknown token
    /// becomes [`Self::Unrecognized`] and fails closed.
    pub fn from_attribute(token: Option<&str>) -> Self {
        match token {
            None => Self::NotAControl,
            Some(value) => match InputType::from_token(value) {
                Some(known) => Self::Known(known),
                None => Self::Unrecognized,
            },
        }
    }
}

/// The `autocomplete` token signal (protocol specification section 9.2).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AutocompleteToken {
    /// The current password.
    CurrentPassword,
    /// A new password.
    NewPassword,
    /// A one-time code.
    OneTimeCode,
    /// A payment card number.
    CardNumber,
    /// A payment card security code.
    CardSecurityCode,
    /// A payment card expiry.
    CardExpiry,
    /// A person's name.
    Name,
    /// An email address.
    Email,
    /// A telephone number.
    Telephone,
    /// A postal address.
    StreetAddress,
    /// A postal code.
    PostalCode,
    /// A date of birth.
    Birthday,
    /// A username.
    Username,
}

impl AutocompleteToken {
    /// Parses an `autocomplete` token. `None` means it is outside the list.
    pub fn from_token(token: &str) -> Option<Self> {
        match token.to_ascii_lowercase().as_str() {
            "current-password" => Some(Self::CurrentPassword),
            "new-password" => Some(Self::NewPassword),
            "one-time-code" => Some(Self::OneTimeCode),
            "cc-number" => Some(Self::CardNumber),
            "cc-csc" => Some(Self::CardSecurityCode),
            "cc-exp" => Some(Self::CardExpiry),
            "name" => Some(Self::Name),
            "email" => Some(Self::Email),
            "tel" => Some(Self::Telephone),
            "street-address" => Some(Self::StreetAddress),
            "postal-code" => Some(Self::PostalCode),
            "bday" => Some(Self::Birthday),
            "username" => Some(Self::Username),
            _ => None,
        }
    }
}

/// What the `autocomplete` attribute says, including "not one we know".
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AutocompleteSignal {
    /// No `autocomplete` attribute.
    Absent,
    /// A recognized token.
    Known(AutocompleteToken),
    /// A token outside the closed list. It raises the classification.
    Unrecognized,
}

impl AutocompleteSignal {
    /// Classifies an `autocomplete` token as it arrived on the wire.
    pub fn from_attribute(token: Option<&str>) -> Self {
        match token {
            None => Self::Absent,
            Some(value) => match AutocompleteToken::from_token(value) {
                Some(known) => Self::Known(known),
                None => Self::Unrecognized,
            },
        }
    }
}

/// Which signal raised a classification.
///
/// Recorded so an audit reader can see that a classification came from more
/// than one place, and so a test can assert that a field with an innocent
/// control type was still classified.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ZoneSignalKind {
    /// The classification the page declared. Joined, never trusted as a ceiling.
    DeclaredByPage,
    /// The control type.
    ControlType,
    /// The `autocomplete` token.
    AutocompleteToken,
    /// The semantic role.
    Role,
    /// A keyword in the accessible name or description.
    LabelKeyword,
    /// The control is obscured.
    ObscuredState,
    /// The control is embedded cross-origin.
    CrossOriginEmbedding,
    /// The context is not a secure one.
    InsecureContext,
    /// Organization policy.
    OrganizationPolicy,
    /// A label the user applied.
    UserLabel,
    /// A bounded pattern detector fired on the value.
    ValuePattern,
}

/// Everything except the text, which the observation itself carries.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ZoneSignals {
    /// What the control type says.
    pub input_type: InputTypeSignal,
    /// What the `autocomplete` attribute says.
    pub autocomplete: AutocompleteSignal,
    /// Whether the control's content is obscured from the user.
    pub obscured: bool,
    /// Whether the node is inside a cross-origin embedded frame.
    pub cross_origin_embedded: bool,
    /// Whether Chromium classifies the origin as potentially trustworthy.
    pub origin_is_potentially_trustworthy: bool,
    /// A classification organization policy imposes.
    pub organization_policy: Option<Sensitivity>,
    /// A classification the user applied.
    pub user_label: Option<Sensitivity>,
}

impl Default for ZoneSignals {
    /// The signals of an ordinary node in a secure, same-origin document.
    fn default() -> Self {
        Self {
            input_type: InputTypeSignal::NotAControl,
            autocomplete: AutocompleteSignal::Absent,
            obscured: false,
            cross_origin_embedded: false,
            origin_is_potentially_trustworthy: true,
            organization_policy: None,
            user_label: None,
        }
    }
}
