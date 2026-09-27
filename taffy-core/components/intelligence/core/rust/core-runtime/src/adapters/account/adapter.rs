// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical account adapter over the portable handle-only protocol.
//!
//! The adapter owns no secret, no clock, and no transport. It exists so the
//! pure [`AccountProtocol`] domain state stays free of the composition
//! contract, exactly as [`ProductionPolicy`](crate::adapters::policy::ProductionPolicy) keeps
//! `policy_engine::GrantPolicy` free of [`PolicyPort`](crate::ports::PolicyPort).

use crate::account::{
    AccountAuthMethod, AccountEffect, AccountEmail, AccountError, AccountFailure, AccountProtocol,
    AccountSession, AuthFlowId, AuthorizationEntropy, AuthorizationIntent, GoogleNonceEntropy,
    NativeCredentialOutcome, RedirectReceipt, SecretHandle, SessionReceipt, Sha256Port,
};
use crate::contract::Deadline;
use crate::ports::AccountPort;

/// The one production account port for a profile generation.
#[derive(Clone, Debug, Default)]
pub struct ProductionAccount {
    protocol: AccountProtocol,
}

impl ProductionAccount {
    /// An empty protocol state offering no method.
    pub const fn new() -> Self {
        Self {
            protocol: AccountProtocol::new(),
        }
    }

    /// An empty protocol state that knows which methods the browser can serve.
    ///
    /// The list is browser-minted configuration, not something Rust can decide:
    /// whether the account plane is reachable, and whether this build carries a
    /// Google server client id, are both facts of the embedder.
    pub fn with_available_methods(methods: Vec<AccountAuthMethod>) -> Self {
        let mut protocol = AccountProtocol::new();
        protocol.set_available_methods(methods);
        Self { protocol }
    }
}

impl AccountPort for ProductionAccount {
    fn session(&self) -> Option<&AccountSession> {
        self.protocol.session()
    }

    fn available_methods(&self) -> &[AccountAuthMethod] {
        self.protocol.available_methods()
    }

    fn last_failure(&self) -> Option<&AccountFailure> {
        self.protocol.last_failure()
    }

    fn pending_email(&self) -> Option<&AccountEmail> {
        self.protocol.pending_email()
    }

    fn set_available_methods(&mut self, methods: Vec<AccountAuthMethod>) {
        self.protocol.set_available_methods(methods);
    }

    fn pending_count(&self) -> usize {
        self.protocol.pending_count()
    }

    fn reconciliation_required(&self) -> bool {
        self.protocol.reconciliation_required()
    }

    fn restore_session(&mut self, session: Option<AccountSession>) {
        // Rebuilding the protocol is what makes restore total, but the method
        // list is browser-minted configuration rather than flow state, so it
        // has to survive. Losing it here would report every method as
        // unconfigured on exactly the profiles that already had a session.
        let methods = self.protocol.available_methods().to_vec();
        self.protocol = AccountProtocol::restore_session(session);
        self.protocol.set_available_methods(methods);
    }

    fn begin_authorization(
        &mut self,
        intent: AuthorizationIntent,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol.begin_authorization(intent, now_millis)
    }

    fn begin_native_authorization(
        &mut self,
        flow_id: AuthFlowId,
        auth_method: AccountAuthMethod,
        deadline: Deadline,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol
            .begin_native_authorization(flow_id, auth_method, deadline, now_millis)
    }

    fn accept_authorization_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &AuthorizationEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol
            .accept_authorization_entropy(flow_id, entropy, digest_port)
    }

    fn accept_pkce_verifier_handle(
        &mut self,
        flow_id: &AuthFlowId,
        handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol.accept_pkce_verifier_handle(flow_id, handle)
    }

    fn accept_google_nonce_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &GoogleNonceEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol
            .accept_google_nonce_entropy(flow_id, entropy, digest_port)
    }

    fn accept_google_raw_nonce_handle(
        &mut self,
        flow_id: &AuthFlowId,
        raw_nonce_handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol
            .accept_google_raw_nonce_handle(flow_id, raw_nonce_handle)
    }

    fn accept_redirect(
        &mut self,
        receipt: &RedirectReceipt,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol.accept_redirect(receipt, now_millis)
    }

    fn accept_authorization_surface(
        &mut self,
        flow_id: &AuthFlowId,
        opened: bool,
    ) -> Result<(), AccountError> {
        self.protocol.accept_authorization_surface(flow_id, opened)
    }

    fn accept_email_link_delivery(
        &mut self,
        flow_id: &AuthFlowId,
        accepted: bool,
    ) -> Result<(), AccountError> {
        self.protocol.accept_email_link_delivery(flow_id, accepted)
    }

    fn accept_native_credential(
        &mut self,
        flow_id: &AuthFlowId,
        auth_method: AccountAuthMethod,
        outcome: NativeCredentialOutcome,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        self.protocol
            .accept_native_credential(flow_id, auth_method, outcome, now_millis)
    }

    fn accept_exchange(
        &mut self,
        flow_id: &AuthFlowId,
        receipt: SessionReceipt,
    ) -> Result<AccountSession, AccountError> {
        self.protocol.accept_exchange(flow_id, receipt)
    }

    fn begin_refresh(&mut self, now_millis: u64) -> Result<Option<AccountEffect>, AccountError> {
        self.protocol.begin_refresh(now_millis)
    }

    fn accept_refresh(&mut self, receipt: SessionReceipt) -> Result<AccountSession, AccountError> {
        self.protocol.accept_refresh(receipt)
    }

    fn settle_refresh_failure(&mut self, outcome_unknown: bool) -> bool {
        self.protocol.settle_refresh_failure(outcome_unknown)
    }

    fn begin_sign_out(&mut self) -> Result<Option<AccountEffect>, AccountError> {
        self.protocol.begin_sign_out()
    }

    fn cancel_flow(&mut self, flow_id: &AuthFlowId, failure: AccountFailure) -> bool {
        self.protocol.cancel_flow(flow_id, failure)
    }

    fn require_reconciliation(&mut self) {
        self.protocol.require_reconciliation();
    }
}
