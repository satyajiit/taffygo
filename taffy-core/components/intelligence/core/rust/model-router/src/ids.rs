// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed identifiers.
//!
//! Two families live here and they are deliberately different shapes.
//!
//! **Catalog keys** ([`ProviderId`], [`ModelId`]) are human-authored strings
//! that arrive from untrusted catalog data. They are validated on construction
//! and on deserialization, so a malformed key never reaches a lookup, a log
//! line, or a request builder.
//!
//! **Domain identifiers** ([`TaskId`], [`RequestId`], [`ModelInvocationId`])
//! are opaque 128-bit values per the domain model's identifier rules. They are
//! never parsed from a catalog and never derived from a URL, a title, or a row
//! id. This crate mints none of them; it receives them from the caller that
//! owns the aggregate.

use core::fmt;

/// Longest catalog key this crate accepts, in bytes.
///
/// Catalog input is remote input to a trusted process, so every string that
/// becomes a map key is length-bounded before it is stored.
pub const MAX_KEY_LEN: usize = 128;

/// Why a catalog key was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IdError {
    /// The key was empty.
    Empty,
    /// The key was longer than [`MAX_KEY_LEN`].
    TooLong {
        /// Length of the offending key in bytes.
        len: usize,
    },
    /// The key contained a byte outside the accepted set.
    InvalidByte {
        /// The offending byte.
        byte: u8,
    },
}

impl fmt::Display for IdError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Empty => f.write_str("catalog key is empty"),
            Self::TooLong { len } => write!(f, "catalog key is {len} bytes, over the limit"),
            Self::InvalidByte { byte } => write!(f, "catalog key contains byte 0x{byte:02x}"),
        }
    }
}

/// Accepted key bytes: ASCII alphanumerics and the four separators providers
/// actually use in model names.
fn accepted(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b':' | b'/' | b'@')
}

fn validate_key(raw: &str) -> Result<(), IdError> {
    if raw.is_empty() {
        return Err(IdError::Empty);
    }
    if raw.len() > MAX_KEY_LEN {
        return Err(IdError::TooLong { len: raw.len() });
    }
    for byte in raw.as_bytes() {
        if !accepted(*byte) {
            return Err(IdError::InvalidByte { byte: *byte });
        }
    }
    Ok(())
}

macro_rules! catalog_key {
    ($name:ident, $what:literal) => {
        #[doc = concat!("A validated ", $what, ".")]
        ///
        /// Construction and deserialization both run the same validation, so an
        /// invalid key from a catalog document fails the entry rather than
        /// entering a lookup table.
        #[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
        #[derive(serde::Serialize, serde::Deserialize)]
        #[serde(try_from = "String", into = "String")]
        pub struct $name(String);

        impl $name {
            /// Validates and wraps a key.
            pub fn new(raw: &str) -> Result<Self, IdError> {
                validate_key(raw)?;
                Ok(Self(raw.to_owned()))
            }

            /// The key as a string slice.
            pub fn as_str(&self) -> &str {
                &self.0
            }
        }

        impl TryFrom<String> for $name {
            type Error = IdError;

            fn try_from(raw: String) -> Result<Self, IdError> {
                validate_key(&raw)?;
                Ok(Self(raw))
            }
        }

        impl From<$name> for String {
            fn from(value: $name) -> Self {
                value.0
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(&self.0)
            }
        }
    };
}

catalog_key!(ProviderId, "provider identifier");
catalog_key!(ModelId, "provider-scoped model identifier");

/// The pair that names one catalog model.
///
/// A model identifier is only meaningful under its provider, so nothing in
/// this crate accepts a bare [`ModelId`].
#[derive(
    Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
pub struct ModelKey {
    /// The owning provider.
    pub provider_id: ProviderId,
    /// The model under that provider.
    pub model_id: ModelId,
}

impl ModelKey {
    /// Builds a key from validated parts.
    pub fn new(provider_id: ProviderId, model_id: ModelId) -> Self {
        Self {
            provider_id,
            model_id,
        }
    }
}

impl fmt::Display for ModelKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}/{}", self.provider_id, self.model_id)
    }
}

/// Why an opaque identifier could not be parsed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OpaqueIdError;

impl fmt::Display for OpaqueIdError {
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

fn parse_128(raw: &str) -> Result<[u8; 16], OpaqueIdError> {
    let bytes = raw.as_bytes();
    if bytes.len() != 32 {
        return Err(OpaqueIdError);
    }
    let mut out = [0u8; 16];
    for (index, slot) in out.iter_mut().enumerate() {
        let high = bytes.get(index * 2).copied().ok_or(OpaqueIdError)?;
        let low = bytes.get(index * 2 + 1).copied().ok_or(OpaqueIdError)?;
        let high = hex_digit(high).ok_or(OpaqueIdError)?;
        let low = hex_digit(low).ok_or(OpaqueIdError)?;
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

macro_rules! opaque_id {
    ($name:ident, $what:literal) => {
        #[doc = concat!("An opaque 128-bit ", $what, ".")]
        ///
        /// The domain model forbids deriving identity from a database key, a
        /// URL, a title, or an array index, so this type carries bytes and
        /// nothing else. It has no ordering: sorting identifiers would leak
        /// creation order across a trust boundary.
        #[derive(Clone, Copy, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize)]
        #[serde(try_from = "String", into = "String")]
        pub struct $name([u8; 16]);

        impl $name {
            /// Wraps 16 caller-supplied bytes.
            pub fn from_bytes(bytes: [u8; 16]) -> Self {
                Self(bytes)
            }

            /// Parses 32 lowercase hexadecimal digits.
            pub fn parse(raw: &str) -> Result<Self, OpaqueIdError> {
                parse_128(raw).map(Self)
            }

            /// The raw bytes.
            pub fn as_bytes(&self) -> &[u8; 16] {
                &self.0
            }
        }

        impl TryFrom<String> for $name {
            type Error = OpaqueIdError;

            fn try_from(raw: String) -> Result<Self, OpaqueIdError> {
                Self::parse(&raw)
            }
        }

        impl From<$name> for String {
            fn from(value: $name) -> Self {
                render_128(&value.0)
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

opaque_id!(TaskId, "task identifier");
opaque_id!(RequestId, "model request identifier");
opaque_id!(ModelInvocationId, "model invocation identifier");
opaque_id!(SourceId, "source identifier");
