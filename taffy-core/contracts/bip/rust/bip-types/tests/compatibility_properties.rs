// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Properties of versioning and closed-enumeration decoding (specification
//! section 6.2).
//!
//! The rule under test is the one that decides whether the protocol is unsafe
//! or merely awkward: an unrecognised enumeration value is unsupported, and is
//! never coerced to a known member. It is proved over every enumeration in the
//! contract at once, from [`CLOSED_ENUMS`], so a message family added to the
//! schema is covered the moment it is generated rather than when somebody
//! remembers to extend this file.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use std::collections::HashSet;

use bip_types::result_code::ActionResultCode;
use bip_types::version::{
    ClosedEnum, Decoded, ProtocolVersion, UnknownEnum, VersionParseError, VersionVerdict,
    CLOSED_ENUMS,
};
use proptest::prelude::*;

fn is_declared_member(enumeration_index: usize, value: &str) -> bool {
    let enumeration = &CLOSED_ENUMS[enumeration_index];
    (enumeration.wire_values)().contains(&value)
}

proptest! {
    /// Any wire value that is not a declared member is unsupported, in every
    /// enumeration the protocol defines.
    #[test]
    fn an_undeclared_wire_value_is_unsupported_everywhere(value in ".{0,32}") {
        for (index, enumeration) in CLOSED_ENUMS.iter().enumerate() {
            if is_declared_member(index, &value) {
                continue;
            }
            prop_assert!(
                !(enumeration.is_known)(&value),
                "{} accepted an undeclared value",
                enumeration.name,
            );
        }
    }

    /// A declared member with anything appended is a different value, and a
    /// different value is unsupported. This is the shape a minor version's new
    /// member arrives in, and the near-miss a lenient decoder would round to
    /// its neighbour.
    #[test]
    fn a_near_miss_of_a_declared_member_is_unsupported(
        enumeration_index in 0usize..CLOSED_ENUMS.len(),
        member_index in 0usize..64,
        suffix in "[A-Z_]{1,8}",
    ) {
        let enumeration = &CLOSED_ENUMS[enumeration_index];
        let members = (enumeration.wire_values)();
        let member = members[member_index % members.len()];

        let appended = format!("{member}{suffix}");
        prop_assume!(!is_declared_member(enumeration_index, &appended));
        prop_assert!(!(enumeration.is_known)(&appended));

        let lowercased = member.to_lowercase();
        prop_assume!(!is_declared_member(enumeration_index, &lowercased));
        prop_assert!(!(enumeration.is_known)(&lowercased));
    }

    /// An unknown action result never decodes, and in particular never decodes
    /// to the one code that would let a caller report success.
    #[test]
    fn an_unknown_action_result_never_becomes_verified(value in ".{0,32}") {
        prop_assume!(
            !ActionResultCode::ALL
                .iter()
                .any(|code| code.wire() == value)
        );

        let decoded = ActionResultCode::decode(&value);
        prop_assert!(decoded.is_unsupported());
        prop_assert!(decoded.known().is_none());
        prop_assert_ne!(decoded, Decoded::Known(ActionResultCode::Verified));
        prop_assert_eq!(
            decoded.unsupported().map(UnknownEnum::enumeration),
            Some("ActionResultCode"),
        );
    }

    /// Compatibility is decided by the major version and nothing else.
    #[test]
    fn compatibility_follows_the_major_version(
        major_a in any::<u16>(),
        minor_a in any::<u16>(),
        major_b in any::<u16>(),
        minor_b in any::<u16>(),
    ) {
        let reader = ProtocolVersion::new(major_a, minor_a);
        let peer = ProtocolVersion::new(major_b, minor_b);

        prop_assert_eq!(reader.is_compatible_with(peer), major_a == major_b);
        prop_assert_eq!(reader.is_compatible_with(peer), peer.is_compatible_with(reader));
        prop_assert!(reader.is_compatible_with(reader));

        let verdict = reader.verdict_for(peer);
        prop_assert_eq!(verdict.accepts_message(), reader.is_compatible_with(peer));
        prop_assert_eq!(
            verdict.may_contain_unknown_fields(),
            reader.accepts_minor_addition(peer),
        );
        prop_assert_eq!(
            verdict == VersionVerdict::UnsupportedMajor,
            major_a != major_b,
        );
    }

    /// A minor addition runs one way only: both sides cannot each be the newer
    /// one, and a version never adds to itself.
    #[test]
    fn a_minor_addition_is_one_directional(
        major_a in any::<u16>(),
        minor_a in any::<u16>(),
        major_b in any::<u16>(),
        minor_b in any::<u16>(),
    ) {
        let reader = ProtocolVersion::new(major_a, minor_a);
        let peer = ProtocolVersion::new(major_b, minor_b);

        prop_assert!(!reader.accepts_minor_addition(reader));
        prop_assert!(
            !(reader.accepts_minor_addition(peer) && peer.accepts_minor_addition(reader))
        );
        // Accepting an addition implies the message is decodable at all.
        prop_assert!(!reader.accepts_minor_addition(peer) || reader.is_compatible_with(peer));
    }

    /// Parsing and formatting are inverse for every well-formed version.
    #[test]
    fn a_version_survives_its_wire_form(major in any::<u16>(), minor in any::<u16>()) {
        let version = ProtocolVersion::new(major, minor);
        prop_assert_eq!(ProtocolVersion::from_wire(&version.to_wire()), Ok(version));
        prop_assert_eq!(ProtocolVersion::parse(&version.to_string()), Ok(version));
    }

    /// A version string that is not exactly two numeric parts is rejected
    /// rather than partially understood.
    #[test]
    fn a_malformed_version_is_rejected(value in "[^.]{0,8}") {
        prop_assume!(value.parse::<u16>().is_err() || !value.is_empty());
        prop_assert_eq!(ProtocolVersion::parse(&value), Err(VersionParseError::NotTwoParts));
    }
}

#[test]
fn every_declared_member_decodes_and_re_encodes() {
    for enumeration in CLOSED_ENUMS {
        let members = (enumeration.wire_values)();
        assert!(
            !members.is_empty(),
            "{} declares no members",
            enumeration.name
        );
        for member in members {
            assert!(
                (enumeration.is_known)(member),
                "{} rejected its own member {member}",
                enumeration.name
            );
        }
    }
}

#[test]
fn every_enumeration_appears_once_and_declares_unique_members() {
    let mut names = HashSet::new();
    for enumeration in CLOSED_ENUMS {
        assert!(
            names.insert(enumeration.name),
            "{} is listed twice in the closed-enumeration registry",
            enumeration.name
        );

        let members = (enumeration.wire_values)();
        let unique: HashSet<&&str> = members.iter().collect();
        assert_eq!(
            unique.len(),
            members.len(),
            "{} declares a duplicate wire value",
            enumeration.name
        );
    }

    // The registry is the executable form of "every enumeration is closed", so
    // an empty or nearly empty one would make the properties above vacuous.
    assert!(
        CLOSED_ENUMS.len() >= 30,
        "the closed-enumeration registry looks incomplete: {} entries",
        CLOSED_ENUMS.len()
    );
}

#[test]
fn the_generated_protocol_version_parses() {
    let current = ProtocolVersion::current().expect("the generated version constant must parse");
    assert_eq!(
        current.to_wire().0,
        bip_types::PROTOCOL_VERSION,
        "the parsed version must round-trip to the generated constant"
    );
    assert!(current.is_compatible_with(current));
}

/// Sibling crates decode through the trait rather than through each generated
/// type by name; this is that call shape, exercised once so it stays usable.
fn decode_or_fail_closed<T: ClosedEnum>(value: &str) -> Decoded<T> {
    T::decode_wire(value)
}

#[test]
fn the_trait_decodes_any_closed_enumeration_uniformly() {
    use bip_types::snapshot::Sensitivity;

    assert_eq!(
        decode_or_fail_closed::<Sensitivity>("CREDENTIAL"),
        Decoded::Known(Sensitivity::Credential)
    );
    assert!(decode_or_fail_closed::<Sensitivity>("SLIGHTLY_SENSITIVE").is_unsupported());
    assert!(decode_or_fail_closed::<ActionResultCode>("ALMOST_VERIFIED").is_unsupported());
}

#[test]
fn the_unsupported_outcome_carries_no_page_supplied_text() {
    // The rejected value came from a renderer. Only the compiled-in enumeration
    // name is retained, so nothing untrusted reaches a log, a metric, or a
    // user-facing string.
    let decoded = ActionResultCode::decode("<script>alert(1)</script>");
    let unknown = decoded
        .unsupported()
        .expect("an undeclared value must be unsupported");
    assert_eq!(unknown.enumeration(), "ActionResultCode");
    assert!(!unknown.to_string().contains("script"));
}
