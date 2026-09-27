// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The profile runtime's account surface.
//!
//! Restore, admission and exactly-once delivery for the signed-in account,
//! moved out of `profile.rs` unchanged. A private profile has no account, so
//! it refuses a restored session, a submitted command and a delivered result.
//! Teardown cancellation is unconditional: a profile that was never allowed
//! an operation has none to cancel, and refusing there would say otherwise.

use std::rc::Rc;

use crate::account::{AccountAuthMethod, AccountSession};
use crate::adapters::account::{AccountServiceError, AccountServiceStep};

use super::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    /// Restores committed handle-only account state during bootstrap.
    pub fn restore_account_session(
        &mut self,
        session: Option<AccountSession>,
    ) -> Result<(), crate::runtime::AccountRestoreError> {
        if self.private_profile && session.is_some() {
            return Err(crate::runtime::AccountRestoreError::PrivateProfile);
        }
        self.core.restore_account_session(session)
    }

    /// Admits one typed account command through the exactly-once profile table.
    pub fn submit_account_command(
        &mut self,
        command: &core_service_types::CoreServiceCommand,
        now_millis: u64,
    ) -> Result<AccountServiceStep, AccountServiceError> {
        if self.private_profile {
            return Err(AccountServiceError::PrivateProfile);
        }
        if let Some(start) = command.start_auth.as_ref() {
            let method = match start.method {
                core_service_types::AccountAuthMethod::Google => AccountAuthMethod::Google,
                core_service_types::AccountAuthMethod::EmailLink => AccountAuthMethod::EmailLink,
                core_service_types::AccountAuthMethod::Github => AccountAuthMethod::Github,
                core_service_types::AccountAuthMethod::Facebook => AccountAuthMethod::Facebook,
            };
            if !self.available_account_methods.contains(&method) {
                return Err(AccountServiceError::AccountMethodUnavailable);
            }
        }
        if command.request_email_link.is_some()
            && !self
                .available_account_methods
                .contains(&AccountAuthMethod::EmailLink)
        {
            return Err(AccountServiceError::AccountMethodUnavailable);
        }
        let generation = self.core.service_generation();
        let digest = Rc::clone(&self.digest);
        self.account_operations.submit(
            self.core.account_mut(),
            generation,
            command,
            now_millis,
            digest.as_ref(),
        )
    }

    /// Delivers one typed terminal account effect result exactly once.
    pub fn deliver_account_effect_result(
        &mut self,
        result: &core_service_types::EffectResult,
        now_millis: u64,
    ) -> Result<AccountServiceStep, AccountServiceError> {
        if self.private_profile {
            return Err(AccountServiceError::PrivateProfile);
        }
        let generation = self.core.service_generation();
        let digest = Rc::clone(&self.digest);
        let step = self.account_operations.deliver(
            self.core.account_mut(),
            generation,
            result,
            now_millis,
            digest.as_ref(),
        );
        // The entitlement is the signed-in account's answer, and it goes when
        // the session does — in the same delivery, not on the next fetch. The
        // condition is the fact itself rather than "this was a sign-out",
        // because a session can also end by refusal, and an entitlement that
        // outlives its session either way is another account's balance drawn
        // on this screen.
        if step.is_ok()
            && self.core.account().session().is_none()
            && self.entitlement_refresh.summary().is_some()
        {
            self.clear_managed_entitlement();
        }
        step
    }

    /// Clears all generation-bound account operations before teardown.
    pub fn cancel_account_operations(&mut self) {
        self.account_operations.cancel_all(self.core.account_mut());
    }
}
