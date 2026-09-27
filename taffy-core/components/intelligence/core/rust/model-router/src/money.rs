// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Integer money and token-rate arithmetic.
//!
//! Everything is integer micro-units of a currency: a hundredth of a cent is
//! still 10,000 of them, which is finer than any provider prices at. Floating
//! point is avoided on purpose — the model router is deterministic by construction,
//! and a budget refusal that depends on rounding mode is not a refusal anyone
//! can reproduce.
//!
//! Every operation is checked. A price snapshot arrives from catalog data, so
//! an absurd rate must produce a refusal, never a wrapped total.

use core::fmt;

/// Tokens are priced per million, so this is the divisor of every rate.
pub const TOKENS_PER_RATE_UNIT: u64 = 1_000_000;

/// A non-negative amount in integer micro-units of a [`Currency`].
#[derive(
    Clone,
    Copy,
    Debug,
    Default,
    PartialEq,
    Eq,
    PartialOrd,
    Ord,
    Hash,
    serde::Serialize,
    serde::Deserialize,
)]
#[serde(transparent)]
pub struct Micros(u64);

impl Micros {
    /// Zero.
    pub const ZERO: Self = Self(0);

    /// Wraps a raw micro-unit count.
    pub fn new(value: u64) -> Self {
        Self(value)
    }

    /// The raw micro-unit count.
    pub fn get(self) -> u64 {
        self.0
    }

    /// Adds two amounts, refusing to wrap.
    pub fn checked_add(self, other: Self) -> Option<Self> {
        self.0.checked_add(other.0).map(Self)
    }

    /// Subtracts, saturating at zero.
    #[must_use]
    pub fn saturating_sub(self, other: Self) -> Self {
        Self(self.0.saturating_sub(other.0))
    }

    /// Prices `tokens` at `rate` micro-units per million tokens.
    ///
    /// Rounds up: a fraction of a micro-unit is charged, never dropped, so a
    /// budget can never be overspent by accumulated truncation.
    pub fn for_tokens(rate: u64, tokens: u64) -> Option<Self> {
        let product = u128::from(rate).checked_mul(u128::from(tokens))?;
        let divisor = u128::from(TOKENS_PER_RATE_UNIT);
        let rounded = product.checked_add(divisor - 1)? / divisor;
        u64::try_from(rounded).ok().map(Self)
    }
}

impl fmt::Display for Micros {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// Why a currency code was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CurrencyError;

impl fmt::Display for CurrencyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("expected three uppercase ASCII letters")
    }
}

/// A three-letter currency code.
///
/// Amounts in different currencies are never combined. Budget arithmetic
/// refuses a mismatch instead of assuming a conversion rate the model router has no
/// business inventing.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(try_from = "String", into = "String")]
pub struct Currency([u8; 3]);

impl Currency {
    /// Validates and wraps a code.
    pub fn new(raw: &str) -> Result<Self, CurrencyError> {
        let bytes = raw.as_bytes();
        if bytes.len() != 3 {
            return Err(CurrencyError);
        }
        let mut out = [0u8; 3];
        for (index, slot) in out.iter_mut().enumerate() {
            let byte = bytes.get(index).copied().ok_or(CurrencyError)?;
            if !byte.is_ascii_uppercase() {
                return Err(CurrencyError);
            }
            *slot = byte;
        }
        Ok(Self(out))
    }

    /// The code as a string slice.
    pub fn as_str(&self) -> &str {
        core::str::from_utf8(&self.0).unwrap_or("???")
    }
}

impl TryFrom<String> for Currency {
    type Error = CurrencyError;

    fn try_from(raw: String) -> Result<Self, CurrencyError> {
        Self::new(&raw)
    }
}

impl From<Currency> for String {
    fn from(value: Currency) -> Self {
        value.as_str().to_owned()
    }
}

impl fmt::Display for Currency {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}
