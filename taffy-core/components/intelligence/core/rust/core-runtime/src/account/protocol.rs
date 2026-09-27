// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Profile-scoped authorization-flow and serialized-session state.

use std::collections::{BTreeMap, BTreeSet};

mod native;
mod session;

use crate::contract::Deadline;

use super::crypto::{derive_authorization_material, Sha256Port};
use super::{
    AccountAuthMethod, AccountEffect, AccountEmail, AccountEndpointId, AccountError,
    AccountFailure, AccountFailureCode, AccountProvider, AccountScope, AccountSession, AuthFlowId,
    AuthorizationEntropy, AuthorizationIntent, AuthorizationRequestPlan, EmailLinkPlan,
    GoogleNonceHash, PkceChallenge, RedirectBindingId, RedirectOutcome, RedirectReceipt,
    RedirectState, SecretHandle, TokenExchangePlan, AUTHORIZATION_ENTROPY_BYTES,
};

/// Hard ceiling retained at the contract boundary. The portable protocol
/// deliberately admits only one session-changing authorization flow at once.
pub use core_service_types::MAX_PENDING_ACCOUNT_FLOWS as MAX_PENDING_AUTH_FLOWS;

/// Supabase authorization codes are single-use and valid for five minutes.
pub const MAX_AUTHORIZATION_CODE_LIFETIME_MILLIS: u64 = 300_000;

/// Begin a serialized refresh this long before the recorded expiry.
pub const REFRESH_AHEAD_MILLIS: u64 = 60_000;

#[derive(Clone, Debug)]
struct AuthorizationMaterial {
    flow_id: AuthFlowId,
    auth_method: super::AccountAuthMethod,
    email: Option<AccountEmail>,
    redirect_binding: RedirectBindingId,
    scopes: Vec<AccountScope>,
    pkce_challenge: PkceChallenge,
    state: RedirectState,
    issued_at_millis: u64,
    deadline: Deadline,
}

#[derive(Clone, Debug)]
struct NativeNonceMaterial {
    auth_method: AccountAuthMethod,
    deadline: Deadline,
    hashed_nonce: GoogleNonceHash,
}

#[derive(Clone, Debug)]
struct PendingNativeCredential {
    auth_method: AccountAuthMethod,
    deadline: Deadline,
    raw_nonce_handle: SecretHandle,
}

/// Pure, profile-scoped account protocol state.
#[derive(Clone, Debug, Default)]
pub struct AccountProtocol {
    awaiting_entropy: BTreeMap<AuthFlowId, AuthorizationIntent>,
    awaiting_verifier_handle: BTreeMap<AuthFlowId, AuthorizationMaterial>,
    pending_redirect: BTreeMap<AuthFlowId, AuthorizationRequestPlan>,
    awaiting_native_nonce_entropy: BTreeMap<AuthFlowId, (AccountAuthMethod, Deadline)>,
    awaiting_native_nonce_handle: BTreeMap<AuthFlowId, NativeNonceMaterial>,
    pending_native_credential: BTreeMap<AuthFlowId, PendingNativeCredential>,
    exchange_in_flight: BTreeMap<AuthFlowId, AccountAuthMethod>,
    session: Option<AccountSession>,
    refresh_in_flight: Option<u64>,
    reconciliation_required: bool,
    /// The four methods this build and this account plane can actually serve.
    ///
    /// Browser-minted at bootstrap. Empty means no method is offered, which is
    /// what a private profile and an unconfigured account plane both produce —
    /// and is projected as every method being unconfigured rather than as an
    /// empty screen.
    available_methods: Vec<AccountAuthMethod>,
    /// The last terminal failure, held until the next attempt begins.
    last_failure: Option<AccountFailure>,
    /// The address a sign-in link was accepted for, held until it is used.
    ///
    /// Mutually exclusive with `last_failure` by construction: recording one
    /// clears the other, because a link that was sent did not fail and a flow
    /// that failed did not send a link.
    link_sent: Option<AccountEmail>,
}

impl AccountProtocol {
    /// An empty protocol state.
    pub const fn new() -> Self {
        Self {
            awaiting_entropy: BTreeMap::new(),
            awaiting_verifier_handle: BTreeMap::new(),
            pending_redirect: BTreeMap::new(),
            awaiting_native_nonce_entropy: BTreeMap::new(),
            awaiting_native_nonce_handle: BTreeMap::new(),
            pending_native_credential: BTreeMap::new(),
            exchange_in_flight: BTreeMap::new(),
            session: None,
            refresh_in_flight: None,
            reconciliation_required: false,
            available_methods: Vec::new(),
            last_failure: None,
            link_sent: None,
        }
    }

    /// Restores only the handle-based durable session at profile bootstrap.
    ///
    /// Pending authorization and refresh work is generation-bound and is
    /// deliberately not restored. The browser single writer supplies this
    /// record from committed storage before any account command is accepted.
    pub const fn restore_session(session: Option<AccountSession>) -> Self {
        Self {
            awaiting_entropy: BTreeMap::new(),
            awaiting_verifier_handle: BTreeMap::new(),
            pending_redirect: BTreeMap::new(),
            awaiting_native_nonce_entropy: BTreeMap::new(),
            awaiting_native_nonce_handle: BTreeMap::new(),
            pending_native_credential: BTreeMap::new(),
            exchange_in_flight: BTreeMap::new(),
            session,
            refresh_in_flight: None,
            reconciliation_required: false,
            available_methods: Vec::new(),
            // A failure and a sent link are both generation-bound, exactly as
            // pending flows are: neither survives a restart, because neither
            // describes anything the browser committed.
            last_failure: None,
            link_sent: None,
        }
    }

    /// Starts a flow from UI intent without accepting state, PKCE, or a URI.
    pub fn begin_authorization(
        &mut self,
        intent: AuthorizationIntent,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        match (intent.auth_method, intent.email.is_some()) {
            (AccountAuthMethod::Google, _) => return Err(AccountError::InvalidAuthMethod),
            (AccountAuthMethod::EmailLink, false)
            | (AccountAuthMethod::Github | AccountAuthMethod::Facebook, true) => {
                return Err(AccountError::InvalidEmail);
            }
            (AccountAuthMethod::EmailLink, true)
            | (AccountAuthMethod::Github | AccountAuthMethod::Facebook, false) => {}
        }
        if intent.deadline.is_expired_at(now_millis) {
            return Err(AccountError::DeadlineExceeded);
        }
        let latest_deadline = intent
            .issued_at_millis
            .checked_add(MAX_AUTHORIZATION_CODE_LIFETIME_MILLIS)
            .ok_or(AccountError::CodeLifetimeTooLong)?;
        if intent.issued_at_millis > now_millis || intent.deadline.as_millis() > latest_deadline {
            return Err(AccountError::CodeLifetimeTooLong);
        }
        validate_scopes(&intent.scopes)?;
        if self.contains_flow(&intent.flow_id) {
            return Err(AccountError::DuplicateFlow);
        }
        self.ensure_sign_in_available()?;
        if self.pending_count() >= MAX_PENDING_AUTH_FLOWS {
            return Err(AccountError::TooManyPendingFlows);
        }
        let flow_id = intent.flow_id.clone();
        self.awaiting_entropy.insert(flow_id.clone(), intent);
        // An attempt that is starting is neither failed nor waiting for a link.
        self.clear_terminal_markers();
        Ok(AccountEffect::RequestSecureEntropy {
            flow_id,
            bytes: AUTHORIZATION_ENTROPY_BYTES,
        })
    }

    /// Derives state and the S256 challenge, then asks secure storage for a handle.
    pub fn accept_authorization_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &AuthorizationEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        let Some(intent) = self.awaiting_entropy.get(flow_id) else {
            return Err(AccountError::UnknownFlow);
        };
        let (state, verifier, pkce_challenge) =
            derive_authorization_material(entropy, digest_port)?;
        let material = AuthorizationMaterial {
            flow_id: intent.flow_id.clone(),
            auth_method: intent.auth_method,
            email: intent.email.clone(),
            redirect_binding: intent.redirect_binding.clone(),
            scopes: intent.scopes.clone(),
            pkce_challenge,
            state,
            issued_at_millis: intent.issued_at_millis,
            deadline: intent.deadline,
        };
        self.awaiting_entropy.remove(flow_id);
        self.awaiting_verifier_handle
            .insert(flow_id.clone(), material);
        Ok(AccountEffect::StorePkceVerifier {
            flow_id: flow_id.clone(),
            verifier,
        })
    }

    /// Binds the browser secure-store handle before opening authorization.
    pub fn accept_pkce_verifier_handle(
        &mut self,
        flow_id: &AuthFlowId,
        handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        let Some(material) = self.awaiting_verifier_handle.remove(flow_id) else {
            return Err(AccountError::UnknownFlow);
        };
        let plan = AuthorizationRequestPlan {
            flow_id: material.flow_id,
            auth_method: material.auth_method,
            email: material.email,
            provider: AccountProvider::Supabase,
            endpoint: AccountEndpointId::SupabaseAuthorize,
            redirect_binding: material.redirect_binding,
            scopes: material.scopes,
            pkce_challenge: material.pkce_challenge,
            pkce_verifier_handle: handle,
            state: material.state,
            issued_at_millis: material.issued_at_millis,
            deadline: material.deadline,
        };
        self.pending_redirect.insert(flow_id.clone(), plan.clone());
        match plan.auth_method {
            AccountAuthMethod::Github | AccountAuthMethod::Facebook => {
                Ok(AccountEffect::OpenAuthorization(plan))
            }
            AccountAuthMethod::EmailLink => {
                let Some(email) = plan.email.clone() else {
                    self.pending_redirect.remove(flow_id);
                    return Err(AccountError::InvalidEmail);
                };
                Ok(AccountEffect::RequestEmailLink(EmailLinkPlan {
                    flow_id: plan.flow_id,
                    email,
                    redirect_binding: plan.redirect_binding,
                    pkce_challenge: plan.pkce_challenge,
                    pkce_verifier_handle: plan.pkce_verifier_handle,
                    state: plan.state,
                    issued_at_millis: plan.issued_at_millis,
                    deadline: plan.deadline,
                }))
            }
            AccountAuthMethod::Google => {
                self.pending_redirect.remove(flow_id);
                Err(AccountError::InvalidAuthMethod)
            }
        }
    }

    /// Validates a redirect and consumes its flow exactly once.
    pub fn accept_redirect(
        &mut self,
        receipt: &RedirectReceipt,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        let Some(plan) = self.pending_redirect.get(&receipt.flow_id) else {
            return Err(AccountError::UnknownFlow);
        };
        // Copied before any mutation: every arm below both removes the flow and
        // records why, and a failure that could not name its method would leave
        // the screen saying "that did not work" about four buttons at once.
        let method = plan.auth_method;
        if plan.deadline.is_expired_at(now_millis) {
            self.fail_flow(&receipt.flow_id, AccountFailureCode::Network, method);
            return Err(AccountError::DeadlineExceeded);
        }
        // A binding or state mismatch does not consume the flow — the real
        // redirect may still arrive — but it is still something a person's
        // screen has to be able to say, because a mismatched callback is the
        // shape a redirect-hijack attempt has.
        if plan.redirect_binding != receipt.redirect_binding {
            self.record_failure(AccountFailure::for_method(
                AccountFailureCode::InvalidRedirect,
                method,
            ));
            return Err(AccountError::RedirectBindingMismatch);
        }
        if !plan.state.matches(&receipt.state) {
            self.record_failure(AccountFailure::for_method(
                AccountFailureCode::InvalidRedirect,
                method,
            ));
            return Err(AccountError::StateMismatch);
        }
        let code = match &receipt.outcome {
            RedirectOutcome::AuthorizationCode(handle) => handle.clone(),
            RedirectOutcome::Denied => {
                self.fail_flow(&receipt.flow_id, AccountFailureCode::Cancelled, method);
                return Err(AccountError::AccessDenied);
            }
            RedirectOutcome::ProviderError => {
                self.fail_flow(&receipt.flow_id, AccountFailureCode::Rejected, method);
                return Err(AccountError::ProviderError);
            }
            RedirectOutcome::DeadlineExceeded => {
                self.fail_flow(&receipt.flow_id, AccountFailureCode::Network, method);
                return Err(AccountError::DeadlineExceeded);
            }
            RedirectOutcome::PlatformUnavailable => {
                self.fail_flow(&receipt.flow_id, AccountFailureCode::NotConfigured, method);
                return Err(AccountError::SurfaceUnavailable);
            }
        };
        let Some(plan) = self.pending_redirect.remove(&receipt.flow_id) else {
            return Err(AccountError::UnknownFlow);
        };
        self.exchange_in_flight
            .insert(plan.flow_id.clone(), plan.auth_method);
        Ok(AccountEffect::ExchangeCode(TokenExchangePlan {
            flow_id: plan.flow_id,
            auth_method: plan.auth_method,
            provider: plan.provider,
            endpoint: AccountEndpointId::SupabaseToken,
            authorization_code_handle: code,
            pkce_verifier_handle: plan.pkce_verifier_handle,
            redirect_binding: plan.redirect_binding,
            issued_at_millis: plan.issued_at_millis,
            deadline: plan.deadline,
        }))
    }

    /// Records whether the visible OAuth surface opened without treating that as authorization.
    pub fn accept_authorization_surface(
        &mut self,
        flow_id: &AuthFlowId,
        opened: bool,
    ) -> Result<(), AccountError> {
        if !self.pending_redirect.contains_key(flow_id) {
            return Err(AccountError::UnknownFlow);
        }
        if opened {
            return Ok(());
        }
        self.pending_redirect.remove(flow_id);
        Err(AccountError::SurfaceUnavailable)
    }

    /// Records whether the fixed account route accepted a bound email-link request.
    pub fn accept_email_link_delivery(
        &mut self,
        flow_id: &AuthFlowId,
        accepted: bool,
    ) -> Result<(), AccountError> {
        let Some(plan) = self.pending_redirect.get(flow_id) else {
            return Err(AccountError::UnknownFlow);
        };
        if plan.auth_method != AccountAuthMethod::EmailLink {
            return Err(AccountError::AuthMethodMismatch);
        }
        if accepted {
            // The plan retains the address only while its redirect is pending,
            // which is exactly as long as "we sent you a link" is true.
            if let Some(email) = plan.email.clone() {
                self.record_link_sent(email);
            }
            return Ok(());
        }
        self.pending_redirect.remove(flow_id);
        self.record_failure(AccountFailure::for_method(
            AccountFailureCode::Rejected,
            AccountAuthMethod::EmailLink,
        ));
        Err(AccountError::ProviderError)
    }

    /// Drops one pending redirect and records why it ended.
    ///
    /// The pair is a helper rather than two statements at each of five sites
    /// because the two halves are one fact: the flow is over, and this is what
    /// happened to it.
    fn fail_flow(
        &mut self,
        flow_id: &AuthFlowId,
        code: AccountFailureCode,
        method: AccountAuthMethod,
    ) {
        self.pending_redirect.remove(flow_id);
        self.record_failure(AccountFailure::for_method(code, method));
    }
}

fn validate_scopes(scopes: &[AccountScope]) -> Result<(), AccountError> {
    if scopes.is_empty() {
        return Err(AccountError::NoScopes);
    }
    let mut seen = BTreeSet::new();
    for scope in scopes {
        if !seen.insert(*scope) {
            return Err(AccountError::DuplicateScope);
        }
    }
    Ok(())
}
