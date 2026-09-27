// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed account protocol failures.

/// Why portable account protocol validation refused an input.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AccountError {
    /// Flow identity was empty.
    EmptyFlowId,
    /// Flow identity crossed its cap.
    FlowIdTooLong,
    /// Account subject identity was empty.
    EmptyAccountSubject,
    /// Account subject identity crossed its cap.
    AccountSubjectTooLong,
    /// An email-link destination failed the bounded canonical mailbox shape.
    InvalidEmail,
    /// A provider-supplied display name was empty, oversized, or held control
    /// characters.
    InvalidDisplayName,
    /// The selected account method is not valid for this flow family.
    InvalidAuthMethod,
    /// A terminal result named a different method than the pending flow.
    AuthMethodMismatch,
    /// A refresh receipt named a different durable account identity.
    AccountSubjectMismatch,
    /// Product callback registration identity was empty.
    EmptyRedirectBinding,
    /// Product callback registration identity crossed its cap.
    RedirectBindingTooLong,
    /// A callback URI or unsupported character was supplied instead of an id.
    InvalidRedirectBinding,
    /// State did not meet its entropy-length floor.
    StateTooShort,
    /// State crossed its cap.
    StateTooLong,
    /// State was not unpadded base64url.
    InvalidStateEncoding,
    /// A fixed-size encoding or digest invariant unexpectedly failed.
    CryptoInvariant,
    /// The secure-random adapter returned a trivially invalid value.
    InvalidEntropy,
    /// A secure-store reference was empty.
    EmptySecretHandle,
    /// A secure-store reference crossed its cap.
    SecretHandleTooLong,
    /// An authorization-code reference was empty.
    EmptyAuthorizationCodeHandle,
    /// An authorization-code reference crossed its cap.
    AuthorizationCodeHandleTooLong,
    /// A session reference was empty.
    EmptySessionHandle,
    /// A session reference crossed its cap.
    SessionHandleTooLong,
    /// At least one closed account scope is required.
    NoScopes,
    /// Repeating a scope makes the request non-canonical.
    DuplicateScope,
    /// The profile already has a flow with this identity.
    DuplicateFlow,
    /// Another flow is already allowed to change the canonical profile session.
    SessionMutationInFlight,
    /// A provider or vault mutation may have happened and must be reconciled by the browser.
    ReconciliationRequired,
    /// A canonical profile session already exists and must not be replaced by sign-in.
    SessionAlreadyExists,
    /// The bounded pending-flow table is full.
    TooManyPendingFlows,
    /// No pending flow matches the receipt.
    UnknownFlow,
    /// Returned CSRF state did not match.
    StateMismatch,
    /// Redirect arrived through a different registered callback binding.
    RedirectBindingMismatch,
    /// The authorization request expired.
    DeadlineExceeded,
    /// The user denied or dismissed authorization.
    AccessDenied,
    /// The provider returned a closed protocol failure.
    ProviderError,
    /// The visible account surface could not be opened.
    SurfaceUnavailable,
    /// The native credential surface was dismissed.
    CredentialCancelled,
    /// No eligible native credential was available.
    NoCredential,
    /// The caller attempted to make an authorization code live too long.
    CodeLifetimeTooLong,
    /// A refresh is already using the rotating token for this profile.
    RefreshAlreadyInFlight,
    /// A refresh receipt did not carry the expected rotation.
    RefreshRotationMismatch,
    /// Rotation overflowed rather than wrapping to an old token generation.
    RefreshRotationExhausted,
}

impl AccountError {
    /// A compiled-in diagnostic label with no provider text.
    pub const fn label(self) -> &'static str {
        match self {
            Self::EmptyFlowId => "empty_flow_id",
            Self::FlowIdTooLong => "flow_id_too_long",
            Self::EmptyAccountSubject => "empty_account_subject",
            Self::AccountSubjectTooLong => "account_subject_too_long",
            Self::InvalidEmail => "invalid_email",
            Self::InvalidDisplayName => "invalid_display_name",
            Self::InvalidAuthMethod => "invalid_auth_method",
            Self::AuthMethodMismatch => "auth_method_mismatch",
            Self::AccountSubjectMismatch => "account_subject_mismatch",
            Self::EmptyRedirectBinding => "empty_redirect_binding",
            Self::RedirectBindingTooLong => "redirect_binding_too_long",
            Self::InvalidRedirectBinding => "invalid_redirect_binding",
            Self::StateTooShort => "state_too_short",
            Self::StateTooLong => "state_too_long",
            Self::InvalidStateEncoding => "invalid_state_encoding",
            Self::CryptoInvariant => "crypto_invariant",
            Self::InvalidEntropy => "invalid_entropy",
            Self::EmptySecretHandle => "empty_secret_handle",
            Self::SecretHandleTooLong => "secret_handle_too_long",
            Self::EmptyAuthorizationCodeHandle => "empty_code_handle",
            Self::AuthorizationCodeHandleTooLong => "code_handle_too_long",
            Self::EmptySessionHandle => "empty_session_handle",
            Self::SessionHandleTooLong => "session_handle_too_long",
            Self::NoScopes => "no_scopes",
            Self::DuplicateScope => "duplicate_scope",
            Self::DuplicateFlow => "duplicate_flow",
            Self::SessionMutationInFlight => "session_mutation_in_flight",
            Self::ReconciliationRequired => "reconciliation_required",
            Self::SessionAlreadyExists => "session_already_exists",
            Self::TooManyPendingFlows => "too_many_pending_flows",
            Self::UnknownFlow => "unknown_flow",
            Self::StateMismatch => "state_mismatch",
            Self::RedirectBindingMismatch => "redirect_binding_mismatch",
            Self::DeadlineExceeded => "deadline_exceeded",
            Self::AccessDenied => "access_denied",
            Self::ProviderError => "provider_error",
            Self::SurfaceUnavailable => "surface_unavailable",
            Self::CredentialCancelled => "credential_cancelled",
            Self::NoCredential => "no_credential",
            Self::CodeLifetimeTooLong => "code_lifetime_too_long",
            Self::RefreshAlreadyInFlight => "refresh_already_in_flight",
            Self::RefreshRotationMismatch => "refresh_rotation_mismatch",
            Self::RefreshRotationExhausted => "refresh_rotation_exhausted",
        }
    }
}
