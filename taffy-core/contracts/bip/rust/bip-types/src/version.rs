// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Protocol version and closed-enumeration decoding (specification section
//! 6.2).
//!
//! Two refusals live here, and both are refusals a decoder must not be able to
//! forget.
//!
//! A message written at an incompatible major version is rejected on the
//! version alone, before its content is examined. A wire value outside a closed
//! enumeration is unsupported: it is never coerced to the nearest known member,
//! never treated as the least restrictive member, and never dropped. The
//! generated `from_wire` says so by returning `None`; [`Decoded`] says so as a
//! value the caller has to destructure.
//!
//! Accepting is not the same as understanding. A decoder accepts a newer minor
//! version's unknown optional fields, but a delta subscriber that meets one
//! inside a page delta still discards its projection and takes a fresh
//! snapshot, because it cannot prove the projection is still correct. That
//! decision belongs to the delta consumer; this module only tells it which case
//! it is in.

use core::fmt;

pub use crate::generated::version::{PROTOCOL_STATUS, PROTOCOL_VERSION};

use crate::generated::identity::ProtocolVersion as WireProtocolVersion;
use crate::generated::{action, delta, identity, protocol_info, snapshot};

/// A parsed `major.minor` protocol version.
///
/// The wire form is a string ([`WireProtocolVersion`]); this is the form
/// compatibility rules are evaluated on. The number itself is owned by
/// `taffy-core/contracts/bip/schema/bip.version.json` and reaches Rust only through the
/// generated [`PROTOCOL_VERSION`] constant, so no source file in this crate
/// restates it.
///
/// Ordering is defined here, unlike anywhere in [`crate::identity`]: versions
/// really are ordered, major before minor.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ProtocolVersion {
    /// A change to required meaning, or a removal of compatibility.
    pub major: u16,
    /// An addition of optional fields, enumeration members, adapters, or
    /// action types.
    pub minor: u16,
}

/// Why a `major.minor` string could not be parsed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum VersionParseError {
    /// The value is not exactly two dot-separated parts.
    NotTwoParts,
    /// The major part is empty, non-numeric, or out of range.
    Major,
    /// The minor part is empty, non-numeric, or out of range.
    Minor,
}

impl fmt::Display for VersionParseError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::NotTwoParts => "a protocol version must be exactly major.minor",
            Self::Major => "the major part is not a number in range",
            Self::Minor => "the minor part is not a number in range",
        };
        formatter.write_str(text)
    }
}

/// The verdict a decoder must reach about a peer's protocol version.
///
/// Every variant except [`Self::UnsupportedMajor`] means "decode the message".
/// [`Self::UnsupportedMajor`] means "reject it on the version alone", which is
/// the fail-closed half of section 6.2.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum VersionVerdict {
    /// Identical versions.
    Exact,
    /// Same major, older minor. Every field this reader knows exists.
    OlderMinor,
    /// Same major, newer minor. The reader accepts the message and ignores
    /// optional fields it does not know, but treats an unknown enumeration
    /// member as unsupported all the same.
    MinorAddition,
    /// Different major. Required meaning may have changed or been removed, so
    /// the message is rejected without being interpreted.
    UnsupportedMajor,
}

impl VersionVerdict {
    /// Whether a decoder may go on to interpret the message.
    pub fn accepts_message(self) -> bool {
        !matches!(self, Self::UnsupportedMajor)
    }

    /// Whether the message may contain optional fields this reader does not
    /// know.
    pub fn may_contain_unknown_fields(self) -> bool {
        matches!(self, Self::MinorAddition)
    }
}

impl ProtocolVersion {
    /// Builds a version from its parts.
    pub const fn new(major: u16, minor: u16) -> Self {
        Self { major, minor }
    }

    /// The version these bindings speak, parsed from the generated constant.
    ///
    /// Fallible because parsing is fallible, not because the generated value is
    /// in doubt: the same code path parses a peer's version, and a total
    /// function is worth more here than a convenient one. A unit test asserts
    /// this returns `Ok`.
    pub fn current() -> Result<Self, VersionParseError> {
        Self::parse(PROTOCOL_VERSION)
    }

    /// Parses a `major.minor` string.
    pub fn parse(value: &str) -> Result<Self, VersionParseError> {
        let mut parts = value.split('.');
        let (Some(major), Some(minor), None) = (parts.next(), parts.next(), parts.next()) else {
            return Err(VersionParseError::NotTwoParts);
        };
        Ok(Self {
            major: major.parse().map_err(|_| VersionParseError::Major)?,
            minor: minor.parse().map_err(|_| VersionParseError::Minor)?,
        })
    }

    /// Parses the wire form carried in a message.
    pub fn from_wire(value: &WireProtocolVersion) -> Result<Self, VersionParseError> {
        Self::parse(&value.0)
    }

    /// The wire form to put in a message.
    pub fn to_wire(self) -> WireProtocolVersion {
        WireProtocolVersion(self.to_string())
    }

    /// Whether a reader at `self` may interpret a message written at `other`.
    ///
    /// Compatibility is decided by the major version and nothing else. A minor
    /// difference in either direction is compatible: an older reader ignores
    /// what it does not know, and a newer reader finds every field it expects
    /// or treats an absent optional field as absent.
    pub fn is_compatible_with(self, other: Self) -> bool {
        self.major == other.major
    }

    /// Whether `other` is this version plus minor additions this reader does
    /// not know.
    ///
    /// True only for the same major version and a strictly greater minor. It
    /// answers "may this message contain optional fields I have never heard
    /// of?" — not "may I guess what they mean". A delta consumer that meets
    /// one still invalidates its projection.
    pub fn accepts_minor_addition(self, other: Self) -> bool {
        self.major == other.major && other.minor > self.minor
    }

    /// The full verdict for a peer version.
    pub fn verdict_for(self, peer: Self) -> VersionVerdict {
        if self.major != peer.major {
            VersionVerdict::UnsupportedMajor
        } else if peer.minor > self.minor {
            VersionVerdict::MinorAddition
        } else if peer.minor < self.minor {
            VersionVerdict::OlderMinor
        } else {
            VersionVerdict::Exact
        }
    }
}

impl fmt::Display for ProtocolVersion {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}.{}", self.major, self.minor)
    }
}

/// A wire value that is outside its closed enumeration.
///
/// It records which enumeration refused the value and never the value itself.
/// The rejected text came from a renderer, so keeping it would carry untrusted
/// page-derived content into logs and telemetry, which section 16 forbids and
/// section 11.7 forbids showing to a user. The enumeration name is a compiled-in
/// string, safe to count and safe to report.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct UnknownEnum {
    enumeration: &'static str,
}

impl UnknownEnum {
    /// Records that a value outside `enumeration` was received.
    pub const fn in_enumeration(enumeration: &'static str) -> Self {
        Self { enumeration }
    }

    /// The schema name of the enumeration that refused the value.
    pub const fn enumeration(self) -> &'static str {
        self.enumeration
    }
}

impl fmt::Display for UnknownEnum {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "unsupported value for closed enumeration {}",
            self.enumeration
        )
    }
}

/// The outcome of decoding one closed-enumeration field.
///
/// There is no accessor that produces a `T` from [`Self::Unsupported`]. That is
/// the whole point: a caller cannot reach for a default, a nearest member, or
/// the least restrictive member, because none is offered.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Decoded<T> {
    /// The value is a member of the enumeration.
    Known(T),
    /// The value is outside the enumeration. The message is unsupported and the
    /// caller must fail closed.
    Unsupported(UnknownEnum),
}

impl<T> Decoded<T> {
    /// The decoded member, or `None` when the value was unsupported.
    pub fn known(self) -> Option<T> {
        match self {
            Self::Known(value) => Some(value),
            Self::Unsupported(_) => None,
        }
    }

    /// Which enumeration refused the value, or `None` when it was known.
    pub fn unsupported(self) -> Option<UnknownEnum> {
        match self {
            Self::Known(_) => None,
            Self::Unsupported(unknown) => Some(unknown),
        }
    }

    /// Whether the caller must fail closed.
    pub fn is_unsupported(&self) -> bool {
        matches!(self, Self::Unsupported(_))
    }
}

/// A closed enumeration of the protocol.
///
/// Every enumeration in `taffy-core/contracts/bip/schema/` is closed and implements this.
/// The trait exists so a consumer can decode any of them through one refusal
/// shape, and so a compatibility test can walk all of them without naming each
/// by hand.
pub trait ClosedEnum: Copy + Sized + 'static {
    /// The enumeration's schema definition name.
    const NAME: &'static str;

    /// Every member, in the order the schema declares them.
    const MEMBERS: &'static [Self];

    /// The wire value of a member.
    fn wire_value(self) -> &'static str;

    /// The generated closed-set parse. `None` means the value is outside the
    /// enumeration.
    fn parse_wire(value: &str) -> Option<Self>;

    /// Decodes a wire value into an explicit known-or-unsupported outcome.
    ///
    /// This is the only decoding entry point a consumer should use, because it
    /// makes the unsupported case a value rather than a `None` that is easy to
    /// turn into a default.
    fn decode_wire(value: &str) -> Decoded<Self> {
        match Self::parse_wire(value) {
            Some(member) => Decoded::Known(member),
            None => Decoded::Unsupported(UnknownEnum::in_enumeration(Self::NAME)),
        }
    }
}

/// One closed enumeration, with its type erased.
///
/// A compatibility gate has to prove the fail-closed rule for every enumeration
/// in the protocol, not for the ones someone remembered to list in a test. See
/// [`CLOSED_ENUMS`].
#[derive(Clone, Copy)]
pub struct ClosedEnumeration {
    /// The enumeration's schema definition name.
    pub name: &'static str,
    /// Every wire value it accepts, in schema order.
    pub wire_values: fn() -> Vec<&'static str>,
    /// Whether a wire value is a known member. `false` means the message is
    /// unsupported and the caller must fail closed.
    pub is_known: fn(&str) -> bool,
}

impl fmt::Debug for ClosedEnumeration {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("ClosedEnumeration")
            .field("name", &self.name)
            .finish_non_exhaustive()
    }
}

fn wire_values_of<T: ClosedEnum>() -> Vec<&'static str> {
    T::MEMBERS
        .iter()
        .map(|member| member.wire_value())
        .collect()
}

fn is_known_in<T: ClosedEnum>(value: &str) -> bool {
    !T::decode_wire(value).is_unsupported()
}

macro_rules! closed_enums {
    ($($name:literal => $ty:path),+ $(,)?) => {
        $(
            impl ClosedEnum for $ty {
                const NAME: &'static str = $name;
                const MEMBERS: &'static [Self] = <$ty>::ALL;

                fn wire_value(self) -> &'static str {
                    <$ty>::wire(self)
                }

                fn parse_wire(value: &str) -> Option<Self> {
                    <$ty>::from_wire(value)
                }
            }
        )+

        /// Every closed enumeration in the protocol, in schema-document order.
        ///
        /// The list is the executable form of "every enumeration is closed":
        /// a test walks it and proves that no member of any enumeration decodes
        /// an unrecognised value into a known member.
        pub const CLOSED_ENUMS: &[ClosedEnumeration] = &[
            $(
                ClosedEnumeration {
                    name: $name,
                    wire_values: wire_values_of::<$ty>,
                    is_known: is_known_in::<$ty>,
                },
            )+
        ];
    };
}

closed_enums! {
    "DigestAlgorithm" => identity::DigestAlgorithm,
    "DocumentLifecycleState" => identity::DocumentLifecycleState,
    "OriginKind" => identity::OriginKind,
    "AdapterKind" => protocol_info::AdapterKind,
    "ObservationScope" => protocol_info::ObservationScope,
    "RedactionFeature" => protocol_info::RedactionFeature,
    "AdapterRequirementLevel" => snapshot::AdapterRequirementLevel,
    "AdapterStatus" => snapshot::AdapterStatus,
    "AttributeName" => snapshot::AttributeName,
    "BudgetKind" => snapshot::BudgetKind,
    "ChallengeKind" => snapshot::ChallengeKind,
    "ContentSignal" => snapshot::ContentSignal,
    "ContentTrust" => snapshot::ContentTrust,
    "FrameExclusionReason" => snapshot::FrameExclusionReason,
    "NodeState" => snapshot::NodeState,
    "RelationshipKind" => snapshot::RelationshipKind,
    "SemanticField" => snapshot::SemanticField,
    "SemanticRole" => snapshot::SemanticRole,
    "Sensitivity" => snapshot::Sensitivity,
    "SourceKind" => snapshot::SourceKind,
    "Transformation" => snapshot::Transformation,
    "UrlDisclosure" => snapshot::UrlDisclosure,
    "ValueKind" => snapshot::ValueKind,
    "WarningCode" => snapshot::WarningCode,
    "BackpressureAction" => delta::BackpressureAction,
    "DeltaCategory" => delta::DeltaCategory,
    "InvalidationReason" => delta::InvalidationReason,
    "ActionInputKind" => action::ActionInputKind,
    "ActionResultCode" => action::ActionResultCode,
    "ActionType" => action::ActionType,
    "IdempotencyPolicy" => action::IdempotencyPolicy,
    "PostconditionKind" => action::PostconditionKind,
    "PreconditionKind" => action::PreconditionKind,
    "PrincipalKind" => action::PrincipalKind,
    "VerifierKind" => action::VerifierKind,
}

#[cfg(test)]
mod tests {
    use super::{
        ClosedEnum, Decoded, ProtocolVersion, VersionParseError, VersionVerdict, CLOSED_ENUMS,
        PROTOCOL_STATUS, PROTOCOL_VERSION,
    };
    use crate::generated::identity::DocumentLifecycleState;
    use std::collections::BTreeSet;

    #[test]
    fn the_generated_constants_are_present_and_parseable() {
        assert!(!PROTOCOL_VERSION.is_empty());
        assert!(!PROTOCOL_STATUS.is_empty());
        assert_eq!(
            ProtocolVersion::current(),
            ProtocolVersion::parse(PROTOCOL_VERSION),
        );
        assert!(ProtocolVersion::current().is_ok());
    }

    #[test]
    fn a_version_is_rejected_unless_it_is_exactly_two_numeric_parts() {
        assert_eq!(
            ProtocolVersion::parse("1.2"),
            Ok(ProtocolVersion::new(1, 2))
        );
        assert_eq!(
            ProtocolVersion::parse("1"),
            Err(VersionParseError::NotTwoParts)
        );
        assert_eq!(
            ProtocolVersion::parse("1.2.3"),
            Err(VersionParseError::NotTwoParts)
        );
        assert_eq!(ProtocolVersion::parse("x.2"), Err(VersionParseError::Major));
        assert_eq!(ProtocolVersion::parse("1.y"), Err(VersionParseError::Minor));
        assert_eq!(
            ProtocolVersion::parse("-1.0"),
            Err(VersionParseError::Major)
        );
        assert_eq!(
            ProtocolVersion::parse(""),
            Err(VersionParseError::NotTwoParts)
        );
    }

    #[test]
    fn a_newer_minor_is_accepted_and_a_different_major_is_not() {
        let reader = ProtocolVersion::new(1, 4);

        assert_eq!(
            reader.verdict_for(ProtocolVersion::new(1, 4)),
            VersionVerdict::Exact
        );
        assert_eq!(
            reader.verdict_for(ProtocolVersion::new(1, 9)),
            VersionVerdict::MinorAddition
        );
        assert_eq!(
            reader.verdict_for(ProtocolVersion::new(1, 0)),
            VersionVerdict::OlderMinor
        );
        assert_eq!(
            reader.verdict_for(ProtocolVersion::new(2, 0)),
            VersionVerdict::UnsupportedMajor
        );

        assert!(reader.accepts_minor_addition(ProtocolVersion::new(1, 9)));
        // A newer major is not a minor addition, however small the minor step.
        assert!(!reader.accepts_minor_addition(ProtocolVersion::new(2, 5)));
    }

    #[test]
    fn an_unsupported_major_is_rejected_before_the_message_is_read() {
        let verdict = ProtocolVersion::new(1, 0).verdict_for(ProtocolVersion::new(2, 0));
        assert!(!verdict.accepts_message());
        assert!(!verdict.may_contain_unknown_fields());
    }

    #[test]
    fn an_unknown_enumeration_value_has_no_route_back_to_a_member() {
        let decoded = DocumentLifecycleState::decode_wire("MOSTLY_ACTIVE");
        assert!(decoded.is_unsupported());
        assert_eq!(decoded.known(), None);
        assert_eq!(
            decoded.unsupported().map(super::UnknownEnum::enumeration),
            Some("DocumentLifecycleState"),
        );

        // The one value a lenient decoder would reach for stays unreachable.
        assert_ne!(decoded, Decoded::Known(DocumentLifecycleState::Active));
    }

    #[test]
    fn the_registry_covers_every_generated_enumeration() {
        // The registry is hand-maintained, because Rust cannot enumerate its
        // own types. What it is checked against must therefore come from the
        // schema, not from a number somebody remembered to raise: a count
        // asserted against a literal passes unchanged when an enumeration is
        // added to the schema and left out of the registry, which is exactly
        // the case it exists to catch. The generator emits the roster beside
        // the types, so this comparison moves on its own.
        let registered: BTreeSet<&str> = CLOSED_ENUMS.iter().map(|entry| entry.name).collect();
        let generated: BTreeSet<&str> = crate::generated::GENERATED_ENUMERATIONS
            .iter()
            .copied()
            .collect();
        assert_eq!(
            generated.difference(&registered).collect::<Vec<_>>(),
            Vec::<&&str>::new(),
            "a generated enumeration is missing from CLOSED_ENUMS, so the \
             fail-closed property is proved over less than the contract"
        );
        assert_eq!(
            registered.difference(&generated).collect::<Vec<_>>(),
            Vec::<&&str>::new(),
            "CLOSED_ENUMS names an enumeration the generator no longer emits"
        );
    }
}
