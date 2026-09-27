// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The sensitivity lattice and the never-extract value classes (specification
//! section 9).
//!
//! Classification is conservative and it only ever goes up. Evidence combines
//! by union, so adding a signal can raise the handling a value requires and can
//! never lower it — which is the executable form of "page instructions cannot
//! lower sensitivity". There is no operation in this module that removes a
//! classification from a set.
//!
//! Three rules from section 9 are encoded rather than described:
//!
//! - [`Sensitivity::Credential`], [`Sensitivity::OneTimeCode`],
//!   [`Sensitivity::ChallengeResponse`], and every [`NeverExtractClass`] resolve to
//!   [`Handling::Withhold`] at every destination, with no exception parameter
//!   and no policy that can widen it (section 9.1);
//! - [`Sensitivity::UnknownSensitive`] resolves to the strictest handling the
//!   destination requires of any class it can accept, never to the least
//!   restrictive one (section 9.2);
//! - the rich local observation and the remote-model projection are different
//!   destinations with different answers, so a caller cannot serialize the
//!   whole local snapshot by convenience (section 9.3).
//!
//! What this module does not do is decide policy. It says what handling a
//! classification requires; `policy-engine` owns whether a given task, origin,
//! profile, or organization setting permits that handling at all, and may only
//! make the answer stricter.

pub use crate::generated::snapshot::Sensitivity;

/// The classifications that name a kind of sensitive content, in schema order.
///
/// [`Sensitivity::NotSensitive`] is the bottom of the lattice and names
/// nothing; [`Sensitivity::Credential`] is never-extract and is handled by its
/// own rule; [`Sensitivity::UnknownSensitive`] is the deliberate absence of a
/// name. The nine that remain are the classes a destination policy can be
/// written about.
pub const NAMED_SENSITIVE: &[Sensitivity] = &[
    Sensitivity::Personal,
    Sensitivity::Account,
    Sensitivity::Payment,
    Sensitivity::Identity,
    Sensitivity::Health,
    Sensitivity::Financial,
    Sensitivity::Legal,
    Sensitivity::PrivateCommunication,
    Sensitivity::Administration,
];

/// What a destination may do with an observed value.
///
/// Ordered from permissive to strict, so combining requirements is a maximum
/// and can never relax one of them.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Handling {
    /// The value may be included as observed.
    Allowed,
    /// The value may be included only after field selection and redaction for
    /// this destination.
    MinimizeAndRedact,
    /// The value must not be included in any form, redacted or otherwise.
    Withhold,
}

impl Handling {
    /// Whether the value must not reach the destination at all.
    pub fn withholds(self) -> bool {
        matches!(self, Self::Withhold)
    }
}

/// Where an observed value is about to go.
///
/// The local core service normally receives a rich observation while the
/// remote-model projection carries only selected, minimized, redacted fields.
/// Section 9.3 requires that distinction to stay explicit in types, so the
/// destination is a parameter of every question this module answers rather than
/// an assumption baked into one answer.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DestinationPolicy {
    /// The sandboxed core service, holding the local projection.
    LocalCoreService,
    /// A durable local record: task journal, audit event, stored evidence.
    LocalRecord,
    /// A model provider outside the device.
    RemoteModel,
    /// Product analytics. Section 16 keeps observed values out of it entirely;
    /// counts, identifiers, and enumeration names are not values and are
    /// unaffected by this classifier.
    Telemetry,
}

impl DestinationPolicy {
    /// The handling this destination requires for content carrying no
    /// sensitivity classification.
    pub fn handling_of_ordinary_content(self) -> Handling {
        match self {
            Self::LocalCoreService | Self::LocalRecord | Self::RemoteModel => Handling::Allowed,
            Self::Telemetry => Handling::Withhold,
        }
    }

    /// The handling this destination requires for a named sensitive class.
    pub fn handling_of_named_sensitive(self) -> Handling {
        match self {
            Self::LocalCoreService => Handling::Allowed,
            Self::LocalRecord | Self::RemoteModel => Handling::MinimizeAndRedact,
            Self::Telemetry => Handling::Withhold,
        }
    }

    /// The strictest handling this destination requires of any class it can
    /// accept.
    ///
    /// This is what [`Sensitivity::UnknownSensitive`] resolves to: an
    /// unclassified value is handled at least as strictly as the destination's
    /// policy requires, so it is treated as though it were whichever acceptable
    /// class demands the most.
    pub fn strictest_acceptable_handling(self) -> Handling {
        self.handling_of_ordinary_content()
            .max(self.handling_of_named_sensitive())
    }
}

impl Sensitivity {
    /// The handling this single classification requires at `destination`.
    ///
    /// [`Self::Credential`] withholds everywhere. [`Self::UnknownSensitive`]
    /// resolves to [`DestinationPolicy::strictest_acceptable_handling`], never
    /// to the least restrictive known member.
    pub fn handling_for(self, destination: DestinationPolicy) -> Handling {
        match self {
            Self::NotSensitive => destination.handling_of_ordinary_content(),
            Self::Credential | Self::OneTimeCode | Self::ChallengeResponse => Handling::Withhold,
            Self::UnknownSensitive => destination.strictest_acceptable_handling(),
            Self::Personal
            | Self::Account
            | Self::Payment
            | Self::Identity
            | Self::Health
            | Self::Financial
            | Self::Legal
            | Self::PrivateCommunication
            | Self::Administration => destination.handling_of_named_sensitive(),
        }
    }

    /// Whether this classification forbids emitting the value in plaintext at
    /// any destination (section 9.1).
    pub fn is_never_extract(self) -> bool {
        matches!(
            self,
            Self::Credential | Self::OneTimeCode | Self::ChallengeResponse
        )
    }

    /// The lattice bit for this classification.
    ///
    /// [`Self::NotSensitive`] is the bottom element and carries no bit, so a
    /// set that contains nothing is exactly a value classified as not
    /// sensitive.
    const fn bit(self) -> u16 {
        match self {
            Self::NotSensitive => 0,
            Self::Personal => 1 << 0,
            Self::Account => 1 << 1,
            Self::Payment => 1 << 2,
            Self::Identity => 1 << 3,
            Self::Health => 1 << 4,
            Self::Financial => 1 << 5,
            Self::Legal => 1 << 6,
            Self::PrivateCommunication => 1 << 7,
            Self::Administration => 1 << 8,
            Self::Credential => 1 << 9,
            Self::UnknownSensitive => 1 << 10,
            Self::OneTimeCode => 1 << 11,
            Self::ChallengeResponse => 1 << 12,
        }
    }
}

/// A point in the sensitivity lattice: every classification a value has drawn.
///
/// Classification signals arrive from several places — form type, autocomplete
/// tokens, role, label, origin, security context, obscured state, cross-origin
/// embedding, organization policy, user labels, and bounded pattern detectors —
/// and no single one is authoritative. The lattice element is therefore the set
/// of everything that fired, joined by union:
///
/// - [`Self::EMPTY`] is the bottom element and means not sensitive;
/// - [`Self::join`] is the least upper bound: commutative, associative,
///   idempotent, and never smaller than either input;
/// - [`Self::is_at_least_as_strict_as`] is the partial order, which is set
///   containment.
///
/// There is no complement, no difference, and no removal, because section 9
/// allows a classification to be raised and never lowered.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[must_use]
pub struct SensitivitySet(u16);

impl SensitivitySet {
    /// The bottom of the lattice: no classification, meaning not sensitive.
    pub const EMPTY: Self = Self(0);

    /// The set holding one classification.
    pub const fn of(classification: Sensitivity) -> Self {
        Self(classification.bit())
    }

    /// The least upper bound of two elements: everything either one carries.
    pub const fn join(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    /// This element joined with one more classification.
    ///
    /// The result is never less strict than the input, whatever is added.
    pub const fn with(self, classification: Sensitivity) -> Self {
        self.join(Self::of(classification))
    }

    /// Whether this element carries `classification`.
    ///
    /// [`Sensitivity::NotSensitive`] carries no bit, so asking for it asks
    /// whether the set is empty.
    pub const fn contains(self, classification: Sensitivity) -> bool {
        let bit = classification.bit();
        if bit == 0 {
            self.0 == 0
        } else {
            self.0 & bit == bit
        }
    }

    /// Whether nothing has classified this value as sensitive.
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }

    /// Whether this element requires at least the handling `other` requires,
    /// everywhere.
    ///
    /// This is the lattice's partial order. It holds exactly when this element
    /// carries every classification `other` carries, which is what makes
    /// [`Self::join`] safe to apply to evidence from sources of differing
    /// trust: the result is at least as strict as each of them.
    pub const fn is_at_least_as_strict_as(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }

    /// Whether any classification here forbids emitting the value in plaintext
    /// (section 9.1).
    pub const fn is_never_extract(self) -> bool {
        self.contains(Sensitivity::Credential)
            || self.contains(Sensitivity::OneTimeCode)
            || self.contains(Sensitivity::ChallengeResponse)
    }

    /// Every classification in this element, in schema order.
    ///
    /// An empty element yields an empty list, not
    /// [`Sensitivity::NotSensitive`]: the absence of a classification is not a
    /// classification.
    pub fn members(self) -> Vec<Sensitivity> {
        Sensitivity::ALL
            .iter()
            .copied()
            .filter(|classification| {
                let bit = classification.bit();
                bit != 0 && self.0 & bit == bit
            })
            .collect()
    }

    /// The handling this element requires at `destination`.
    ///
    /// The strictest requirement of any classification present wins, and an
    /// empty element takes the destination's ordinary-content handling.
    pub fn handling_for(self, destination: DestinationPolicy) -> Handling {
        self.members().into_iter().fold(
            destination.handling_of_ordinary_content(),
            |strictest, c| strictest.max(c.handling_for(destination)),
        )
    }
}

impl FromIterator<Sensitivity> for SensitivitySet {
    fn from_iter<I: IntoIterator<Item = Sensitivity>>(iter: I) -> Self {
        iter.into_iter().fold(Self::EMPTY, Self::with)
    }
}

impl Extend<Sensitivity> for SensitivitySet {
    fn extend<I: IntoIterator<Item = Sensitivity>>(&mut self, iter: I) {
        for classification in iter {
            *self = self.with(classification);
        }
    }
}

/// A value class the protocol must never emit in plaintext (section 9.1).
///
/// The graph may say that a field of this class is present, because that is
/// often useful and reveals nothing. It may not carry the current value, a
/// suggested value, or anything derived from the secret — including its length,
/// which is why [`Self::structural_placeholder`] returns a fixed local template
/// rather than anything built from what was observed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum NeverExtractClass {
    /// A password field.
    Password,
    /// A passcode field.
    Passcode,
    /// A personal identification number.
    PersonalIdentificationNumber,
    /// A card verification value.
    CardVerificationValue,
    /// A one-time code.
    OneTimeCode,
    /// A short response a person reads from a page challenge.
    ChallengeResponse,
    /// An account recovery code.
    RecoveryCode,
    /// A passkey.
    Passkey,
    /// A private key.
    PrivateKey,
    /// A wallet seed phrase.
    SeedPhrase,
    /// An authentication token.
    AuthenticationToken,
    /// A session cookie.
    SessionCookie,
    /// A suggestion offered by the browser password manager.
    PasswordManagerSuggestion,
    /// A payload supplied by a platform credential provider.
    PlatformCredentialPayload,
}

impl NeverExtractClass {
    /// Every class, in the order section 9.1 lists them.
    pub const ALL: &'static [Self] = &[
        Self::Password,
        Self::Passcode,
        Self::PersonalIdentificationNumber,
        Self::CardVerificationValue,
        Self::OneTimeCode,
        Self::ChallengeResponse,
        Self::RecoveryCode,
        Self::Passkey,
        Self::PrivateKey,
        Self::SeedPhrase,
        Self::AuthenticationToken,
        Self::SessionCookie,
        Self::PasswordManagerSuggestion,
        Self::PlatformCredentialPayload,
    ];

    /// The lattice classification this never-extract class carries.
    pub const fn sensitivity(self) -> Sensitivity {
        match self {
            Self::OneTimeCode => Sensitivity::OneTimeCode,
            Self::ChallengeResponse => Sensitivity::ChallengeResponse,
            Self::Password
            | Self::Passcode
            | Self::PersonalIdentificationNumber
            | Self::CardVerificationValue
            | Self::RecoveryCode
            | Self::Passkey
            | Self::PrivateKey
            | Self::SeedPhrase
            | Self::AuthenticationToken
            | Self::SessionCookie
            | Self::PasswordManagerSuggestion
            | Self::PlatformCredentialPayload => Sensitivity::Credential,
        }
    }

    /// The lattice element every never-extract class carries.
    pub const fn sensitivity_set(self) -> SensitivitySet {
        SensitivitySet::of(self.sensitivity())
    }

    /// The handling this class requires at `destination`, which is always
    /// [`Handling::Withhold`].
    ///
    /// The destination is a parameter so the signature matches the rest of this
    /// module, not because any destination can change the answer.
    pub const fn handling_for(self, _destination: DestinationPolicy) -> Handling {
        Handling::Withhold
    }

    /// The structural placeholder the graph may carry in place of the value.
    ///
    /// A trusted local template. It is built from the class alone, so it cannot
    /// leak the value, a suggestion, or a length-derived fingerprint, and it
    /// carries no renderer-supplied text.
    pub const fn structural_placeholder(self) -> &'static str {
        match self {
            Self::Password => "password field present",
            Self::Passcode => "passcode field present",
            Self::PersonalIdentificationNumber => "personal identification number field present",
            Self::CardVerificationValue => "card verification value field present",
            Self::OneTimeCode => "one-time code field present",
            Self::ChallengeResponse => "challenge response field present",
            Self::RecoveryCode => "recovery code field present",
            Self::Passkey => "passkey present",
            Self::PrivateKey => "private key present",
            Self::SeedPhrase => "seed phrase field present",
            Self::AuthenticationToken => "authentication token present",
            Self::SessionCookie => "session cookie present",
            Self::PasswordManagerSuggestion => "saved sign-in suggestion present",
            Self::PlatformCredentialPayload => "platform credential present",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{
        DestinationPolicy, Handling, NeverExtractClass, Sensitivity, SensitivitySet,
        NAMED_SENSITIVE,
    };

    #[test]
    fn the_empty_element_is_exactly_not_sensitive() {
        assert!(SensitivitySet::EMPTY.is_empty());
        assert!(SensitivitySet::EMPTY.contains(Sensitivity::NotSensitive));
        assert_eq!(
            SensitivitySet::of(Sensitivity::NotSensitive),
            SensitivitySet::EMPTY
        );
        assert!(SensitivitySet::EMPTY.members().is_empty());
    }

    #[test]
    fn every_named_class_occupies_its_own_place_in_the_lattice() {
        for class in NAMED_SENSITIVE {
            let element = SensitivitySet::of(*class);
            assert_eq!(element.members(), vec![*class]);
            assert!(!element.is_empty());
            assert!(!element.is_never_extract());
        }
    }

    #[test]
    fn an_unclassified_value_is_handled_as_strictly_as_a_named_one() {
        for destination in [
            DestinationPolicy::LocalCoreService,
            DestinationPolicy::LocalRecord,
            DestinationPolicy::RemoteModel,
            DestinationPolicy::Telemetry,
        ] {
            let unknown = Sensitivity::UnknownSensitive.handling_for(destination);
            assert_eq!(unknown, destination.strictest_acceptable_handling());
            for class in NAMED_SENSITIVE {
                assert!(unknown >= class.handling_for(destination));
            }
        }
    }

    #[test]
    fn a_credential_is_never_extracted_however_it_is_combined() {
        let element = SensitivitySet::of(Sensitivity::Personal)
            .with(Sensitivity::Credential)
            .with(Sensitivity::NotSensitive);

        assert!(element.is_never_extract());
        assert_eq!(
            element.handling_for(DestinationPolicy::LocalCoreService),
            Handling::Withhold
        );
    }

    #[test]
    fn a_placeholder_exists_for_every_never_extract_class_and_repeats_none() {
        let mut placeholders: Vec<&str> = NeverExtractClass::ALL
            .iter()
            .map(|class| class.structural_placeholder())
            .collect();
        let count = placeholders.len();
        placeholders.sort_unstable();
        placeholders.dedup();
        assert_eq!(placeholders.len(), count);
        assert_eq!(count, NeverExtractClass::ALL.len());
    }

    #[test]
    fn handling_orders_from_permissive_to_strict() {
        assert!(Handling::Allowed < Handling::MinimizeAndRedact);
        assert!(Handling::MinimizeAndRedact < Handling::Withhold);
        assert!(Handling::Withhold.withholds());
        assert!(!Handling::MinimizeAndRedact.withholds());
    }
}
