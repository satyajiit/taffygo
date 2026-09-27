// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Why the provider plane refused one request.
//!
//! Every variant is a refusal the core reached on its own facts. Nothing here
//! describes a network outcome, because this plane performs no I/O.

use core::fmt;

/// A refusal from the portable provider-credential protocol.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProviderError {
    /// The provider identity was empty.
    EmptyProviderId,
    /// The provider identity was longer than the bound both sides accept.
    ProviderIdTooLong,
    /// The provider identity used a byte the platform store cannot hold.
    InvalidProviderId,
    /// The shown name was blank.
    EmptyDisplayName,
    /// The shown name was longer than the bound.
    DisplayNameTooLong,
    /// The opaque secure-store handle was empty.
    EmptyCredentialHandle,
    /// The opaque secure-store handle was longer than the bound.
    CredentialHandleTooLong,
    /// The endpoint was not an `https` URL this build will speak to.
    InvalidEndpoint,
    /// The person already defines as many providers as this profile allows.
    TooManyCustomProviders,
    /// The save carried more models than one provider may arrive with.
    TooManyCustomModels,
    /// No provider is filed under that identity.
    UnknownProvider,
    /// The catalog already defines this identity, so a person's own provider
    /// may not take it.
    ProviderIdReserved,
    /// The provider does not offer the method that was asked for.
    MethodNotOffered,
    /// The catalog ships this provider switched off, so no flow may start.
    ProviderDisabled,
    /// A sign-in is already running for this provider.
    FlowAlreadyRunning,
    /// No sign-in is running under that flow identity.
    UnknownFlow,
    /// The redirect did not carry the state the flow was started with.
    StateMismatch,
    /// The redirect arrived after the flow's deadline.
    FlowExpired,
    /// More flows are pending than this profile allows at once.
    TooManyPendingFlows,
    /// A redirect reported success and carried no authorization code handle,
    /// or reported failure and carried one.
    MalformedRedirect,
}

impl fmt::Display for ProviderError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::EmptyProviderId => "the provider identity is empty",
            Self::ProviderIdTooLong => "the provider identity is too long",
            Self::InvalidProviderId => "the provider identity uses a byte the store cannot hold",
            Self::EmptyDisplayName => "the provider name is blank",
            Self::DisplayNameTooLong => "the provider name is too long",
            Self::EmptyCredentialHandle => "the credential handle is empty",
            Self::CredentialHandleTooLong => "the credential handle is too long",
            Self::InvalidEndpoint => "the endpoint is not one this build will speak to",
            Self::TooManyCustomProviders => {
                "this profile already defines as many providers as it may"
            }
            Self::TooManyCustomModels => "the endpoint listed more models than one may carry",
            Self::UnknownProvider => "no provider is filed under that identity",
            Self::ProviderIdReserved => "the catalog already defines that identity",
            Self::MethodNotOffered => "the provider does not offer that method",
            Self::ProviderDisabled => "the catalog ships that provider switched off",
            Self::FlowAlreadyRunning => "a sign-in is already running for that provider",
            Self::UnknownFlow => "no sign-in is running under that flow identity",
            Self::StateMismatch => "the redirect did not carry the state the flow started with",
            Self::FlowExpired => "the redirect arrived after the flow's deadline",
            Self::TooManyPendingFlows => "more sign-ins are pending than this profile allows",
            Self::MalformedRedirect => "the redirect's outcome and its code handle disagree",
        };
        formatter.write_str(text)
    }
}
