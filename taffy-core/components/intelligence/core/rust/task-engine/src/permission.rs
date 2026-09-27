// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed native-permission facts owned by the durable task reducer.

use core::fmt;

use crate::task::BrowserSessionId;

/// Maximum opaque permission-request identity accepted by the reducer.
pub const MAX_PERMISSION_REQUEST_ID_BYTES: usize = 256;

/// One browser-minted permission request identity.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct PermissionRequestId(String);

impl PermissionRequestId {
    /// Accepts one non-empty bounded opaque identity.
    pub fn new(value: impl Into<String>) -> Result<Self, PermissionFactError> {
        let value = value.into();
        if value.is_empty() || value.len() > MAX_PERMISSION_REQUEST_ID_BYTES {
            return Err(PermissionFactError::InvalidRequestId);
        }
        Ok(Self(value))
    }

    /// The opaque identity, for exact correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for PermissionRequestId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Native permissions the portable reducer can request.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum PlatformPermission {
    Notifications,
    Microphone,
    Camera,
    Location,
}

impl PlatformPermission {
    /// Every closed permission, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Notifications,
        Self::Microphone,
        Self::Camera,
        Self::Location,
    ];
}

/// Terminal platform decisions for one visible permission request.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum PermissionDecision {
    Granted,
    Denied,
    Dismissed,
    Unavailable,
}

impl PermissionDecision {
    /// Every closed terminal decision, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Granted,
        Self::Denied,
        Self::Dismissed,
        Self::Unavailable,
    ];
}

/// Exact facts committed before a native permission surface is opened.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PermissionRequest {
    request_id: PermissionRequestId,
    permission: PlatformPermission,
    deadline_monotonic_ms: u64,
    deadline_utc_ms: u64,
    browser_session_id: BrowserSessionId,
}

impl PermissionRequest {
    pub fn new(
        request_id: PermissionRequestId,
        permission: PlatformPermission,
        deadline_monotonic_ms: u64,
        deadline_utc_ms: u64,
        browser_session_id: BrowserSessionId,
    ) -> Result<Self, PermissionFactError> {
        if deadline_monotonic_ms == 0 || deadline_utc_ms == 0 {
            return Err(PermissionFactError::InvalidDeadline);
        }
        Ok(Self {
            request_id,
            permission,
            deadline_monotonic_ms,
            deadline_utc_ms,
            browser_session_id,
        })
    }

    pub const fn request_id(&self) -> &PermissionRequestId {
        &self.request_id
    }

    pub const fn permission(&self) -> PlatformPermission {
        self.permission
    }

    pub const fn deadline_monotonic_ms(&self) -> u64 {
        self.deadline_monotonic_ms
    }

    pub const fn deadline_utc_ms(&self) -> u64 {
        self.deadline_utc_ms
    }

    pub const fn browser_session_id(&self) -> &BrowserSessionId {
        &self.browser_session_id
    }
}

/// Exact terminal facts returned for a native permission request.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PermissionResult {
    request_id: PermissionRequestId,
    permission: PlatformPermission,
    decision: PermissionDecision,
}

impl PermissionResult {
    pub const fn new(
        request_id: PermissionRequestId,
        permission: PlatformPermission,
        decision: PermissionDecision,
    ) -> Self {
        Self {
            request_id,
            permission,
            decision,
        }
    }

    pub const fn request_id(&self) -> &PermissionRequestId {
        &self.request_id
    }

    pub const fn permission(&self) -> PlatformPermission {
        self.permission
    }

    pub const fn decision(&self) -> PermissionDecision {
        self.decision
    }
}

/// A permission fact was malformed at the reducer boundary.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PermissionFactError {
    InvalidRequestId,
    InvalidDeadline,
}

#[cfg(test)]
mod tests {
    use super::{PermissionFactError, PermissionRequestId, MAX_PERMISSION_REQUEST_ID_BYTES};

    #[test]
    fn request_identity_is_non_empty_and_bounded() {
        assert_eq!(
            PermissionRequestId::new(""),
            Err(PermissionFactError::InvalidRequestId)
        );
        assert!(PermissionRequestId::new("p".repeat(MAX_PERMISSION_REQUEST_ID_BYTES)).is_ok());
        assert_eq!(
            PermissionRequestId::new("p".repeat(MAX_PERMISSION_REQUEST_ID_BYTES + 1)),
            Err(PermissionFactError::InvalidRequestId)
        );
    }
}
