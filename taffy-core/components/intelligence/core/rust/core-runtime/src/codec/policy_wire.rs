// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated Core Service policy request decoding and closed result projection.

use core_service_types as wire;
use task_engine::ProposalDecision;

use crate::contract::OperationId;
use crate::runtime::BoxedCoreRuntime;

use super::action_result_code::action_result_code_to_wire;
use super::grant_wire::MintedGrantWire;

mod decode;
mod operation;

/// Why browser-owned policy facts were refused before policy evaluation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PolicyWireError {
    InvalidOperation,
    StaleGeneration,
    DeadlineExceeded,
    UnknownTask,
    StaleRevision,
    InvalidIdentity,
    InvalidPrincipal,
    InvalidDigest,
    InvalidScope,
    InvalidDataClasses,
    InvalidLifetime,
    InvalidApproval,
    InvalidAuthorityContext,
    InvalidPolicyVersion,
}

/// Decodes, validates, evaluates, and projects one generated policy request.
///
/// The service bridge calls only this function. It never constructs a policy
/// domain type, selects a policy bundle, or decides how failures map to wire.
pub fn evaluate_policy_request(
    runtime: &mut BoxedCoreRuntime,
    request: &wire::PolicyEvaluationRequest,
) -> wire::PolicyEvaluationResult {
    evaluate_policy_request_labeled(runtime, request).0
}

/// The same evaluation, with the content-free name of the branch that refused.
///
/// Every `invalid` exit below answers one word, `INVALID_REQUEST`, which the
/// task engine stamps as `Deny(Unsupported)` against the action. Fourteen
/// decode clauses and one port error share that word, and none of them reached
/// a log, so a proposal this build cannot read and a proposal it refuses were
/// indistinguishable from the journal — the state a navigate sat in while the
/// browser side of the same question had already been named clause by clause.
pub fn evaluate_policy_request_labeled(
    runtime: &mut BoxedCoreRuntime,
    request: &wire::PolicyEvaluationRequest,
) -> (wire::PolicyEvaluationResult, &'static str) {
    let operation_id = response_operation_id(&request.operation.operation_id);
    let decoded = match decode::request(runtime, request) {
        Ok(decoded) => decoded,
        Err(error) => return (invalid(operation_id), error.label()),
    };
    let Ok(evaluation) = runtime.decide_policy(&decoded.grant, decoded.now_millis) else {
        return (invalid(operation_id), "port-rejected");
    };
    match (evaluation.task_decision, evaluation.minted_grant) {
        (ProposalDecision::Authorize(_), Some(grant)) => match grant.to_wire() {
            Ok(projected) => {
                let direct_effect = direct_observation_effect(request, &projected);
                (
                    result(
                        operation_id,
                        wire::PolicyEvaluationStatus::Granted,
                        Some(projected),
                        direct_effect,
                        None,
                    ),
                    "",
                )
            }
            Err(_) => (invalid(operation_id), "grant-projection"),
        },
        (ProposalDecision::RequireApproval, None) => (
            result(
                operation_id,
                wire::PolicyEvaluationStatus::ApprovalRequired,
                None,
                None,
                None,
            ),
            "",
        ),
        // A refusal is a decision the action is settled with, so it carries
        // the closed code the reducer records and the model is told. The
        // refusing rule's own label stays in the audit record.
        (ProposalDecision::Deny(denial), None) => (
            result(
                operation_id,
                wire::PolicyEvaluationStatus::Denied,
                None,
                None,
                Some(wire::PolicyDenial {
                    code: action_result_code_to_wire(denial.code),
                }),
            ),
            "",
        ),
        (ProposalDecision::Authorize(_), None)
        | (ProposalDecision::RequireApproval | ProposalDecision::Deny(_), Some(_)) => {
            (invalid(operation_id), "decision-shape")
        }
    }
}

impl PolicyWireError {
    /// A compiled-in name per clause. Never a value read off the request.
    const fn label(self) -> &'static str {
        match self {
            Self::InvalidOperation => "operation",
            Self::StaleGeneration => "generation",
            Self::DeadlineExceeded => "deadline",
            Self::UnknownTask => "unknown-task",
            Self::StaleRevision => "revision",
            Self::InvalidIdentity => "identity",
            Self::InvalidPrincipal => "principal",
            Self::InvalidDigest => "digest",
            Self::InvalidScope => "scope",
            Self::InvalidDataClasses => "data-classes",
            Self::InvalidLifetime => "lifetime",
            Self::InvalidApproval => "approval",
            Self::InvalidAuthorityContext => "authority-context",
            Self::InvalidPolicyVersion => "policy-version",
        }
    }
}

fn invalid(operation_id: String) -> wire::PolicyEvaluationResult {
    result(
        operation_id,
        wire::PolicyEvaluationStatus::InvalidRequest,
        None,
        None,
        None,
    )
}

fn response_operation_id(value: &str) -> String {
    OperationId::new(value.to_owned()).map_or_else(|_| String::new(), |id| id.as_str().to_owned())
}

fn result(
    operation_id: String,
    status: wire::PolicyEvaluationStatus,
    minted_grant: Option<wire::MintedCapabilityGrant>,
    direct_observation_effect: Option<wire::EffectEnvelope>,
    denial: Option<wire::PolicyDenial>,
) -> wire::PolicyEvaluationResult {
    wire::PolicyEvaluationResult {
        operation_id,
        status,
        minted_grant,
        direct_observation_effect,
        denial,
    }
}

fn direct_observation_effect(
    request: &wire::PolicyEvaluationRequest,
    grant: &wire::MintedCapabilityGrant,
) -> Option<wire::EffectEnvelope> {
    if request.context != wire::PolicyEvaluationContext::DirectUserObservation {
        return None;
    }
    Some(wire::EffectEnvelope {
        operation: request.operation.clone(),
        effect_id: request.operation.operation_id.clone(),
        kind: wire::EffectKind::PageObservation,
        retry_class: wire::RetryClass::Idempotent,
        storage_commit: None,
        page_observation: Some(wire::PageObservationEffect {
            tab_id: request.scope.tab_id.clone(),
            frame_id: request.scope.frame_id.clone(),
            page_epoch: request.scope.page_epoch.clone(),
            scope: wire::ObservationScope::CurrentDocument,
            max_bytes: u32::try_from(wire::MAX_DIRECT_OBSERVATION_TOTAL_BYTES).unwrap_or(u32::MAX),
            task_id: String::new(),
            action_id: String::new(),
            capability_id: grant.capability_id.clone(),
            proposal_digest: grant.proposal_digest.clone(),
            idempotency_key: grant.idempotency_key.clone(),
            authority_subject: grant.authority_subject.clone(),
            max_nodes: u32::try_from(wire::MAX_DIRECT_OBSERVATION_NODES).unwrap_or(u32::MAX),
            max_text_bytes: u32::try_from(wire::MAX_DIRECT_OBSERVATION_TEXT_BYTES)
                .unwrap_or(u32::MAX),
            max_frames: u32::try_from(wire::MAX_DIRECT_OBSERVATION_FRAMES).unwrap_or(u32::MAX),
            deadline_ms: u32::try_from(wire::MAX_DIRECT_OBSERVATION_DEADLINE_MS)
                .unwrap_or(u32::MAX),
            expected_graph_revision: 0,
        }),
        model_request: None,
        network_request: None,
        browser_action: None,
        tool_job: None,
        secure_store: None,
        auth_surface: None,
        permission_request: None,
        asset_delivery: None,
        catalog_fetch: None,
        provider_listing_fetch: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    })
}

pub(super) struct DecodedPolicyRequest {
    grant: policy_engine::GrantRequest,
    now_millis: u64,
}

#[cfg(test)]
mod tests;
