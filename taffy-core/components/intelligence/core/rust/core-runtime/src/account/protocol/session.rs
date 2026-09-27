// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable session, refresh, and generation-bound flow cleanup.

use super::super::{
    AccountAuthMethod, AccountEffect, AccountEmail, AccountEndpointId, AccountError,
    AccountFailure, AccountFailureCode, AccountSession, AuthFlowId, RefreshPlan, SessionReceipt,
};
use super::{AccountProtocol, REFRESH_AHEAD_MILLIS};

impl AccountProtocol {
    /// Records a browser-owned session handle after a successful exchange.
    pub fn accept_exchange(
        &mut self,
        flow_id: &AuthFlowId,
        receipt: SessionReceipt,
    ) -> Result<AccountSession, AccountError> {
        let Some(expected_method) = self.exchange_in_flight.get(flow_id).copied() else {
            return Err(AccountError::UnknownFlow);
        };
        if expected_method != receipt.auth_method {
            return Err(AccountError::AuthMethodMismatch);
        }
        if receipt.rotation != 0 || self.refresh_in_flight.is_some() {
            return Err(AccountError::RefreshRotationMismatch);
        }
        if self.session.is_some() {
            return Err(AccountError::SessionAlreadyExists);
        }
        self.exchange_in_flight.remove(flow_id);
        Ok(self.store_session(receipt))
    }

    fn store_session(&mut self, receipt: SessionReceipt) -> AccountSession {
        // A refresh reaches here too, and a refresh that came back without an
        // identity must not erase the one already held. The provider is not
        // obliged to repeat the user object on every rotation, and a name that
        // vanished an hour into a session would look like a bug in the screen
        // rather than in the response. An initial exchange has no previous
        // session, so the fallback costs it nothing.
        let previous = self.session.as_ref();
        let email = receipt
            .email
            .or_else(|| previous.and_then(|session| session.email.clone()));
        let display_name = receipt
            .display_name
            .or_else(|| previous.and_then(|session| session.display_name.clone()));
        let session = AccountSession {
            session_handle: receipt.session_handle,
            auth_method: receipt.auth_method,
            account_subject: receipt.account_subject,
            expires_at: receipt.expires_at,
            rotation: receipt.rotation,
            email,
            display_name,
        };
        self.session = Some(session.clone());
        self.refresh_in_flight = None;
        // Signed in is neither failed nor waiting for a link.
        self.clear_terminal_markers();
        session
    }

    /// The current handle-only account state.
    pub const fn session(&self) -> Option<&AccountSession> {
        self.session.as_ref()
    }

    /// Plans a refresh before expiry without exposing its rotating token.
    pub fn begin_refresh(
        &mut self,
        now_millis: u64,
    ) -> Result<Option<AccountEffect>, AccountError> {
        if self.reconciliation_required {
            return Err(AccountError::ReconciliationRequired);
        }
        if self.pending_count() != 0 {
            return Err(AccountError::SessionMutationInFlight);
        }
        if self.refresh_in_flight.is_some() {
            return Err(AccountError::RefreshAlreadyInFlight);
        }
        let Some(session) = self.session.as_ref() else {
            return Ok(None);
        };
        if now_millis.saturating_add(REFRESH_AHEAD_MILLIS) < session.expires_at.as_millis() {
            return Ok(None);
        }
        let next_rotation = session
            .rotation
            .checked_add(1)
            .ok_or(AccountError::RefreshRotationExhausted)?;
        self.refresh_in_flight = Some(next_rotation);
        Ok(Some(AccountEffect::Refresh(RefreshPlan {
            session_handle: session.session_handle.clone(),
            expected_rotation: next_rotation,
            expected_account_subject: session.account_subject.clone(),
            expected_auth_method: session.auth_method,
            endpoint: AccountEndpointId::SupabaseToken,
        })))
    }

    /// Commits one rotating-token refresh and unlocks the next attempt.
    pub fn accept_refresh(
        &mut self,
        receipt: SessionReceipt,
    ) -> Result<AccountSession, AccountError> {
        if self.refresh_in_flight != Some(receipt.rotation) {
            return Err(AccountError::RefreshRotationMismatch);
        }
        let Some(current) = self.session.as_ref() else {
            return Err(AccountError::RefreshRotationMismatch);
        };
        if current.auth_method != receipt.auth_method {
            return Err(AccountError::AuthMethodMismatch);
        }
        if current.account_subject != receipt.account_subject {
            return Err(AccountError::AccountSubjectMismatch);
        }
        Ok(self.store_session(receipt))
    }

    /// Settles a refresh that did not produce a committed receipt.
    ///
    /// A known failure unlocks an explicit later attempt against the current
    /// version. An ambiguous dispatch drops the portable session entirely so
    /// neither this generation nor a restored checkpoint can blindly reuse a
    /// token whose rotation may already have been spent by the provider.
    pub fn settle_refresh_failure(&mut self, outcome_unknown: bool) -> bool {
        if self.refresh_in_flight.take().is_none() {
            return false;
        }
        if outcome_unknown {
            self.session = None;
            self.reconciliation_required = true;
            // The session is gone and the browser has to reconcile both halves
            // before anything else may run, so the person is signed out for a
            // reason and the screen should say which.
            self.record_failure(AccountFailure::for_profile(
                AccountFailureCode::CoreUnavailable,
            ));
        }
        true
    }

    /// Clears portable state and returns the secure-store handle to revoke.
    pub fn begin_sign_out(&mut self) -> Result<Option<AccountEffect>, AccountError> {
        if self.reconciliation_required {
            return Err(AccountError::ReconciliationRequired);
        }
        Ok(self.session.take().map(|session| {
            self.refresh_in_flight = None;
            AccountEffect::SignOut {
                session_handle: session.session_handle,
                endpoint: AccountEndpointId::SupabaseRevoke,
            }
        }))
    }

    /// Whether the browser must reconcile its durable SQL and vault halves
    /// before a later utility generation may accept account work.
    pub const fn reconciliation_required(&self) -> bool {
        self.reconciliation_required
    }

    /// Fences every account mutation in this utility generation after an
    /// ambiguous provider or vault operation. Only profile bootstrap creates
    /// a fresh protocol after the browser has reconciled both physical halves.
    pub(crate) fn require_reconciliation(&mut self) {
        self.awaiting_entropy.clear();
        self.awaiting_verifier_handle.clear();
        self.pending_redirect.clear();
        self.awaiting_native_nonce_entropy.clear();
        self.awaiting_native_nonce_handle.clear();
        self.pending_native_credential.clear();
        self.exchange_in_flight.clear();
        self.session = None;
        self.refresh_in_flight = None;
        self.reconciliation_required = true;
        // Every flow this generation held has just been dropped, so the profile
        // owes the person a reason. It is not attributed to a method: what is
        // known is that the core can no longer accept account work, which is
        // true of all four.
        self.record_failure(AccountFailure::for_profile(
            AccountFailureCode::CoreUnavailable,
        ));
    }

    pub(super) fn contains_flow(&self, flow_id: &AuthFlowId) -> bool {
        self.awaiting_entropy.contains_key(flow_id)
            || self.awaiting_verifier_handle.contains_key(flow_id)
            || self.pending_redirect.contains_key(flow_id)
            || self.awaiting_native_nonce_entropy.contains_key(flow_id)
            || self.awaiting_native_nonce_handle.contains_key(flow_id)
            || self.pending_native_credential.contains_key(flow_id)
            || self.exchange_in_flight.contains_key(flow_id)
    }

    pub(super) fn ensure_sign_in_available(&self) -> Result<(), AccountError> {
        if self.reconciliation_required {
            return Err(AccountError::ReconciliationRequired);
        }
        if self.session.is_some() {
            return Err(AccountError::SessionAlreadyExists);
        }
        if self.refresh_in_flight.is_some() || self.pending_count() != 0 {
            return Err(AccountError::SessionMutationInFlight);
        }
        Ok(())
    }

    /// Number of bounded flows across entropy, secure-store, and redirect stages.
    pub fn pending_count(&self) -> usize {
        self.awaiting_entropy
            .len()
            .saturating_add(self.awaiting_verifier_handle.len())
            .saturating_add(self.pending_redirect.len())
            .saturating_add(self.awaiting_native_nonce_entropy.len())
            .saturating_add(self.awaiting_native_nonce_handle.len())
            .saturating_add(self.pending_native_credential.len())
            .saturating_add(self.exchange_in_flight.len())
    }

    /// Removes generation-bound state for one failed or cancelled flow, and
    /// records why.
    ///
    /// The reason is a required argument rather than a second call, because the
    /// two must not be able to come apart. A flow removed without a reason
    /// leaves exactly the state a flow that never started leaves, and the
    /// status projection cannot tell those apart — which is what made every
    /// failed sign-in read as "signed out" and left six translated failure
    /// sentences unreachable.
    pub fn cancel_flow(&mut self, flow_id: &AuthFlowId, failure: AccountFailure) -> bool {
        let entropy = self.awaiting_entropy.remove(flow_id).is_some();
        let verifier = self.awaiting_verifier_handle.remove(flow_id).is_some();
        let redirect = self.pending_redirect.remove(flow_id).is_some();
        let nonce_entropy = self.awaiting_native_nonce_entropy.remove(flow_id).is_some();
        let nonce_handle = self.awaiting_native_nonce_handle.remove(flow_id).is_some();
        let native = self.pending_native_credential.remove(flow_id).is_some();
        let exchange = self.exchange_in_flight.remove(flow_id).is_some();
        let removed =
            entropy || verifier || redirect || nonce_entropy || nonce_handle || native || exchange;
        if removed {
            self.record_failure(failure);
        }
        removed
    }

    /// Installs the browser-minted list of methods this profile can serve.
    pub fn set_available_methods(&mut self, methods: Vec<AccountAuthMethod>) {
        self.available_methods = methods;
    }

    /// The methods this profile can serve, in the order the browser sent them.
    pub fn available_methods(&self) -> &[AccountAuthMethod] {
        &self.available_methods
    }

    /// The last terminal failure, if the profile has not since moved on.
    pub const fn last_failure(&self) -> Option<&AccountFailure> {
        self.last_failure.as_ref()
    }

    /// The address a sign-in link was accepted for, while it is still current.
    pub const fn pending_email(&self) -> Option<&AccountEmail> {
        self.link_sent.as_ref()
    }

    /// Records one terminal failure, displacing any sent-link state.
    pub(crate) fn record_failure(&mut self, failure: AccountFailure) {
        self.last_failure = Some(failure);
        self.link_sent = None;
    }

    /// Records that the account plane accepted a link for this address.
    pub(crate) fn record_link_sent(&mut self, email: AccountEmail) {
        self.link_sent = Some(email);
        self.last_failure = None;
    }

    /// Clears both terminal markers because a fresh attempt is starting.
    ///
    /// An attempt in flight is neither failed nor waiting for a link, and a
    /// screen that kept showing the previous failure underneath a running
    /// spinner would be describing the wrong moment.
    pub(crate) fn clear_terminal_markers(&mut self) {
        self.last_failure = None;
        self.link_sent = None;
    }
}
