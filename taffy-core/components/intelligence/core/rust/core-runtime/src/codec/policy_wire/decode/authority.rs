// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    FrameId, GraphRevision, MonotonicMillis, PageEpoch, ProfileId, SemanticNodeId, SkillVersionId,
    TabId, TaskId,
};
use core_service_types as wire;
use policy_engine::{
    ActorLeaseFact, ActorLeaseId, AllowedRedirects, AuthoritySubject, CapabilityScope, ControlMode,
    DirectUserIntentId, NormalizedOrigin, PolicyEvaluationContext, TaskDiscoveryAuthorityFact,
    MAX_TASK_DISCOVERY_SOURCE_CAP,
};

use crate::runtime::BoxedCoreRuntime;

use super::super::PolicyWireError;
use super::values::identifier;

const MAX_POLICY_REDIRECTS: usize = 32;
const _: () = assert!(
    policy_engine::MAX_TASK_DISCOVERY_SOURCE_CAP == task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP
);

pub(super) fn principal(input: &wire::PolicyPrincipal) -> Result<Principal, PolicyWireError> {
    let (kind, skill_version_id) = match (input.kind, input.skill_version_id.as_deref()) {
        (wire::PolicyPrincipalKind::Assistant, None) => (PrincipalKind::Assistant, None),
        (wire::PolicyPrincipalKind::Skill, Some(value)) => (
            PrincipalKind::Skill,
            Some(identifier(value).map(SkillVersionId::new)?),
        ),
        _ => return Err(PolicyWireError::InvalidPrincipal),
    };
    Ok(Principal {
        kind,
        skill_version_id,
    })
}

pub(super) fn scope(
    input: &wire::PolicyCapabilityScope,
) -> Result<CapabilityScope, PolicyWireError> {
    if input.allowed_redirects.len() > MAX_POLICY_REDIRECTS {
        return Err(PolicyWireError::InvalidScope);
    }
    let mut redirects = Vec::with_capacity(input.allowed_redirects.len());
    for entry in &input.allowed_redirects {
        let decoded = origin(entry)?;
        if redirects
            .last()
            .is_some_and(|previous| previous >= &decoded)
        {
            return Err(PolicyWireError::InvalidScope);
        }
        redirects.push(decoded);
    }
    let destination_address = input
        .destination_address
        .as_ref()
        .map(|value| {
            if value.is_empty()
                || value.len() > wire::MAX_DESTINATION_ADDRESS_BYTES
                || value.chars().any(char::is_control)
            {
                return Err(PolicyWireError::InvalidScope);
            }
            Ok(value.clone())
        })
        .transpose()?;
    Ok(CapabilityScope {
        profile_id: identifier(&input.profile_id).map(ProfileId::new)?,
        tab_id: identifier(&input.tab_id).map(TabId::new)?,
        frame_id: identifier(&input.frame_id).map(FrameId::new)?,
        page_epoch: identifier(&input.page_epoch).map(PageEpoch::new)?,
        origin: origin(&input.origin)?,
        node_id: input
            .node_id
            .as_deref()
            .map(|value| identifier(value).map(SemanticNodeId::new))
            .transpose()?,
        destination_scope: input.destination_scope.as_ref().map(origin).transpose()?,
        destination_address,
        required_graph_revision: GraphRevision(input.required_graph_revision),
        allowed_redirects: AllowedRedirects::from_normalized(redirects),
    })
}

fn origin(input: &wire::PolicyOrigin) -> Result<NormalizedOrigin, PolicyWireError> {
    match (
        input.kind,
        input.serialization.as_deref(),
        input.opaque_id.as_deref(),
    ) {
        (wire::PolicyOriginKind::Tuple, Some(serialization), None) => {
            identifier(serialization)?;
            let normalized = policy_engine::origin::normalize_serialization(serialization)
                .map_err(|_| PolicyWireError::InvalidScope)?;
            if normalized.display() != serialization {
                return Err(PolicyWireError::InvalidScope);
            }
            Ok(normalized)
        }
        (wire::PolicyOriginKind::Opaque, None, Some(opaque_id)) => Ok(NormalizedOrigin::Opaque {
            opaque_id: identifier(opaque_id)?,
        }),
        _ => Err(PolicyWireError::InvalidScope),
    }
}

pub(super) fn actor_lease(input: &wire::ActorLeaseFact) -> Result<ActorLeaseFact, PolicyWireError> {
    Ok(ActorLeaseFact {
        lease_id: ActorLeaseId::new(identifier(&input.lease_id)?),
        service_generation: input.service_generation,
        authority_subject: authority_subject(&input.authority_subject)?,
        profile_id: identifier(&input.profile_id).map(ProfileId::new)?,
        tab_id: identifier(&input.tab_id).map(TabId::new)?,
        control_mode: match input.control_mode {
            wire::TaskControlMode::User => ControlMode::User,
            wire::TaskControlMode::Shared => ControlMode::Shared,
            wire::TaskControlMode::Assistant => ControlMode::Assistant,
        },
        expires_at: MonotonicMillis(input.expires_at_monotonic_ms),
    })
}

pub(super) fn authority_subject(
    input: &wire::AuthoritySubject,
) -> Result<AuthoritySubject, PolicyWireError> {
    if input.authority_subject_id.is_empty()
        || input.authority_subject_id.len() > wire::MAX_AUTHORITY_SUBJECT_ID_BYTES
        || input.authority_subject_id.chars().any(char::is_control)
    {
        return Err(PolicyWireError::InvalidIdentity);
    }
    match input.kind {
        wire::AuthoritySubjectKind::Task => {
            if input.authority_subject_id.starts_with("direct-intent-") {
                return Err(PolicyWireError::InvalidAuthorityContext);
            }
            Ok(AuthoritySubject::Task(TaskId::new(
                input.authority_subject_id.clone(),
            )))
        }
        wire::AuthoritySubjectKind::DirectUserIntent => {
            DirectUserIntentId::new(input.authority_subject_id.clone())
                .map(AuthoritySubject::DirectUserIntent)
                .map_err(|_| PolicyWireError::InvalidAuthorityContext)
        }
    }
}

pub(super) fn validate_authority_context(
    runtime: &BoxedCoreRuntime,
    input: &wire::PolicyEvaluationRequest,
    subject: &AuthoritySubject,
) -> Result<(), PolicyWireError> {
    if input.actor_lease.authority_subject != input.authority_subject
        || input.actor_lease.service_generation != input.operation.service_generation
        || input.actor_lease.profile_id != input.scope.profile_id
        || input.actor_lease.tab_id != input.scope.tab_id
    {
        return Err(PolicyWireError::InvalidAuthorityContext);
    }
    match (input.context, subject) {
        (wire::PolicyEvaluationContext::Task, AuthoritySubject::Task(task_id)) => {
            if input.task_id != task_id.as_str()
                || input.actor_lease.task_id != task_id.as_str()
                || input.action_id.is_empty()
                || input.discovery.is_some()
            {
                return Err(PolicyWireError::InvalidAuthorityContext);
            }
        }
        (wire::PolicyEvaluationContext::TaskDiscovery, AuthoritySubject::Task(task_id)) => {
            validate_discovery_authority_context(runtime, input, task_id)?;
        }
        (
            wire::PolicyEvaluationContext::DirectUserObservation,
            AuthoritySubject::DirectUserIntent(_),
        ) => {
            if !input.task_id.is_empty()
                || !input.actor_lease.task_id.is_empty()
                || !input.action_id.is_empty()
                || input.discovery.is_some()
                || input.principal.kind != wire::PolicyPrincipalKind::Assistant
                || input.principal.skill_version_id.is_some()
                || input.action_class != wire::PolicyActionClass::ObservePage
                || input.context_risk != wire::PolicyRiskClass::LocalRead
                || input.data_classes.as_slice() != [wire::BipSensitivity::NotSensitive]
                || input.approval.is_some()
                || input.actor_lease.control_mode != wire::TaskControlMode::User
                || input.scope.origin.kind != wire::PolicyOriginKind::Tuple
                || input.scope.node_id.is_some()
                || input.scope.destination_scope.is_some()
                || !input.scope.allowed_redirects.is_empty()
                || input.scope.required_graph_revision != 0
                || input
                    .expires_at_monotonic_ms
                    .saturating_sub(input.now_monotonic_ms)
                    > wire::MAX_DIRECT_OBSERVATION_LEASE_MS as u64
            {
                return Err(PolicyWireError::InvalidAuthorityContext);
            }
        }
        _ => return Err(PolicyWireError::InvalidAuthorityContext),
    }
    Ok(())
}

/// A discovery context must equal the durable discovery facts the task holds,
/// name the discovery tab everywhere a tab is named, and carry the one shape
/// a discovery grant is minted in.
fn validate_discovery_authority_context(
    runtime: &BoxedCoreRuntime,
    input: &wire::PolicyEvaluationRequest,
    task_id: &TaskId,
) -> Result<(), PolicyWireError> {
    let Some(discovery) = input.discovery.as_ref() else {
        return Err(PolicyWireError::InvalidAuthorityContext);
    };
    let Some(durable) = runtime
        .task(task_id)
        .and_then(loop_kernel::ports::TaskEnginePort::discovery_authority_facts)
    else {
        return Err(PolicyWireError::InvalidAuthorityContext);
    };
    let opaque_origin = input.scope.origin.kind == wire::PolicyOriginKind::Opaque
        && input.scope.origin.serialization.is_none()
        && input.scope.origin.opaque_id.is_some();
    let tuple_destination = input
        .scope
        .destination_scope
        .as_ref()
        .is_some_and(|origin| {
            origin.kind == wire::PolicyOriginKind::Tuple
                && origin.serialization.is_some()
                && origin.opaque_id.is_none()
        });
    if input.task_id != task_id.as_str()
        || input.actor_lease.task_id != task_id.as_str()
        || input.action_id.is_empty()
        || discovery.discovery_tab_id != durable.discovery_tab_id
        || discovery.browser_session_id != durable.browser_session_id
        || discovery.remaining_new_source_cap != durable.remaining_new_source_cap
        || discovery.discovery_tab_id != input.scope.tab_id
        || discovery.discovery_tab_id != input.actor_lease.tab_id
        || discovery.remaining_new_source_cap == 0
        || discovery.remaining_new_source_cap > MAX_TASK_DISCOVERY_SOURCE_CAP
        || input.principal.kind != wire::PolicyPrincipalKind::Assistant
        || input.principal.skill_version_id.is_some()
        || input.action_class != wire::PolicyActionClass::OpenLink
        || !matches!(
            input.operation_kind,
            wire::TaskActionOperationKind::Search | wire::TaskActionOperationKind::Navigate
        )
        || input.context_risk != wire::PolicyRiskClass::ReversibleDisclosure
        || input.data_classes.as_slice() != [wire::BipSensitivity::NotSensitive]
        || input.approval.is_some()
        || input.actor_lease.control_mode != wire::TaskControlMode::Assistant
        || !opaque_origin
        || input.scope.node_id.is_some()
        || !tuple_destination
        || input.scope.destination_address.is_none()
        || !input.scope.allowed_redirects.is_empty()
        || input.scope.required_graph_revision != 0
    {
        return Err(PolicyWireError::InvalidAuthorityContext);
    }
    Ok(())
}

pub(super) const fn context(value: wire::PolicyEvaluationContext) -> PolicyEvaluationContext {
    match value {
        wire::PolicyEvaluationContext::Task => PolicyEvaluationContext::Task,
        wire::PolicyEvaluationContext::TaskDiscovery => PolicyEvaluationContext::TaskDiscovery,
        wire::PolicyEvaluationContext::DirectUserObservation => {
            PolicyEvaluationContext::DirectUserObservation
        }
    }
}

pub(super) fn discovery(
    input: &wire::TaskDiscoveryAuthorityFact,
) -> Result<TaskDiscoveryAuthorityFact, PolicyWireError> {
    if input.remaining_new_source_cap == 0
        || input.remaining_new_source_cap > MAX_TASK_DISCOVERY_SOURCE_CAP
    {
        return Err(PolicyWireError::InvalidAuthorityContext);
    }
    Ok(TaskDiscoveryAuthorityFact {
        discovery_tab_id: identifier(&input.discovery_tab_id).map(TabId::new)?,
        browser_session_id: identifier(&input.browser_session_id)?,
        remaining_new_source_cap: input.remaining_new_source_cap,
    })
}
