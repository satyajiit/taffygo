// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Portable account and PKCE protocol with no HTTP or token material.
//!
//! Platform code opens the trusted browser surface, receives its registered
//! callback, and owns secure-store handles. Rust derives state and S256 PKCE,
//! validates flow/session transitions, and emits fixed-endpoint plans.

pub(crate) mod crypto;
mod error;
mod failure;
mod identity;
mod messages;
mod protocol;
mod token_response;

pub use self::crypto::{DigestError, Sha256Port};
pub use self::error::AccountError;
pub use self::failure::{AccountFailure, AccountFailureCode};
pub use self::identity::{
    AccountAuthMethod, AccountDisplayName, AccountEmail, AccountEndpointId, AccountProvider,
    AccountScope, AccountSubjectId, AuthFlowId, AuthorizationCodeHandle, AuthorizationEntropy,
    GoogleNonceEntropy, GoogleNonceHash, GoogleRawNonceMaterial, PkceChallenge,
    PkceVerifierMaterial, RedirectBindingId, RedirectState, SecretHandle, SessionHandle,
    AUTHORIZATION_ENTROPY_BYTES, GOOGLE_NONCE_ENTROPY_BYTES,
};
pub use self::messages::{
    classify_token_status, AccountEffect, AccountSession, AuthorizationIntent,
    AuthorizationRequestPlan, EmailLinkPlan, NativeCredentialExchangePlan, NativeCredentialOutcome,
    RedirectOutcome, RedirectReceipt, RefreshPlan, SessionReceipt, TokenExchangePlan,
    TokenHttpDisposition,
};
pub use self::protocol::{
    AccountProtocol, MAX_AUTHORIZATION_CODE_LIFETIME_MILLIS, MAX_PENDING_AUTH_FLOWS,
    REFRESH_AHEAD_MILLIS,
};
pub use self::token_response::validate_account_token_response;

#[cfg(test)]
mod tests;
