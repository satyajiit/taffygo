// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Total conversion from generated authority-neutral DTOs into policy values.

use bip_types::identity::{ActionId, MonotonicMillis, TaskId};
use core_service_types as wire;
use policy_engine::{GrantIdempotencyKey, GrantRequest};

use crate::contract::{Deadline, OperationId, ServiceGeneration};
use crate::runtime::BoxedCoreRuntime;

use super::operation::operation_kind;
use super::{DecodedPolicyRequest, PolicyWireError};

use self::authority::{
    actor_lease, authority_subject, context, discovery, principal, scope,
    validate_authority_context,
};
use self::values::{action_class, approval, digest, identifier, risk, sensitivities};

mod authority;
mod values;

pub(super) fn request(
    runtime: &BoxedCoreRuntime,
    input: &wire::PolicyEvaluationRequest,
) -> Result<DecodedPolicyRequest, PolicyWireError> {
    validate_operation(runtime, input)?;
    let authority_subject = authority_subject(&input.authority_subject)?;
    validate_authority_context(runtime, input, &authority_subject)?;
    let grant = GrantRequest {
        context: context(input.context),
        authority_subject: authority_subject.clone(),
        action_id: if input.action_id.is_empty() {
            ActionId::new(String::new())
        } else {
            identifier(&input.action_id).map(ActionId::new)?
        },
        principal: principal(&input.principal)?,
        action_class: action_class(input.action_class),
        operation_kind: operation_kind(input.operation_kind),
        canonical_intent_digest: input.canonical_intent_digest,
        proposal_digest: digest(&input.proposal_digest)?,
        scope: scope(&input.scope)?,
        data_classes: sensitivities(&input.data_classes)?,
        context_risk: risk(input.context_risk),
        service_generation: input.operation.service_generation,
        idempotency_key: GrantIdempotencyKey::new(input.operation.idempotency_key.clone())
            .map_err(|_| PolicyWireError::InvalidOperation)?,
        expires_at: MonotonicMillis(input.expires_at_monotonic_ms),
        now_utc_ms: input.now_utc_ms,
        actor_lease: actor_lease(&input.actor_lease)?,
        approval: input.approval.as_ref().map(approval).transpose()?,
        discovery: input.discovery.as_ref().map(discovery).transpose()?,
    };
    if input.expires_at_monotonic_ms <= input.now_monotonic_ms
        || input.expires_at_monotonic_ms > input.operation.deadline_monotonic_ms
    {
        return Err(PolicyWireError::InvalidLifetime);
    }
    Ok(DecodedPolicyRequest {
        grant,
        now_millis: input.now_monotonic_ms,
    })
}

fn validate_operation(
    runtime: &BoxedCoreRuntime,
    input: &wire::PolicyEvaluationRequest,
) -> Result<(), PolicyWireError> {
    OperationId::new(input.operation.operation_id.clone())
        .map_err(|_| PolicyWireError::InvalidOperation)?;
    if input.operation.service_generation == 0
        || ServiceGeneration::new(input.operation.service_generation)
            != runtime.service_generation()
    {
        return Err(PolicyWireError::StaleGeneration);
    }
    if Deadline::from_millis(input.operation.deadline_monotonic_ms)
        .is_expired_at(input.now_monotonic_ms)
    {
        return Err(PolicyWireError::DeadlineExceeded);
    }
    match input.context {
        wire::PolicyEvaluationContext::Task | wire::PolicyEvaluationContext::TaskDiscovery => {
            let task_id = identifier(&input.task_id).map(TaskId::new)?;
            let task = runtime.task(&task_id).ok_or(PolicyWireError::UnknownTask)?;
            if input.operation.task_revision != task.revision() {
                return Err(PolicyWireError::StaleRevision);
            }
            let frozen = runtime
                .task_policy_version(&task_id)
                .ok_or(PolicyWireError::UnknownTask)?;
            if input.policy_version == 0
                || input.policy_version != frozen.0
                || input.policy_version != runtime.policy_version().0
            {
                return Err(PolicyWireError::InvalidPolicyVersion);
            }
        }
        wire::PolicyEvaluationContext::DirectUserObservation => {
            if input.operation.task_revision != 0
                || input.policy_version == 0
                || input.policy_version != runtime.policy_version().0
                || input
                    .operation
                    .deadline_monotonic_ms
                    .saturating_sub(input.now_monotonic_ms)
                    > wire::MAX_DIRECT_OBSERVATION_DEADLINE_MS as u64
            {
                return Err(PolicyWireError::InvalidAuthorityContext);
            }
        }
    }
    Ok(())
}
