// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated broker completion decoding for durability operations.

use core_service_types as wire;
use task_engine::IdempotencyKey;

use crate::contract::{
    CancellationReason, Completion, Deadline, EnvelopeError, OperationEnvelope, OperationId,
    RuntimeError, ServiceGeneration,
};

/// Why a generated broker result could not become a storage completion.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionWireError {
    InvalidBody,
    WrongKind,
    MismatchedEffectIdentity,
    InvalidEnvelope(EnvelopeError),
}

/// Decodes one generated storage result into the exact canonical completion.
///
/// Status mapping lives here so service glue cannot reinterpret broker
/// failures or treat arbitrary success bytes as durable commit evidence.
pub fn decode_storage_completion(
    result: &wire::EffectResult,
) -> Result<OperationEnvelope<Completion>, CompletionWireError> {
    if !result.has_valid_body() {
        return Err(CompletionWireError::InvalidBody);
    }
    if result.kind != wire::EffectKind::StorageCommit {
        return Err(CompletionWireError::WrongKind);
    }
    if result.effect_id != result.operation.operation_id {
        return Err(CompletionWireError::MismatchedEffectIdentity);
    }
    let body = match result.status {
        wire::EffectStatus::Completed => Completion::StorageCommitted {
            committed_revision: result
                .storage
                .as_ref()
                .ok_or(CompletionWireError::InvalidBody)?
                .committed_revision,
        },
        wire::EffectStatus::Denied => Completion::Failed(RuntimeError::StorageConflict),
        wire::EffectStatus::Cancelled => Completion::Cancelled(CancellationReason::User),
        wire::EffectStatus::DeadlineExceeded => Completion::Failed(RuntimeError::DeadlineExceeded),
        wire::EffectStatus::ResourceLimit | wire::EffectStatus::Unavailable => {
            Completion::Failed(RuntimeError::PortUnavailable)
        }
        wire::EffectStatus::OutcomeUnknown => Completion::OutcomeUnknown,
        wire::EffectStatus::InvalidResult => Completion::Failed(RuntimeError::ProtocolViolation),
    };
    OperationEnvelope::new(
        OperationId::new(result.operation.operation_id.clone())
            .map_err(CompletionWireError::InvalidEnvelope)?,
        ServiceGeneration::new(result.operation.service_generation),
        result.operation.task_revision,
        Deadline::from_millis(result.operation.deadline_monotonic_ms),
        IdempotencyKey::new(result.operation.idempotency_key.clone()),
        body,
    )
    .map_err(CompletionWireError::InvalidEnvelope)
}

#[cfg(test)]
mod tests {
    use super::{decode_storage_completion, CompletionWireError};
    use crate::contract::Completion;
    use core_service_types as wire;

    fn result() -> wire::EffectResult {
        wire::EffectResult {
            operation: wire::OperationEnvelope {
                operation_id: "commit-1".to_owned(),
                service_generation: 1,
                task_revision: 2,
                deadline_monotonic_ms: 100,
                idempotency_key: "task-command-1".to_owned(),
            },
            effect_id: "commit-1".to_owned(),
            status: wire::EffectStatus::Completed,
            kind: wire::EffectKind::StorageCommit,
            storage: Some(wire::StorageEffectResult {
                committed_revision: 2,
            }),
            observation: None,
            model: None,
            network: None,
            browser_action: None,
            tool: None,
            secure_store: None,
            auth_surface: None,
            permission: None,
            asset_delivery: None,
            catalog: None,
            provider_listing: None,
            composer_completion: None,
            custom_endpoint_probe: None,
        }
    }

    #[test]
    fn generated_storage_success_keeps_the_exact_durable_revision() {
        let decoded = decode_storage_completion(&result()).unwrap_or_else(|_| unreachable!());
        assert_eq!(
            decoded.body,
            Completion::StorageCommitted {
                committed_revision: 2
            }
        );
    }

    #[test]
    fn a_result_for_another_effect_identity_fails_closed() {
        let mut result = result();
        result.effect_id = "other".to_owned();
        assert_eq!(
            decode_storage_completion(&result),
            Err(CompletionWireError::MismatchedEffectIdentity)
        );
    }
}
