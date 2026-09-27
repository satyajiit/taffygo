// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Provider-route identity frozen by a task without importing a router.

use core::fmt;

/// Maximum accepted route identifier length in bytes.
pub const MAX_PROVIDER_ROUTE_ID_LEN: usize = 128;

/// Why a provider route identifier was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProviderRouteIdError {
    /// The identifier was empty.
    Empty,
    /// The identifier exceeded the boundary limit.
    TooLong,
    /// The identifier contained a byte outside the closed grammar.
    InvalidByte,
}

/// A validated, provider-neutral route identifier.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ProviderRouteId(String);

impl ProviderRouteId {
    /// Validates an identifier from fixed product configuration.
    pub fn new(raw: &str) -> Result<Self, ProviderRouteIdError> {
        if raw.is_empty() {
            return Err(ProviderRouteIdError::Empty);
        }
        if raw.len() > MAX_PROVIDER_ROUTE_ID_LEN {
            return Err(ProviderRouteIdError::TooLong);
        }
        if raw.as_bytes().iter().any(|byte| {
            !(byte.is_ascii_alphanumeric()
                || matches!(*byte, b'-' | b'_' | b'.' | b':' | b'/' | b'@'))
        }) {
            return Err(ProviderRouteIdError::InvalidByte);
        }
        Ok(Self(raw.to_owned()))
    }

    /// The validated identifier.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for ProviderRouteId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}
