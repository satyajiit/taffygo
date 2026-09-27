// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded identity strings.
//!
//! An asset identity reaches this crate from a generated catalog and reaches
//! the browser as part of a request for bytes. Both ends need it to be short,
//! printable, and free of anything that changes meaning when it is joined onto
//! a path — so the constructor is the only way to make one, and it refuses
//! rather than sanitizing. A value that had to be cleaned up is a value the two
//! ends would have disagreed about.

use core::fmt;

/// The longest an asset identity may be.
pub const MAX_ASSET_ID_BYTES: usize = 64;

/// The longest a revision may be.
pub const MAX_REVISION_BYTES: usize = 64;

/// Why an identity was refused.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IdentityError {
    /// Nothing was supplied.
    Empty,
    /// Longer than the maximum for its kind.
    TooLong,
    /// Carries a byte outside the permitted alphabet.
    ForbiddenByte,
    /// Starts or ends with a separator, or repeats one.
    MalformedSeparator,
}

impl fmt::Display for IdentityError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::Empty => "empty",
            Self::TooLong => "too long",
            Self::ForbiddenByte => "forbidden byte",
            Self::MalformedSeparator => "malformed separator",
        };
        formatter.write_str(text)
    }
}

/// An asset's stable identity, such as `python-stdlib`.
///
/// Lowercase ASCII letters, digits and single interior hyphens. That alphabet
/// is a deliberate subset of what a path segment permits: it cannot be `.` or
/// `..`, cannot introduce a second path segment, and cannot need escaping in a
/// URL, a file name or an audit record.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct AssetId(String);

impl AssetId {
    /// Parses `text`, or says why it is not an identity.
    pub fn parse(text: &str) -> Result<Self, IdentityError> {
        check_alphabet(text, MAX_ASSET_ID_BYTES, b'-', |byte| {
            byte.is_ascii_lowercase() || byte.is_ascii_digit()
        })?;
        Ok(Self(text.to_owned()))
    }

    /// The identity as it was written.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for AssetId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// One published revision of an asset, such as `3.14.2-taffy.1`.
///
/// Wider than an identity because a revision quotes an upstream version, but
/// still closed: lowercase letters, digits, and the two separators a version
/// uses.
///
/// `+` is deliberately absent, although a version scheme would allow it and
/// this parser once did. A revision is embedded in its own delivery path by
/// every builder in the tree, and `IsRelativePath` in the browser refuses `+`
/// in a path segment — so a revision carrying one produces a catalog three
/// checkers accept and a phone rejects, naming no character. Narrowing the
/// revision costs a hyphen; widening the browser's path alphabet would mean
/// accepting a character no artifact this product builds needs.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct AssetRevision(String);

impl AssetRevision {
    /// Parses `text`, or says why it is not a revision.
    pub fn parse(text: &str) -> Result<Self, IdentityError> {
        if text.is_empty() {
            return Err(IdentityError::Empty);
        }
        if text.len() > MAX_REVISION_BYTES {
            return Err(IdentityError::TooLong);
        }
        let bytes = text.as_bytes();
        for &byte in bytes {
            let permitted =
                byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'.' || byte == b'-';
            if !permitted {
                return Err(IdentityError::ForbiddenByte);
            }
        }
        if is_separator(first(bytes)?) || is_separator(last(bytes)?) {
            return Err(IdentityError::MalformedSeparator);
        }
        Ok(Self(text.to_owned()))
    }

    /// The revision as it was written.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for AssetRevision {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

fn is_separator(byte: u8) -> bool {
    byte == b'.' || byte == b'-'
}

fn first(bytes: &[u8]) -> Result<u8, IdentityError> {
    bytes.first().copied().ok_or(IdentityError::Empty)
}

fn last(bytes: &[u8]) -> Result<u8, IdentityError> {
    bytes.last().copied().ok_or(IdentityError::Empty)
}

fn check_alphabet(
    text: &str,
    maximum: usize,
    separator: u8,
    permitted: impl Fn(u8) -> bool,
) -> Result<(), IdentityError> {
    if text.is_empty() {
        return Err(IdentityError::Empty);
    }
    if text.len() > maximum {
        return Err(IdentityError::TooLong);
    }
    let bytes = text.as_bytes();
    let mut previous_was_separator = false;
    for &byte in bytes {
        if byte == separator {
            if previous_was_separator {
                return Err(IdentityError::MalformedSeparator);
            }
            previous_was_separator = true;
            continue;
        }
        if !permitted(byte) {
            return Err(IdentityError::ForbiddenByte);
        }
        previous_was_separator = false;
    }
    if first(bytes)? == separator || last(bytes)? == separator {
        return Err(IdentityError::MalformedSeparator);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{AssetId, AssetRevision, IdentityError};

    #[test]
    fn an_ordinary_identity_parses() {
        let id = AssetId::parse("python-stdlib").unwrap_or_else(|_| unreachable!());
        assert_eq!(id.as_str(), "python-stdlib");
    }

    #[test]
    fn traversal_and_separators_are_refused() {
        for text in [
            "..", ".", "a/b", "a..b", "-a", "a-", "a--b", "A", "a_b", "a b",
        ] {
            assert!(AssetId::parse(text).is_err(), "accepted {text:?}");
        }
    }

    #[test]
    fn an_empty_identity_is_empty_not_forbidden() {
        assert_eq!(AssetId::parse(""), Err(IdentityError::Empty));
    }

    #[test]
    fn an_identity_at_and_over_the_maximum() {
        let at = "a".repeat(super::MAX_ASSET_ID_BYTES);
        let over = "a".repeat(super::MAX_ASSET_ID_BYTES + 1);
        assert!(AssetId::parse(&at).is_ok());
        assert_eq!(AssetId::parse(&over), Err(IdentityError::TooLong));
    }

    #[test]
    fn a_version_revision_parses_and_a_capital_does_not() {
        assert!(AssetRevision::parse("3.14.2-taffy.1").is_ok());
        // The character three checkers used to accept and a phone never
        // would; see the note on AssetRevision.
        assert!(AssetRevision::parse("3.14.2+taffy.1").is_err());
        assert!(AssetRevision::parse("3.14.2-rc1").is_ok());
        assert_eq!(
            AssetRevision::parse("3.14.2+Taffy"),
            Err(IdentityError::ForbiddenByte)
        );
        assert_eq!(
            AssetRevision::parse(".3.14"),
            Err(IdentityError::MalformedSeparator)
        );
    }
}
