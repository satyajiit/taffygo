// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A generated artifact and the checksum over its bytes.
//!
//! The checksum detects accidental corruption and makes a byte-identical test
//! cheap to write. It is deliberately not a cryptographic digest and is never
//! evidence of authenticity: the durable `content_digest` of domain model
//! section 16 is computed by the storage layer with the browser's own
//! cryptography.

use super::kind::ArtifactKind;
use crate::ids::ArtifactId;
/// A generated artifact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Artifact {
    /// Which artifact this is.
    pub artifact_id: ArtifactId,
    /// Which format.
    pub kind: ArtifactKind,
    /// The bytes, as text.
    pub content: String,
    /// A checksum over the bytes.
    pub checksum: ContentChecksum,
    /// How many facts it rests on.
    pub fact_count: u64,
    /// How many sources those facts cite.
    pub source_count: u64,
}

impl Artifact {
    /// How large the artifact is.
    pub fn byte_len(&self) -> u64 {
        u64::try_from(self.content.len()).unwrap_or(u64::MAX)
    }
}

/// A checksum over an artifact's bytes.
///
/// This detects accidental corruption and makes a byte-identical test cheap to
/// write. It is not a cryptographic digest and is never evidence of
/// authenticity: the durable `content_digest` of domain model section 16 is
/// computed by the storage layer with the browser's own cryptography.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ContentChecksum(pub u64);

impl ContentChecksum {
    const OFFSET: u64 = 0xcbf2_9ce4_8422_2325;
    const PRIME: u64 = 0x0000_0100_0000_01b3;

    /// The checksum of `bytes`.
    pub fn of(bytes: &[u8]) -> Self {
        let mut hash = Self::OFFSET;
        for byte in bytes {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(Self::PRIME);
        }
        Self(hash)
    }

    /// The checksum as fixed-width lowercase hexadecimal.
    pub fn to_hex(self) -> String {
        format!("{:016x}", self.0)
    }
}

#[cfg(test)]
mod tests {
    use super::ContentChecksum;

    #[test]
    fn a_checksum_is_stable_and_distinguishes_a_one_byte_change() {
        assert_eq!(ContentChecksum::of(b"abc"), ContentChecksum::of(b"abc"));
        assert_ne!(ContentChecksum::of(b"abc"), ContentChecksum::of(b"abd"));
        assert_eq!(ContentChecksum::of(b"abc").to_hex().len(), 16);
    }
}
