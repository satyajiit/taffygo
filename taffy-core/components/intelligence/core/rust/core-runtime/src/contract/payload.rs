// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded byte payloads shared by service requests and completions.

/// Absolute upper bound for one effect or completion payload.
pub const MAX_PAYLOAD_BYTES: usize = 1_048_576;

/// Why a requested payload bound is unusable.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PayloadError {
    /// A zero-byte payload allowance cannot carry a protocol value.
    ZeroLimit,
    /// The requested allowance exceeds the service-wide ceiling.
    LimitTooLarge,
    /// The value exceeds its declared allowance.
    TooLarge,
}

impl PayloadError {
    /// A compiled-in diagnostic label.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ZeroLimit => "zero_limit",
            Self::LimitTooLarge => "limit_too_large",
            Self::TooLarge => "payload_too_large",
        }
    }
}

/// A caller-selected bound no larger than the service ceiling.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PayloadLimit(usize);

impl PayloadLimit {
    /// Validates a payload allowance.
    pub const fn new(bytes: usize) -> Result<Self, PayloadError> {
        if bytes == 0 {
            return Err(PayloadError::ZeroLimit);
        }
        if bytes > MAX_PAYLOAD_BYTES {
            return Err(PayloadError::LimitTooLarge);
        }
        Ok(Self(bytes))
    }

    /// The maximum encoded bytes.
    pub const fn bytes(self) -> usize {
        self.0
    }
}

/// Bytes proven to fit both a call-site bound and the service-wide ceiling.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct BoundedPayload(Vec<u8>);

impl BoundedPayload {
    /// Copies a payload after checking its encoded size.
    pub fn new(bytes: impl Into<Vec<u8>>, limit: PayloadLimit) -> Result<Self, PayloadError> {
        let bytes = bytes.into();
        if bytes.len() > limit.bytes() || bytes.len() > MAX_PAYLOAD_BYTES {
            return Err(PayloadError::TooLarge);
        }
        Ok(Self(bytes))
    }

    /// An empty payload, used by terminal completions with no result body.
    pub const fn empty() -> Self {
        Self(Vec::new())
    }

    /// The checked bytes.
    pub fn as_bytes(&self) -> &[u8] {
        &self.0
    }

    /// The encoded length.
    pub fn len(&self) -> usize {
        self.0.len()
    }

    /// Whether the payload is empty.
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::{BoundedPayload, PayloadError, PayloadLimit, MAX_PAYLOAD_BYTES};

    #[test]
    fn payload_limit_refuses_zero_and_values_above_the_service_ceiling() {
        assert_eq!(PayloadLimit::new(0), Err(PayloadError::ZeroLimit));
        assert_eq!(
            PayloadLimit::new(MAX_PAYLOAD_BYTES.saturating_add(1)),
            Err(PayloadError::LimitTooLarge)
        );
    }

    #[test]
    fn payload_is_checked_against_the_narrower_call_site_limit() {
        let limit = PayloadLimit::new(3).unwrap_or_else(|_| unreachable!());
        assert_eq!(
            BoundedPayload::new(vec![0_u8; 4], limit),
            Err(PayloadError::TooLarge)
        );
        assert_eq!(
            BoundedPayload::new(vec![0_u8; 3], limit)
                .map(|payload| payload.len())
                .unwrap_or_default(),
            3
        );
    }
}
