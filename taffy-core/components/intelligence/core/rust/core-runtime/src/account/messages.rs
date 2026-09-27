// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Handle-only account plans, effects, and session receipts.

use crate::contract::Deadline;

use super::{
    AccountAuthMethod, AccountDisplayName, AccountEmail, AccountEndpointId, AccountProvider,
    AccountScope, AccountSubjectId, AuthFlowId, AuthorizationCodeHandle, GoogleNonceHash,
    GoogleRawNonceMaterial, PkceChallenge, PkceVerifierMaterial, RedirectBindingId, RedirectState,
    SecretHandle, SessionHandle,
};

/// UI-level authorization intent before Rust derives state and PKCE values.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AuthorizationIntent {
    /// Correlates the browser redirect to this request.
    pub flow_id: AuthFlowId,
    /// Person-visible account method selected through the closed Core API enum.
    pub auth_method: AccountAuthMethod,
    /// Present only for `EMAIL_LINK`; other methods never carry an address.
    pub email: Option<AccountEmail>,
    /// Opaque registered callback selected by product configuration.
    pub redirect_binding: RedirectBindingId,
    /// Requested claims from the closed account vocabulary.
    pub scopes: Vec<AccountScope>,
    /// Browser-owned monotonic time at which the flow was created.
    pub issued_at_millis: u64,
    /// Browser-owned monotonic deadline, capped at five minutes.
    pub deadline: Deadline,
}

/// A transport-neutral authorization request.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AuthorizationRequestPlan {
    /// Correlates the browser redirect to this request.
    pub flow_id: AuthFlowId,
    /// Closed account method represented by this authorization surface.
    pub auth_method: AccountAuthMethod,
    /// Email-link destination retained only while its bound redirect is pending.
    pub email: Option<AccountEmail>,
    /// Selects pinned endpoint configuration.
    pub provider: AccountProvider,
    /// Selects a pinned endpoint without accepting a URL.
    pub endpoint: AccountEndpointId,
    /// Opaque callback registration bound to the whole flow.
    pub redirect_binding: RedirectBindingId,
    /// Requested claims.
    pub scopes: Vec<AccountScope>,
    /// Public S256 challenge.
    pub pkce_challenge: PkceChallenge,
    /// Browser-owned reference to the verifier.
    pub pkce_verifier_handle: SecretHandle,
    /// CSRF correlation value.
    pub state: RedirectState,
    /// Browser-owned monotonic time at which the flow was created.
    pub issued_at_millis: u64,
    /// The browser-owned monotonic deadline.
    pub deadline: Deadline,
}

/// What the platform redirect receiver observed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RedirectOutcome {
    /// The browser stored the code and returned only its opaque handle.
    AuthorizationCode(AuthorizationCodeHandle),
    /// The person denied or dismissed the request.
    Denied,
    /// The pinned provider returned a protocol error with no copied text.
    ProviderError,
    /// The browser-owned authorization timer expired the bound flow.
    DeadlineExceeded,
    /// The profile platform adapter disconnected before the flow completed.
    PlatformUnavailable,
}

/// A redirect receipt from the registered platform callback adapter.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RedirectReceipt {
    /// Which pending flow the platform believes this belongs to.
    pub flow_id: AuthFlowId,
    /// Registered callback binding that received the platform redirect.
    pub redirect_binding: RedirectBindingId,
    /// The returned CSRF state.
    pub state: RedirectState,
    /// A code handle or a closed refusal.
    pub outcome: RedirectOutcome,
}

/// A transport-neutral exchange request for the pinned token endpoint.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TokenExchangePlan {
    /// The completed flow.
    pub flow_id: AuthFlowId,
    /// Closed account method whose credential is being exchanged.
    pub auth_method: AccountAuthMethod,
    /// Selects pinned endpoint configuration.
    pub provider: AccountProvider,
    /// Selects the pinned token endpoint without accepting a URL.
    pub endpoint: AccountEndpointId,
    /// Browser-owned authorization-code reference.
    pub authorization_code_handle: AuthorizationCodeHandle,
    /// Browser-owned PKCE verifier reference.
    pub pkce_verifier_handle: SecretHandle,
    /// Must equal the registered callback used for authorization.
    pub redirect_binding: RedirectBindingId,
    /// Original browser-owned issue time used to enforce code lifetime.
    pub issued_at_millis: u64,
    /// The original deadline remains binding.
    pub deadline: Deadline,
}

/// A fixed-route email-link request with PKCE state already derived.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EmailLinkPlan {
    pub flow_id: AuthFlowId,
    pub email: AccountEmail,
    pub redirect_binding: RedirectBindingId,
    pub pkce_challenge: PkceChallenge,
    pub pkce_verifier_handle: SecretHandle,
    pub state: RedirectState,
    pub issued_at_millis: u64,
    pub deadline: Deadline,
}

/// A browser-owned native credential handle to exchange at the pinned route.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NativeCredentialExchangePlan {
    pub flow_id: AuthFlowId,
    pub auth_method: AccountAuthMethod,
    pub credential_handle: SecretHandle,
    pub raw_nonce_handle: SecretHandle,
    pub deadline: Deadline,
}

/// Terminal result from a native credential surface; successful material is handle-only.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum NativeCredentialOutcome {
    Success(SecretHandle),
    Cancelled,
    NoCredential,
    Unavailable,
}

/// A token-exchange result containing handles and claims, never tokens.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SessionReceipt {
    /// Browser-owned reference to the account session.
    pub session_handle: SessionHandle,
    /// Closed account method established by the exchange.
    pub auth_method: AccountAuthMethod,
    /// Stable provider subject, bounded as an opaque identifier.
    pub account_subject: AccountSubjectId,
    /// Browser-owned monotonic expiry for refresh planning.
    pub expires_at: Deadline,
    /// Rotation accepted by the secure-store adapter.
    pub rotation: u64,
    /// The address the account plane confirmed, when it confirmed one.
    pub email: Option<AccountEmail>,
    /// The name the provider supplied, when it supplied one.
    pub display_name: Option<AccountDisplayName>,
}

/// The portable account state retained by the core.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AccountSession {
    /// Browser-owned secure-store reference.
    pub session_handle: SessionHandle,
    /// Closed account method retained across service restart for truthful UI state.
    pub auth_method: AccountAuthMethod,
    /// Opaque provider subject.
    pub account_subject: AccountSubjectId,
    /// Refresh should be planned before this boundary.
    pub expires_at: Deadline,
    /// Last committed refresh-token rotation, without carrying the token.
    pub rotation: u64,
    /// The address this account signed in with, when the plane confirmed one.
    ///
    /// Held here rather than fetched when a screen asks, because a screen asks
    /// during the first frame after a restart and there is no answer available
    /// then that does not involve the network.
    pub email: Option<AccountEmail>,
    /// The name this account carries, when the provider supplied one.
    pub display_name: Option<AccountDisplayName>,
}

/// One serialized refresh attempt against a browser-owned rotating token.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RefreshPlan {
    /// Secure-store session reference.
    pub session_handle: SessionHandle,
    /// The next rotation the adapter must commit atomically.
    pub expected_rotation: u64,
    /// Stable account subject that the rotated response must preserve.
    pub expected_account_subject: AccountSubjectId,
    /// Closed account method that the rotated response must preserve.
    pub expected_auth_method: AccountAuthMethod,
    /// Selects the pinned token endpoint without accepting a URL.
    pub endpoint: AccountEndpointId,
}

/// Account work executed by the browser-owned transport and secure store.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AccountEffect {
    /// Ask the browser OS adapter for exact-size secure random bytes.
    RequestSecureEntropy {
        /// Flow that will consume the one reply.
        flow_id: AuthFlowId,
        /// Exact byte count requested by the protocol.
        bytes: usize,
    },
    /// Ask for exact-size entropy for one Google raw nonce.
    RequestGoogleNonceEntropy { flow_id: AuthFlowId, bytes: usize },
    /// Store the Rust-derived verifier and return only an opaque handle.
    StorePkceVerifier {
        /// Flow that will consume the returned handle.
        flow_id: AuthFlowId,
        /// Transient verifier material; custom debug output is redacted.
        verifier: PkceVerifierMaterial,
    },
    /// Store the raw Google nonce and return only an opaque handle.
    StoreGoogleRawNonce {
        flow_id: AuthFlowId,
        raw_nonce: GoogleRawNonceMaterial,
        hashed_nonce: GoogleNonceHash,
    },
    /// Open the pinned authorization endpoint in a trusted browser surface.
    OpenAuthorization(AuthorizationRequestPlan),
    /// Request the platform-native credential surface for one closed method.
    RequestNativeCredential {
        flow_id: AuthFlowId,
        auth_method: AccountAuthMethod,
        raw_nonce_handle: SecretHandle,
        hashed_nonce: GoogleNonceHash,
    },
    /// Ask the fixed account route to send one bound email link.
    RequestEmailLink(EmailLinkPlan),
    /// Exchange code and verifier handles at the pinned token endpoint.
    ExchangeCode(TokenExchangePlan),
    /// Exchange a native credential handle without exposing its material.
    ExchangeNativeCredential(NativeCredentialExchangePlan),
    /// Spend or remove the raw nonce after a native surface ends without a credential.
    DeleteNativeNonce {
        flow_id: AuthFlowId,
        raw_nonce_handle: SecretHandle,
    },
    /// Refresh the browser-owned session; no refresh token crosses this type.
    Refresh(RefreshPlan),
    /// Delete the browser-owned session and revoke it when supported.
    SignOut {
        /// Browser-owned session reference.
        session_handle: SessionHandle,
        /// Selects the pinned revocation endpoint without accepting a URL.
        endpoint: AccountEndpointId,
    },
}

/// Closed interpretation of a pinned account token endpoint status.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TokenHttpDisposition {
    /// Any HTTP status in the complete 2xx range.
    Success,
    /// The grant, code, verifier, or session was refused.
    CredentialRejected,
    /// A bounded retry may be planned after the current attempt ends.
    Retryable,
    /// The pinned endpoint returned an unexpected contract status.
    ProtocolFailure,
}

/// Classifies a token endpoint response without assuming a particular 2xx code.
pub const fn classify_token_status(status: u16) -> TokenHttpDisposition {
    match status {
        200..=299 => TokenHttpDisposition::Success,
        400 | 401 | 403 => TokenHttpDisposition::CredentialRejected,
        408 | 425 | 429 | 500..=599 => TokenHttpDisposition::Retryable,
        _ => TokenHttpDisposition::ProtocolFailure,
    }
}
