// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Whether one wire envelope may be acted on at all.
//!
//! Separated from the runtime that acts on it because these answer a different
//! question: not what the command means, but whether this generation and this
//! moment may consider it. Every one is a pure function of the envelope, so
//! none of them can reach the protocol, and the runtime is left holding only
//! the work that changes state.

use core_service_types as wire;

use crate::contract::ServiceGeneration;

use super::AccountServiceError;

pub(super) fn validate_operation(
    operation: &wire::OperationEnvelope,
    generation: ServiceGeneration,
    now_millis: u64,
) -> Result<(), AccountServiceError> {
    validate_operation_identity(operation, generation)?;
    if now_millis >= operation.deadline_monotonic_ms {
        return Err(AccountServiceError::DeadlineExceeded);
    }
    Ok(())
}

pub(super) fn validate_completion_operation(
    operation: &wire::OperationEnvelope,
    generation: ServiceGeneration,
    now_millis: u64,
    status: wire::EffectStatus,
) -> Result<(), AccountServiceError> {
    validate_operation_identity(operation, generation)?;
    if now_millis >= operation.deadline_monotonic_ms && status == wire::EffectStatus::Completed {
        return Err(AccountServiceError::DeadlineExceeded);
    }
    Ok(())
}

fn validate_operation_identity(
    operation: &wire::OperationEnvelope,
    generation: ServiceGeneration,
) -> Result<(), AccountServiceError> {
    if operation.service_generation != generation.value() {
        return Err(AccountServiceError::StaleGeneration);
    }
    if operation.task_revision != 0
        || operation.operation_id.is_empty()
        || operation.operation_id.len() > wire::MAX_OPERATION_ID_BYTES
        || operation.idempotency_key.is_empty()
        || operation.idempotency_key.len() > wire::MAX_IDEMPOTENCY_KEY_BYTES
    {
        return Err(AccountServiceError::InvalidCommand);
    }
    Ok(())
}
