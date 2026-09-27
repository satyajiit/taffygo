// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Why an account flow ended without a session.
//!
//! A flow that fails is not the same as a flow that never started, and the
//! difference is the whole of what a person needs from screen SCR-701. Until
//! this module existed the protocol discarded every terminal outcome: a
//! cancelled Google sheet, a refused provider and an unreachable network all
//! left the same state behind — no pending flow and no session — which the
//! status projection could only render as signed out. Six failure sentences
//! were written and translated, and none of them could be reached.
//!
//! The vocabulary is deliberately the Core API's own `AuthFailureCode` rather
//! than a richer internal one. `AccountError` has forty variants because the
//! protocol has forty ways to refuse an input; a person has eight things they
//! can do about it, and a mapping that produced a ninth would be a mapping
//! nothing could render.

use core_api_types::AuthFailureCode;

use super::AccountAuthMethod;

/// The closed reason an account flow ended, in the vocabulary the UI renders.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AccountFailureCode {
    /// The method is not configured in this build or on the account plane.
    NotConfigured,
    /// The person dismissed the credential sheet or the browser surface.
    Cancelled,
    /// No eligible credential existed to offer.
    NoCredential,
    /// The request did not reach the account plane, or its answer did not arrive.
    Network,
    /// The account plane or the provider refused the request it received.
    Rejected,
    /// A callback arrived that did not match the flow that was started.
    InvalidRedirect,
    /// The core could not accept account work at all.
    CoreUnavailable,
    /// Nothing more specific is known, which is itself worth saying.
    Unknown,
}

impl AccountFailureCode {
    /// Every reason, so a test can prove each one reaches the screen.
    ///
    /// Rust cannot enumerate its own variants, and a projection that dropped
    /// one would fail silently — the sentence for it would simply never be
    /// shown, which is the state this module was written to end.
    pub const ALL: [Self; 8] = [
        Self::NotConfigured,
        Self::Cancelled,
        Self::NoCredential,
        Self::Network,
        Self::Rejected,
        Self::InvalidRedirect,
        Self::CoreUnavailable,
        Self::Unknown,
    ];

    /// Whether trying the same method again could plausibly succeed.
    ///
    /// This is not a retry policy — nothing here retries anything. It is what
    /// the screen uses to decide between offering the same button again and
    /// telling the person the method will not work.
    pub const fn retryable(self) -> bool {
        match self {
            Self::Cancelled | Self::Network | Self::CoreUnavailable | Self::Unknown => true,
            Self::NotConfigured | Self::NoCredential | Self::Rejected | Self::InvalidRedirect => {
                false
            }
        }
    }

    /// The generated contract value for this reason.
    pub const fn to_wire(self) -> AuthFailureCode {
        match self {
            Self::NotConfigured => AuthFailureCode::NotConfigured,
            Self::Cancelled => AuthFailureCode::Cancelled,
            Self::NoCredential => AuthFailureCode::NoCredential,
            Self::Network => AuthFailureCode::Network,
            Self::Rejected => AuthFailureCode::Rejected,
            Self::InvalidRedirect => AuthFailureCode::InvalidRedirect,
            Self::CoreUnavailable => AuthFailureCode::CoreUnavailable,
            Self::Unknown => AuthFailureCode::Unknown,
        }
    }
}

/// One terminal account failure, kept until the next attempt begins.
///
/// The method is retained because a screen that says "that did not work"
/// without naming which of four buttons it means is not saying anything.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AccountFailure {
    /// What went wrong.
    pub code: AccountFailureCode,
    /// Which of the four methods it went wrong for, when a flow was involved.
    ///
    /// Absent for a failure that belongs to no single method — a refresh of an
    /// existing session, or the core becoming unavailable. Read by the status
    /// projection, which demotes a method that refused to start at all back to
    /// `NOT_CONFIGURED` rather than leaving the screen offering it again.
    pub method: Option<AccountAuthMethod>,
}

impl AccountFailure {
    /// A failure that belongs to one method's flow.
    pub const fn for_method(code: AccountFailureCode, method: AccountAuthMethod) -> Self {
        Self {
            code,
            method: Some(method),
        }
    }

    /// A failure that belongs to the profile rather than to one flow.
    pub const fn for_profile(code: AccountFailureCode) -> Self {
        Self { code, method: None }
    }

    /// Whether trying again could plausibly succeed.
    pub const fn retryable(&self) -> bool {
        self.code.retryable()
    }
}
