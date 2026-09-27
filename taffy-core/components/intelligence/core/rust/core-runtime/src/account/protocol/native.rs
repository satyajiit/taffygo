// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Native credential authorization stages.

use crate::contract::Deadline;

use super::{
    AccountProtocol, NativeNonceMaterial, PendingNativeCredential, MAX_PENDING_AUTH_FLOWS,
};
use crate::account::crypto::{derive_google_nonce_material, Sha256Port};
use crate::account::{
    AccountAuthMethod, AccountEffect, AccountError, AccountFailure, AccountFailureCode, AuthFlowId,
    GoogleNonceEntropy, NativeCredentialExchangePlan, NativeCredentialOutcome, SecretHandle,
    GOOGLE_NONCE_ENTROPY_BYTES,
};

impl AccountProtocol {
    /// Starts one native credential flow by requesting nonce entropy first.
    pub fn begin_native_authorization(
        &mut self,
        flow_id: AuthFlowId,
        auth_method: AccountAuthMethod,
        deadline: Deadline,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        if auth_method != AccountAuthMethod::Google {
            return Err(AccountError::InvalidAuthMethod);
        }
        if deadline.is_expired_at(now_millis) {
            return Err(AccountError::DeadlineExceeded);
        }
        if self.contains_flow(&flow_id) {
            return Err(AccountError::DuplicateFlow);
        }
        self.ensure_sign_in_available()?;
        if self.pending_count() >= MAX_PENDING_AUTH_FLOWS {
            return Err(AccountError::TooManyPendingFlows);
        }
        self.awaiting_native_nonce_entropy
            .insert(flow_id.clone(), (auth_method, deadline));
        // An attempt that is starting is neither failed nor waiting for a link.
        self.clear_terminal_markers();
        Ok(AccountEffect::RequestGoogleNonceEntropy {
            flow_id,
            bytes: GOOGLE_NONCE_ENTROPY_BYTES,
        })
    }

    /// Derives a raw/hashed Google nonce pair and sends only the raw value to secure storage.
    pub fn accept_google_nonce_entropy(
        &mut self,
        flow_id: &AuthFlowId,
        entropy: &GoogleNonceEntropy,
        digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        let Some((auth_method, deadline)) =
            self.awaiting_native_nonce_entropy.get(flow_id).copied()
        else {
            return Err(AccountError::UnknownFlow);
        };
        let (raw_nonce, hashed_nonce) = derive_google_nonce_material(entropy, digest_port)?;
        self.awaiting_native_nonce_entropy.remove(flow_id);
        self.awaiting_native_nonce_handle.insert(
            flow_id.clone(),
            NativeNonceMaterial {
                auth_method,
                deadline,
                hashed_nonce: hashed_nonce.clone(),
            },
        );
        Ok(AccountEffect::StoreGoogleRawNonce {
            flow_id: flow_id.clone(),
            raw_nonce,
            hashed_nonce,
        })
    }

    /// Binds the raw nonce handle before the native Google surface opens.
    pub fn accept_google_raw_nonce_handle(
        &mut self,
        flow_id: &AuthFlowId,
        raw_nonce_handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        let Some(material) = self.awaiting_native_nonce_handle.remove(flow_id) else {
            return Err(AccountError::UnknownFlow);
        };
        self.pending_native_credential.insert(
            flow_id.clone(),
            PendingNativeCredential {
                auth_method: material.auth_method,
                deadline: material.deadline,
                raw_nonce_handle: raw_nonce_handle.clone(),
            },
        );
        Ok(AccountEffect::RequestNativeCredential {
            flow_id: flow_id.clone(),
            auth_method: material.auth_method,
            raw_nonce_handle,
            hashed_nonce: material.hashed_nonce,
        })
    }

    /// Consumes one terminal native credential result exactly once.
    pub fn accept_native_credential(
        &mut self,
        flow_id: &AuthFlowId,
        auth_method: AccountAuthMethod,
        outcome: NativeCredentialOutcome,
        now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        let Some(pending) = self.pending_native_credential.get(flow_id).cloned() else {
            return Err(AccountError::UnknownFlow);
        };
        if pending.auth_method != auth_method {
            return Err(AccountError::AuthMethodMismatch);
        }
        let nonce_cleanup = || AccountEffect::DeleteNativeNonce {
            flow_id: flow_id.clone(),
            raw_nonce_handle: pending.raw_nonce_handle.clone(),
        };
        if pending.deadline.is_expired_at(now_millis) {
            self.pending_native_credential.remove(flow_id);
            self.record_failure(AccountFailure::for_method(
                AccountFailureCode::Network,
                auth_method,
            ));
            return Ok(nonce_cleanup());
        }
        // Every arm here returns Ok, because deleting the raw nonce is the
        // right effect whether the sheet succeeded or not. That is exactly why
        // the reason has to be recorded rather than inferred from the return:
        // three of these four outcomes are the person seeing nothing happen.
        let handle = match outcome {
            NativeCredentialOutcome::Success(handle) => handle,
            NativeCredentialOutcome::Cancelled => {
                self.pending_native_credential.remove(flow_id);
                self.record_failure(AccountFailure::for_method(
                    AccountFailureCode::Cancelled,
                    auth_method,
                ));
                return Ok(nonce_cleanup());
            }
            NativeCredentialOutcome::NoCredential => {
                self.pending_native_credential.remove(flow_id);
                self.record_failure(AccountFailure::for_method(
                    AccountFailureCode::NoCredential,
                    auth_method,
                ));
                return Ok(nonce_cleanup());
            }
            NativeCredentialOutcome::Unavailable => {
                self.pending_native_credential.remove(flow_id);
                // The platform surface could not run at all, which on this path
                // is what an empty server client id produces: the browser
                // refuses before Credential Manager is ever reached.
                self.record_failure(AccountFailure::for_method(
                    AccountFailureCode::NotConfigured,
                    auth_method,
                ));
                return Ok(nonce_cleanup());
            }
        };
        self.pending_native_credential.remove(flow_id);
        self.exchange_in_flight.insert(flow_id.clone(), auth_method);
        Ok(AccountEffect::ExchangeNativeCredential(
            NativeCredentialExchangePlan {
                flow_id: flow_id.clone(),
                auth_method,
                credential_handle: handle,
                raw_nonce_handle: pending.raw_nonce_handle,
                deadline: pending.deadline,
            },
        ))
    }
}
