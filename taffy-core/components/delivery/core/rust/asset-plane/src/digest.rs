// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The content digest and the verdict comparing one against another.
//!
//! A digest is the only thing that makes a delivery origin untrusted rather
//! than trusted. Every byte the plane accepts is compared against a value the
//! product was built with, so an origin that serves different bytes serves a
//! refusal instead. The comparison is constant-time because the alternative
//! leaks how much of a forged prefix was right, and there is no reason to pay
//! that when the whole comparison is thirty-two bytes.

use core::fmt;

/// Bytes in a digest.
pub const DIGEST_BYTES: usize = 32;

/// Characters in the hexadecimal a catalog source writes.
pub const DIGEST_HEX_CHARS: usize = DIGEST_BYTES * 2;

/// A SHA-256 digest of an asset's transfer bytes.
///
/// This crate never computes one — hashing is the browser's, on the bytes it
/// wrote. What lives here is the value to compare against and the comparison.
#[derive(Clone, Copy, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Digest([u8; DIGEST_BYTES]);

impl Digest {
    /// Wraps bytes that are already a digest.
    pub const fn from_bytes(bytes: [u8; DIGEST_BYTES]) -> Self {
        Self(bytes)
    }

    /// Parses lowercase hexadecimal, or `None` when it is not a digest.
    ///
    /// Uppercase is refused rather than folded: a catalog writes one spelling,
    /// and accepting two means two sources can differ and still agree.
    pub fn parse_hex(text: &str) -> Option<Self> {
        if text.len() != DIGEST_HEX_CHARS {
            return None;
        }
        let mut bytes = [0u8; DIGEST_BYTES];
        let source = text.as_bytes();
        for (index, slot) in bytes.iter_mut().enumerate() {
            let high = source.get(index * 2).copied()?;
            let low = source.get(index * 2 + 1).copied()?;
            *slot = (nibble(high)? << 4) | nibble(low)?;
        }
        Some(Self(bytes))
    }

    /// The digest as bytes.
    pub const fn as_bytes(&self) -> &[u8; DIGEST_BYTES] {
        &self.0
    }

    /// Whether `other` is the same digest, compared in constant time.
    pub fn matches(&self, other: &Self) -> bool {
        let mut difference = 0u8;
        for index in 0..DIGEST_BYTES {
            let left = self.0.get(index).copied().unwrap_or(0);
            let right = other.0.get(index).copied().unwrap_or(0);
            difference |= left ^ right;
        }
        difference == 0
    }
}

impl fmt::Debug for Digest {
    /// Prints the first four bytes only.
    ///
    /// A digest in a log is an identifier, not evidence: the whole value would
    /// invite a reader to compare two by eye, which is the one thing
    /// [`Digest::matches`] exists to stop them doing.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("Digest(")?;
        for index in 0..4 {
            let byte = self.0.get(index).copied().unwrap_or(0);
            write!(formatter, "{byte:02x}")?;
        }
        formatter.write_str("…)")
    }
}

fn nibble(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        _ => None,
    }
}

/// What the browser found when it finished writing an asset's bytes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IntegrityVerdict {
    /// Length and digest are both what the catalog names.
    Sound,
    /// The transfer ended with a different number of bytes than the catalog names.
    WrongLength {
        /// What the catalog names.
        expected: u64,
        /// What was written.
        actual: u64,
    },
    /// The right number of bytes hashed to something else.
    WrongDigest,
}

impl IntegrityVerdict {
    /// Whether the bytes may be installed.
    pub fn is_sound(self) -> bool {
        matches!(self, Self::Sound)
    }
}

/// Judges written bytes against what the catalog names.
///
/// Length is checked first and separately. Both answers are a refusal, but a
/// short file and a wrong file are different accidents — one is a truncated
/// transfer and one is not the artifact — and the plane's retry rule differs
/// between them.
pub fn judge(
    expected_bytes: u64,
    expected_digest: &Digest,
    actual_bytes: u64,
    actual_digest: &Digest,
) -> IntegrityVerdict {
    if actual_bytes != expected_bytes {
        return IntegrityVerdict::WrongLength {
            expected: expected_bytes,
            actual: actual_bytes,
        };
    }
    if !expected_digest.matches(actual_digest) {
        return IntegrityVerdict::WrongDigest;
    }
    IntegrityVerdict::Sound
}

#[cfg(test)]
mod tests {
    use super::{judge, Digest, IntegrityVerdict, DIGEST_BYTES};

    fn digest_of(fill: u8) -> Digest {
        Digest::from_bytes([fill; DIGEST_BYTES])
    }

    #[test]
    fn hexadecimal_round_trips() {
        let text = "00112233445566778899aabbccddeeff\
                    00112233445566778899aabbccddeeff";
        let parsed = Digest::parse_hex(text).unwrap_or_else(|| unreachable!());
        assert_eq!(parsed.as_bytes().first().copied(), Some(0x00));
        assert_eq!(parsed.as_bytes().get(1).copied(), Some(0x11));
    }

    #[test]
    fn uppercase_short_and_non_hexadecimal_are_refused() {
        assert!(Digest::parse_hex(&"AB".repeat(32)).is_none());
        assert!(Digest::parse_hex(&"ab".repeat(31)).is_none());
        assert!(Digest::parse_hex(&"zz".repeat(32)).is_none());
    }

    #[test]
    fn a_short_file_is_a_length_verdict_not_a_digest_one() {
        let verdict = judge(100, &digest_of(1), 40, &digest_of(2));
        assert_eq!(
            verdict,
            IntegrityVerdict::WrongLength {
                expected: 100,
                actual: 40
            }
        );
    }

    #[test]
    fn the_right_length_and_the_wrong_bytes_is_a_digest_verdict() {
        assert_eq!(
            judge(100, &digest_of(1), 100, &digest_of(2)),
            IntegrityVerdict::WrongDigest
        );
    }

    #[test]
    fn agreement_is_sound() {
        assert!(judge(100, &digest_of(7), 100, &digest_of(7)).is_sound());
    }

    #[test]
    fn debug_shows_a_prefix_and_not_the_digest() {
        let rendered = format!("{:?}", digest_of(0xab));
        assert_eq!(rendered, "Digest(abababab…)");
    }
}
