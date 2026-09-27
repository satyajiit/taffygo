// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The portable account-protocol composition port.
//!
//! Account state is handle-only: no token, verifier, nonce, or credential
//! value crosses this seam, and no implementation may perform I/O. Every stage
//! returns a typed effect the browser performs later, so the port is the same
//! ordered, synchronous shape as [`PolicyPort`](super::PolicyPort).

use crate::account::{
    AccountAuthMethod, AccountEffect, AccountEmail, AccountError, AccountFailure, AccountSession,
    AuthFlowId, AuthorizationEntropy, AuthorizationIntent, GoogleNonceEntropy,
    NativeCredentialOutcome, RedirectReceipt, SecretHandle, SessionReceipt, Sha256Port,
};
use crate::contract::Deadline;

/// Profile-scoped authorization-flow and serialized-session state.
///
/// Implementations must not block, perform I/O, enter Mojo, read a clock, or
/// retain secret material. `now_millis` and every entropy value are supplied
/// by the caller.
pub trait AccountPort {
    /// The current handle-only account state.
    fn session(&self) -> Option<&AccountSession>;

    /// The methods this build and this account plane can actually serve.
    ///
    /// Browser-minted at bootstrap and never inferred here. A method absent
    /// from this list is one the surface must draw as unconfigured rather than
    /// omit: a button that vanishes tells a person nothing, and screen SCR-701
    /// has a state for each of the four.
    fn available_methods(&self) -> &[AccountAuthMethod];

    /// Why the last flow ended without a session, if one did.
    ///
    /// Cleared when a fresh attempt begins and when a session is established,
    /// so this never describes a moment that has passed.
    fn last_failure(&self) -> Option<&AccountFailure>;

    /// The address a sign-in link was accepted for, while it is still current.
    fn pending_email(&self) -> Option<&AccountEmail>;

    /// Number of bounded flows across entropy, secure-store, and redirect stages.
    fn pending_count(&self) -> usize;

    /// Whether the browser must reconcile its durable SQL and vault halves
    /// before a later utility generation may accept account work.
    fn reconciliation_required(&self) -> bool;

    /// Installs only the handle-based durable session at profile bootstrap.
    fn restore_session(&mut self, session: Option<AccountSession>);

    /// Starts a flow from UI intent without accepting state, PKCE, or a URI.
    fn begin_authorization(
        &mut self,
        intent: AuthorizationIntent,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError>;

    /// Starts one native credential flow by requesting nonce entropy first.
    fn begin_native_authorization(
        &mut self,
        flow_id: AuthFlowId,
        auth_method: AccountAuthMethod,
        deadline: Deadline,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError>;

    /// Derives state and the S256 challenge, then asks secure storage for a handle.
    fn accept_authorization_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &AuthorizationEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError>;

    /// Binds the browser secure-store handle before opening authorization.
    fn accept_pkce_verifier_handle(
        &mut self,
        flow_id: &AuthFlowId,
        handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError>;

    /// Derives a raw/hashed nonce pair and sends only the raw value to storage.
    fn accept_google_nonce_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &GoogleNonceEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError>;

    /// Binds the raw nonce handle before the native surface opens.
    fn accept_google_raw_nonce_handle(
        &mut self,
        flow_id: &AuthFlowId,
        raw_nonce_handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError>;

    /// Validates a redirect and consumes its flow exactly once.
    fn accept_redirect(
        &mut self,
        receipt: &RedirectReceipt,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError>;

    /// Records whether the visible OAuth surface opened, which is not authorization.
    fn accept_authorization_surface(
        &mut self,
        flow_id: &AuthFlowId,
        opened: bool,
    ) -> Result<(), AccountError>;

    /// Records whether the fixed account route accepted a bound email-link request.
    fn accept_email_link_delivery(
        &mut self,
        flow_id: &AuthFlowId,
        accepted: bool,
    ) -> Result<(), AccountError>;

    /// Consumes one terminal native credential result exactly once.
    fn accept_native_credential(
        &mut self,
        flow_id: &AuthFlowId,
        auth_method: AccountAuthMethod,
        outcome: NativeCredentialOutcome,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError>;

    /// Records a browser-owned session handle after a successful exchange.
    fn accept_exchange(
        &mut self,
        flow_id: &AuthFlowId,
        receipt: SessionReceipt,
    ) -> Result<AccountSession, AccountError>;

    /// Plans a refresh before expiry without exposing its rotating token.
    fn begin_refresh(&mut self, now_millis: u64) -> Result<Option<AccountEffect>, AccountError>;

    /// Commits one rotating-token refresh and unlocks the next attempt.
    fn accept_refresh(&mut self, receipt: SessionReceipt) -> Result<AccountSession, AccountError>;

    /// Settles a refresh that did not produce a committed receipt.
    fn settle_refresh_failure(&mut self, outcome_unknown: bool) -> bool;

    /// Clears portable state and returns the secure-store handle to revoke.
    fn begin_sign_out(&mut self) -> Result<Option<AccountEffect>, AccountError>;

    /// Installs the browser-minted list of methods this profile can serve.
    fn set_available_methods(&mut self, methods: Vec<AccountAuthMethod>);

    /// Removes generation-bound state for one failed or cancelled flow, and
    /// records why it ended.
    ///
    /// The reason is a parameter rather than a second call because a flow
    /// removed without one is indistinguishable from a flow that never
    /// started, and the status projection can only render that as signed out.
    fn cancel_flow(&mut self, flow_id: &AuthFlowId, failure: AccountFailure) -> bool;

    /// Fences every account mutation in this utility generation after an
    /// ambiguous provider or vault operation.
    ///
    /// This only ever narrows: it drops the portable session and refuses later
    /// account work until profile bootstrap builds a fresh protocol against a
    /// browser that has reconciled both physical halves.
    fn require_reconciliation(&mut self);
}

impl<T> AccountPort for Box<T>
where
    T: AccountPort + ?Sized,
{
    fn session(&self) -> Option<&AccountSession> {
        self.as_ref().session()
    }

    fn available_methods(&self) -> &[AccountAuthMethod] {
        self.as_ref().available_methods()
    }

    fn last_failure(&self) -> Option<&AccountFailure> {
        self.as_ref().last_failure()
    }

    fn pending_email(&self) -> Option<&AccountEmail> {
        self.as_ref().pending_email()
    }

    fn set_available_methods(&mut self, methods: Vec<AccountAuthMethod>) {
        self.as_mut().set_available_methods(methods);
    }

    fn pending_count(&self) -> usize {
        self.as_ref().pending_count()
    }

    fn reconciliation_required(&self) -> bool {
        self.as_ref().reconciliation_required()
    }

    fn restore_session(&mut self, session: Option<AccountSession>) {
        self.as_mut().restore_session(session);
    }

    fn begin_authorization(
        &mut self,
        intent: AuthorizationIntent,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut().begin_authorization(intent, now_millis)
    }

    fn begin_native_authorization(
        &mut self,
        flow_id: AuthFlowId,
        auth_method: AccountAuthMethod,
        deadline: Deadline,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut()
            .begin_native_authorization(flow_id, auth_method, deadline, now_millis)
    }

    fn accept_authorization_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &AuthorizationEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut()
            .accept_authorization_entropy(flow_id, entropy, digest_port)
    }

    fn accept_pkce_verifier_handle(
        &mut self,
        flow_id: &AuthFlowId,
        handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut().accept_pkce_verifier_handle(flow_id, handle)
    }

    fn accept_google_nonce_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &GoogleNonceEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut()
            .accept_google_nonce_entropy(flow_id, entropy, digest_port)
    }

    fn accept_google_raw_nonce_handle(
        &mut self,
        flow_id: &AuthFlowId,
        raw_nonce_handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut()
            .accept_google_raw_nonce_handle(flow_id, raw_nonce_handle)
    }

    fn accept_redirect(
        &mut self,
        receipt: &RedirectReceipt,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut().accept_redirect(receipt, now_millis)
    }

    fn accept_authorization_surface(
        &mut self,
        flow_id: &AuthFlowId,
        opened: bool,
    ) -> Result<(), AccountError> {
        self.as_mut().accept_authorization_surface(flow_id, opened)
    }

    fn accept_email_link_delivery(
        &mut self,
        flow_id: &AuthFlowId,
        accepted: bool,
    ) -> Result<(), AccountError> {
        self.as_mut().accept_email_link_delivery(flow_id, accepted)
    }

    fn accept_native_credential(
        &mut self,
        flow_id: &AuthFlowId,
        auth_method: AccountAuthMethod,
        outcome: NativeCredentialOutcome,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.as_mut()
            .accept_native_credential(flow_id, auth_method, outcome, now_millis)
    }

    fn accept_exchange(
        &mut self,
        flow_id: &AuthFlowId,
        receipt: SessionReceipt,
    ) -> Result<AccountSession, AccountError> {
        self.as_mut().accept_exchange(flow_id, receipt)
    }

    fn begin_refresh(&mut self, now_millis: u64) -> Result<Option<AccountEffect>, AccountError> {
        self.as_mut().begin_refresh(now_millis)
    }

    fn accept_refresh(&mut self, receipt: SessionReceipt) -> Result<AccountSession, AccountError> {
        self.as_mut().accept_refresh(receipt)
    }

    fn settle_refresh_failure(&mut self, outcome_unknown: bool) -> bool {
        self.as_mut().settle_refresh_failure(outcome_unknown)
    }

    fn begin_sign_out(&mut self) -> Result<Option<AccountEffect>, AccountError> {
        self.as_mut().begin_sign_out()
    }

    fn cancel_flow(&mut self, flow_id: &AuthFlowId, failure: AccountFailure) -> bool {
        self.as_mut().cancel_flow(flow_id, failure)
    }

    fn require_reconciliation(&mut self) {
        self.as_mut().require_reconciliation();
    }
}
