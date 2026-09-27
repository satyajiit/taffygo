// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed record identifiers.
//!
//! Domain identifiers are opaque 128-bit values. A database primary key, a URL,
//! a title, or a row position is not identity, so nothing here is derived from
//! one, and this crate mints none of them: identifiers arrive from the
//! aggregate that owns them.
//!
//! Each type is distinct. A source identifier cannot be passed where a
//! workspace identifier belongs, which matters most in deletion, where the two
//! cascade very differently.

use core::fmt;

/// Why an identifier did not parse.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct IdError;

impl fmt::Display for IdError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("expected 32 lowercase hexadecimal digits")
    }
}

fn hex_digit(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        _ => None,
    }
}

fn parse_128(raw: &str) -> Result<[u8; 16], IdError> {
    let bytes = raw.as_bytes();
    if bytes.len() != 32 {
        return Err(IdError);
    }
    let mut out = [0u8; 16];
    for (index, slot) in out.iter_mut().enumerate() {
        let high = hex_digit(bytes.get(index * 2).copied().ok_or(IdError)?).ok_or(IdError)?;
        let low = hex_digit(bytes.get(index * 2 + 1).copied().ok_or(IdError)?).ok_or(IdError)?;
        *slot = (high << 4) | low;
    }
    Ok(out)
}

fn render_128(value: &[u8; 16]) -> String {
    let mut out = String::with_capacity(32);
    for byte in value {
        out.push(char::from_digit(u32::from(byte >> 4), 16).unwrap_or('0'));
        out.push(char::from_digit(u32::from(byte & 0x0f), 16).unwrap_or('0'));
    }
    out
}

macro_rules! record_id {
    ($name:ident, $what:literal) => {
        #[doc = concat!("An opaque ", $what, ".")]
        #[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name([u8; 16]);

        impl $name {
            /// Wraps 16 caller-supplied bytes.
            pub fn from_bytes(bytes: [u8; 16]) -> Self {
                Self(bytes)
            }

            /// Parses the stored representation.
            pub fn parse(raw: &str) -> Result<Self, IdError> {
                parse_128(raw).map(Self)
            }

            /// The stored representation.
            pub fn to_text(self) -> String {
                render_128(&self.0)
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(&render_128(&self.0))
            }
        }

        impl fmt::Debug for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, concat!(stringify!($name), "({})"), render_128(&self.0))
            }
        }
    };
}

record_id!(WorkspaceId, "workspace identifier");
record_id!(SourceId, "source identifier");
record_id!(ObservationId, "observation identifier");
record_id!(FactId, "fact identifier");
record_id!(ClaimId, "claim identifier");
record_id!(ConflictId, "conflict identifier");
record_id!(TaskId, "task identifier");
record_id!(ArtifactId, "artifact identifier");
record_id!(EventId, "journal event identifier");
record_id!(ProvenanceId, "provenance locator identifier");
record_id!(ModelInvocationId, "model invocation identifier");
record_id!(DocumentId, "search document identifier");
record_id!(ReceiptId, "deletion receipt identifier");
record_id!(LibraryEntryId, "Library entry identifier");
record_id!(MemoryId, "Memory record identifier");
