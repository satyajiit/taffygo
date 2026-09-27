// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exactly-once profile account operations over the generated Core Service contract.

use std::collections::{BTreeMap, BTreeSet, VecDeque};

use core_service_types as wire;

use crate::account::{
    AccountAuthMethod, AccountError, AccountFailure, AccountFailureCode, AuthFlowId, Sha256Port,
};
use crate::contract::ServiceGeneration;
use crate::ports::AccountPort;

use self::validation::{validate_completion_operation, validate_operation};
use crate::codec::account_wire::{encode_account_effect, AccountWireError};

mod command;
mod completion;
mod validation;

#[cfg(test)]
mod tests;

use command::BeginCommand;
use completion::{
    auth_method_of, complete_stage, derive_effect_id, failure_code_for, stage_and_flow,
};

const MAX_COMPLETED_ACCOUNT_OPERATIONS: usize = 64;

/// One accepted account command and any next effect it emitted.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AccountServiceStep {
    pub operation_id: String,
    pub admission: wire::AdmissionStatus,
    pub effects: Vec<wire::EffectEnvelope>,
    pub state_changed: bool,
}

/// Why a generated account command or completion failed closed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AccountServiceError {
    InvalidCommand,
    StaleGeneration,
    DeadlineExceeded,
    Backpressure,
    DuplicateCompletion,
    InvalidCompletion,
    Domain(AccountError),
    Wire(AccountWireError),
    PrivateProfile,
    AccountMethodUnavailable,
}

impl From<AccountError> for AccountServiceError {
    fn from(value: AccountError) -> Self {
        Self::Domain(value)
    }
}

impl From<AccountWireError> for AccountServiceError {
    fn from(value: AccountWireError) -> Self {
        Self::Wire(value)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum AccountStage {
    Entropy,
    Verifier,
    GoogleNonceEntropy,
    GoogleNonceHandle,
    OAuthSurface,
    NativeSurface,
    NativeNonceCleanup,
    EmailLink,
    CodeExchange,
    NativeExchange,
    SignOut,
    Refresh,
}

impl AccountStage {
    const fn label(self) -> &'static str {
        match self {
            Self::Entropy => "entropy",
            Self::Verifier => "verifier",
            Self::GoogleNonceEntropy => "google-nonce-entropy",
            Self::GoogleNonceHandle => "google-nonce-handle",
            Self::OAuthSurface => "oauth-surface",
            Self::NativeSurface => "native-surface",
            Self::NativeNonceCleanup => "native-nonce-cleanup",
            Self::EmailLink => "email-link",
            Self::CodeExchange => "code-exchange",
            Self::NativeExchange => "native-exchange",
            Self::SignOut => "sign-out",
            Self::Refresh => "refresh",
        }
    }

    const fn changes_session(self) -> bool {
        matches!(
            self,
            Self::CodeExchange | Self::NativeExchange | Self::SignOut | Self::Refresh
        )
    }
}

#[derive(Clone, Debug)]
struct PendingAccountOperation {
    operation: wire::OperationEnvelope,
    effect_id: String,
    flow_id: Option<AuthFlowId>,
    stage: AccountStage,
    /// Which of the four buttons this effect belongs to, when the stage names
    /// one. Absent for the entropy and verifier stages, whose effects carry no
    /// method — a failure there is reported without naming a method rather than
    /// by guessing one.
    auth_method: Option<AccountAuthMethod>,
}

/// Canonical profile-owned account operation table.
#[derive(Debug, Default)]
pub struct AccountOperationRuntime {
    pending: BTreeMap<String, PendingAccountOperation>,
    completed: BTreeSet<String>,
    completed_order: VecDeque<String>,
}

impl AccountOperationRuntime {
    pub const fn new() -> Self {
        Self {
            pending: BTreeMap::new(),
            completed: BTreeSet::new(),
            completed_order: VecDeque::new(),
        }
    }

    /// Admits one complete generated account command on the ordered service sequence.
    pub fn submit(
        &mut self,
        account: &mut dyn AccountPort,
        generation: ServiceGeneration,
        command: &wire::CoreServiceCommand,
        now_millis: u64,
        digest: &dyn Sha256Port,
    ) -> Result<AccountServiceStep, AccountServiceError> {
        validate_operation(&command.operation, generation, now_millis)?;
        if !command.has_valid_body() {
            return Err(AccountServiceError::InvalidCommand);
        }
        let operation_id = command.operation.operation_id.clone();
        if self.pending.contains_key(&operation_id) || self.completed.contains(&operation_id) {
            return Ok(AccountServiceStep {
                operation_id,
                admission: wire::AdmissionStatus::Duplicate,
                effects: Vec::new(),
                state_changed: false,
            });
        }
        if account.reconciliation_required() {
            return Err(AccountError::ReconciliationRequired.into());
        }
        if self.pending.len() >= wire::MAX_IN_FLIGHT_PER_PROFILE {
            return Err(AccountServiceError::Backpressure);
        }

        let effect = match command::begin(account, command, now_millis)? {
            BeginCommand::Effect(effect) => effect,
            BeginCommand::Terminal { state_changed } => {
                self.remember_terminal(operation_id.clone());
                return Ok(AccountServiceStep {
                    operation_id,
                    admission: wire::AdmissionStatus::Accepted,
                    effects: Vec::new(),
                    state_changed,
                });
            }
        };
        let wire_effect = self.track_effect(command.operation.clone(), effect, digest)?;
        Ok(AccountServiceStep {
            operation_id,
            admission: wire::AdmissionStatus::Accepted,
            effects: vec![wire_effect],
            state_changed: true,
        })
    }

    /// Delivers one exact terminal browser result and emits at most one next effect.
    pub fn deliver(
        &mut self,
        account: &mut dyn AccountPort,
        generation: ServiceGeneration,
        result: &wire::EffectResult,
        now_millis: u64,
        digest: &dyn Sha256Port,
    ) -> Result<AccountServiceStep, AccountServiceError> {
        validate_completion_operation(&result.operation, generation, now_millis, result.status)?;
        let operation_id = result.operation.operation_id.clone();
        let Some(pending) = self.pending.get(&operation_id).cloned() else {
            return if self.completed.contains(&operation_id) {
                Err(AccountServiceError::DuplicateCompletion)
            } else {
                Err(AccountServiceError::InvalidCompletion)
            };
        };
        if pending.effect_id != result.effect_id
            || pending.operation != result.operation
            || !result.has_valid_body()
        {
            return Err(AccountServiceError::InvalidCompletion);
        }
        if account.reconciliation_required() {
            return Err(AccountError::ReconciliationRequired.into());
        }
        if result.status != wire::EffectStatus::Completed {
            const OUTCOME_UNKNOWN: wire::EffectStatus = wire::EffectStatus::OutcomeUnknown;
            if pending.stage == AccountStage::Refresh {
                account.settle_refresh_failure(result.status == OUTCOME_UNKNOWN);
            } else if let Some(flow_id) = pending.flow_id.as_ref() {
                account.cancel_flow(
                    flow_id,
                    AccountFailure {
                        code: failure_code_for(result.status),
                        method: pending.auth_method,
                    },
                );
            }
            if result.status == OUTCOME_UNKNOWN && pending.stage.changes_session() {
                account.require_reconciliation();
            }
            self.pending.remove(&operation_id);
            self.remember_terminal(operation_id.clone());
            return Ok(AccountServiceStep {
                operation_id,
                admission: wire::AdmissionStatus::Accepted,
                effects: Vec::new(),
                state_changed: true,
            });
        }

        let next = complete_stage(account, &pending, result, digest)?;
        self.pending.remove(&operation_id);
        let effects = if let Some(effect) = next {
            vec![self.track_effect(result.operation.clone(), effect, digest)?]
        } else {
            self.remember_terminal(operation_id.clone());
            Vec::new()
        };
        Ok(AccountServiceStep {
            operation_id,
            admission: wire::AdmissionStatus::Accepted,
            effects,
            state_changed: true,
        })
    }

    pub fn cancel_all(&mut self, account: &mut dyn AccountPort) {
        for pending in self.pending.values() {
            if pending.stage == AccountStage::Refresh {
                account.settle_refresh_failure(false);
            } else if let Some(flow_id) = pending.flow_id.as_ref() {
                // Nothing about the attempt itself went wrong; the core stopped
                // accepting account work underneath it, and that is what the
                // person is owed rather than a guess at the provider.
                account.cancel_flow(
                    flow_id,
                    AccountFailure {
                        code: AccountFailureCode::CoreUnavailable,
                        method: pending.auth_method,
                    },
                );
            }
        }
        self.pending.clear();
    }

    fn track_effect(
        &mut self,
        operation: wire::OperationEnvelope,
        effect: crate::account::AccountEffect,
        digest: &dyn Sha256Port,
    ) -> Result<wire::EffectEnvelope, AccountServiceError> {
        let (stage, flow_id) = stage_and_flow(&effect);
        let auth_method = auth_method_of(&effect);
        let effect_id = derive_effect_id(&operation.operation_id, stage, digest)?;
        let wire_effect = encode_account_effect(effect, operation.clone(), effect_id.clone())?;
        self.pending.insert(
            operation.operation_id.clone(),
            PendingAccountOperation {
                operation,
                effect_id,
                flow_id,
                stage,
                auth_method,
            },
        );
        Ok(wire_effect)
    }

    fn remember_terminal(&mut self, operation_id: String) {
        if self.completed.insert(operation_id.clone()) {
            self.completed_order.push_back(operation_id);
        }
        while self.completed_order.len() > MAX_COMPLETED_ACCOUNT_OPERATIONS {
            if let Some(expired) = self.completed_order.pop_front() {
                self.completed.remove(&expired);
            }
        }
    }
}
