// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Action proposal, policy decision, and terminal outcome projections.

use bip_types::identity::{
    ContentDigest, DigestAlgorithm, DispatchId, FrameId, MonotonicMillis, PageEpoch,
    SemanticNodeId, TabId,
};
use bip_types::Sensitivity;
use core_service_types as wire;
use task_engine::action::ActionIntent;
use task_engine::{
    ActionOutcome, ActionProposal, Authorization, BudgetDraw, Denial, IdempotencyKey,
    ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence, PlanStepId,
    ProposalDecision,
};

use super::consent::{source as consented_source, unsource as unconsented_source};
use super::enum_action::{
    action_class, idempotency, result_code, unaction_class, unidempotency, unresult_code,
};
use super::enum_task::{budget, unbudget};
use super::value::{valid_digest, valid_name};
use super::ConversionError;

const _: () = assert!(
    wire::MAX_CANONICAL_ACTION_INTENT_BYTES
        == task_engine::action::MAX_CANONICAL_ACTION_INTENT_BYTES
);

pub(super) fn proposal(
    value: &ActionProposal,
) -> Result<wire::PersistedActionProposal, ConversionError> {
    let canonical_intent = value
        .intent()
        .encode_canonical()
        .map_err(|_| ConversionError::InvalidValue)?;
    Ok(wire::PersistedActionProposal {
        tool_name: value.tool_name().to_owned(),
        plan_step_id: value.plan_step_id.as_ref().map(|id| id.as_str().to_owned()),
        action_class: action_class(value.action_class()),
        tab_id: value.tab_id().as_str().to_owned(),
        node_id: value.node_id().map(|id| id.as_str().to_owned()),
        destination_address: value.destination_address().map(str::to_owned),
        idempotency_key: value.idempotency_key.as_str().to_owned(),
        idempotency: idempotency(value.idempotency()),
        required_for_step: value.required_for_step,
        budget_draw: value.budget_draw.map(|draw| wire::PersistedBudgetDraw {
            kind: budget(draw.kind),
            amount: draw.amount,
        }),
        proposal_digest: value.proposal_digest.value.clone(),
        canonical_intent,
    })
}

pub(super) fn unproposal(
    value: wire::PersistedActionProposal,
) -> Result<ActionProposal, ConversionError> {
    if !valid_name(&value.tool_name)
        || value.idempotency_key.is_empty()
        || !valid_digest(&value.proposal_digest)
    {
        return Err(ConversionError::InvalidValue);
    }
    let intent = ActionIntent::decode_canonical(&value.canonical_intent)
        .map_err(|_| ConversionError::InvalidValue)?;
    let projected_action_class = unaction_class(value.action_class);
    let projected_idempotency = unidempotency(value.idempotency);
    if value.tool_name != intent.tool_name()
        || projected_action_class != intent.action_class()
        || value.tab_id != intent.tab_id().as_str()
        || value.node_id.as_deref() != intent.node_id().map(SemanticNodeId::as_str)
        || value.destination_address.as_deref() != intent.destination_address()
        || projected_idempotency != intent.idempotency()
    {
        return Err(ConversionError::InvalidValue);
    }
    if value.destination_address.as_ref().is_some_and(|address| {
        address.is_empty()
            || address.len() > wire::MAX_DESTINATION_ADDRESS_BYTES
            || address.bytes().any(|byte| byte < 0x20)
    }) {
        return Err(ConversionError::InvalidValue);
    }
    Ok(ActionProposal::new(
        intent,
        value.plan_step_id.map(PlanStepId::new),
        IdempotencyKey::new(value.idempotency_key),
        value.required_for_step,
        value.budget_draw.map(|draw| BudgetDraw {
            kind: unbudget(draw.kind),
            amount: draw.amount,
        }),
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: value.proposal_digest,
        },
    ))
}

pub(super) fn decision(value: &ProposalDecision) -> wire::PersistedProposalDecision {
    match value {
        ProposalDecision::Authorize(value) => wire::PersistedProposalDecision::Authorize {
            authorization: wire::PersistedAuthorization {
                capability_id: value.capability_id.as_str().to_owned(),
            },
        },
        ProposalDecision::RequireApproval => wire::PersistedProposalDecision::RequireApproval,
        ProposalDecision::Deny(value) => wire::PersistedProposalDecision::Deny {
            denial: wire::PersistedDenial {
                code: result_code(value.code),
            },
        },
    }
}

pub(super) fn undecision(
    value: wire::PersistedProposalDecision,
) -> Result<ProposalDecision, ConversionError> {
    Ok(match value {
        wire::PersistedProposalDecision::Authorize { authorization } => {
            if authorization.capability_id.is_empty() {
                return Err(ConversionError::InvalidIdentifier);
            }
            ProposalDecision::Authorize(Authorization {
                capability_id: task_engine::CapabilityId::new(authorization.capability_id),
            })
        }
        wire::PersistedProposalDecision::RequireApproval => ProposalDecision::RequireApproval,
        wire::PersistedProposalDecision::Deny { denial } => {
            ProposalDecision::Deny(Denial::new(unresult_code(denial.code)))
        }
    })
}

pub(super) fn outcome(value: &ActionOutcome) -> wire::PersistedActionOutcome {
    wire::PersistedActionOutcome {
        code: result_code(value.code),
        dispatch_id: value.dispatch_id.as_ref().map(|id| id.0.clone()),
        observed_at_monotonic_ms: value.observed_at.0,
        observation: value.observation.as_ref().map(observation),
        discovered_source: value.discovered_source.as_ref().map(consented_source),
    }
}

pub(super) fn unoutcome(
    value: wire::PersistedActionOutcome,
) -> Result<ActionOutcome, ConversionError> {
    Ok(ActionOutcome {
        code: unresult_code(value.code),
        dispatch_id: value.dispatch_id.map(DispatchId),
        observed_at: MonotonicMillis(value.observed_at_monotonic_ms),
        observation: value.observation.map(unobservation).transpose()?,
        discovered_source: value
            .discovered_source
            .map(unconsented_source)
            .transpose()?,
    })
}

fn observation(value: &PageObservationEvidence) -> wire::PersistedPageObservationEvidence {
    wire::PersistedPageObservationEvidence {
        service_generation: value.service_generation,
        schema_version: value.schema_version.clone(),
        tab_id: value.tab_id.0.clone(),
        frame_id: value.frame_id.0.clone(),
        page_epoch: value.page_epoch.0.clone(),
        graph_revision: value.graph_revision,
        normalized_origin: value.normalized_origin.clone(),
        private_profile: value.private_profile,
        completeness: match value.completeness {
            ObservationCompleteness::Complete => wire::PersistedObservationCompleteness::Complete,
            ObservationCompleteness::Incomplete => {
                wire::PersistedObservationCompleteness::Incomplete
            }
        },
        graph: wire::PersistedObservationGraphSummary {
            node_count: value.graph.node_count,
            relationship_count: value.graph.relationship_count,
            named_node_count: value.graph.named_node_count,
            text_run_count: value.graph.text_run_count,
            text_byte_count: value.graph.text_byte_count,
        },
        total_bytes: value.total_bytes,
        truncated: value.truncated,
        may_change_answer: value.may_change_answer,
        redacted_field_count: value.redacted_field_count,
        suppressed_secret_value_count: value.suppressed_secret_value_count,
        sensitive_zone_count: value.sensitive_zone_count,
        policy_filtered_frame_count: value.policy_filtered_frame_count,
        highest_sensitivity: sensitivity(value.highest_sensitivity),
    }
}

fn unobservation(
    value: wire::PersistedPageObservationEvidence,
) -> Result<PageObservationEvidence, ConversionError> {
    let evidence = PageObservationEvidence {
        service_generation: value.service_generation,
        schema_version: value.schema_version,
        tab_id: TabId(value.tab_id),
        frame_id: FrameId(value.frame_id),
        page_epoch: PageEpoch(value.page_epoch),
        graph_revision: value.graph_revision,
        normalized_origin: value.normalized_origin,
        private_profile: value.private_profile,
        completeness: match value.completeness {
            wire::PersistedObservationCompleteness::Complete => ObservationCompleteness::Complete,
            wire::PersistedObservationCompleteness::Incomplete => {
                ObservationCompleteness::Incomplete
            }
        },
        graph: ObservationGraphSummary {
            node_count: value.graph.node_count,
            relationship_count: value.graph.relationship_count,
            named_node_count: value.graph.named_node_count,
            text_run_count: value.graph.text_run_count,
            text_byte_count: value.graph.text_byte_count,
        },
        total_bytes: value.total_bytes,
        truncated: value.truncated,
        may_change_answer: value.may_change_answer,
        redacted_field_count: value.redacted_field_count,
        suppressed_secret_value_count: value.suppressed_secret_value_count,
        sensitive_zone_count: value.sensitive_zone_count,
        policy_filtered_frame_count: value.policy_filtered_frame_count,
        highest_sensitivity: unsensitivity(value.highest_sensitivity),
    };
    if evidence.is_valid() {
        Ok(evidence)
    } else {
        Err(ConversionError::InvalidValue)
    }
}

const fn sensitivity(value: Sensitivity) -> wire::PersistedSensitivity {
    match value {
        Sensitivity::NotSensitive => wire::PersistedSensitivity::NotSensitive,
        Sensitivity::Personal => wire::PersistedSensitivity::Personal,
        Sensitivity::Account => wire::PersistedSensitivity::Account,
        Sensitivity::Payment => wire::PersistedSensitivity::Payment,
        Sensitivity::Identity => wire::PersistedSensitivity::Identity,
        Sensitivity::Health => wire::PersistedSensitivity::Health,
        Sensitivity::Financial => wire::PersistedSensitivity::Financial,
        Sensitivity::Legal => wire::PersistedSensitivity::Legal,
        Sensitivity::PrivateCommunication => wire::PersistedSensitivity::PrivateCommunication,
        Sensitivity::Administration => wire::PersistedSensitivity::Administration,
        Sensitivity::Credential => wire::PersistedSensitivity::Credential,
        // The frozen durable enum predates challenge subtypes. Both are
        // never-extract classes, so the conservative durable projection is
        // the existing credential-withheld class, never a public class.
        Sensitivity::OneTimeCode | Sensitivity::ChallengeResponse => {
            wire::PersistedSensitivity::Credential
        }
        Sensitivity::UnknownSensitive => wire::PersistedSensitivity::UnknownSensitive,
    }
}

const fn unsensitivity(value: wire::PersistedSensitivity) -> Sensitivity {
    match value {
        wire::PersistedSensitivity::NotSensitive => Sensitivity::NotSensitive,
        wire::PersistedSensitivity::Personal => Sensitivity::Personal,
        wire::PersistedSensitivity::Account => Sensitivity::Account,
        wire::PersistedSensitivity::Payment => Sensitivity::Payment,
        wire::PersistedSensitivity::Identity => Sensitivity::Identity,
        wire::PersistedSensitivity::Health => Sensitivity::Health,
        wire::PersistedSensitivity::Financial => Sensitivity::Financial,
        wire::PersistedSensitivity::Legal => Sensitivity::Legal,
        wire::PersistedSensitivity::PrivateCommunication => Sensitivity::PrivateCommunication,
        wire::PersistedSensitivity::Administration => Sensitivity::Administration,
        wire::PersistedSensitivity::Credential => Sensitivity::Credential,
        wire::PersistedSensitivity::UnknownSensitive => Sensitivity::UnknownSensitive,
    }
}

#[cfg(test)]
mod tests {
    use bip_types::identity::{ContentDigest, DigestAlgorithm, SemanticNodeId, TabId};
    use task_engine::action::{ActionIntent, BrowserIntent};
    use task_engine::{ActionProposal, IdempotencyKey};

    use super::{proposal, unproposal, ConversionError};
    use core_service_types::{PersistedActionClass, PersistedIdempotencyClass};

    fn digest() -> ContentDigest {
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "11".repeat(32),
        }
    }

    fn navigate() -> ActionProposal {
        ActionProposal::new(
            ActionIntent::Browser(BrowserIntent::Navigate {
                tab: TabId::new("tab-1"),
                address: "https://example.test/path".to_owned(),
                new_tab: false,
            }),
            None,
            IdempotencyKey::new("navigate-1"),
            true,
            None,
            digest(),
        )
    }

    fn click() -> ActionProposal {
        ActionProposal::new(
            ActionIntent::Browser(BrowserIntent::DomClick {
                target: task_engine::action::ObservedNodeHandle::from_node_handle(
                    &bip_types::identity::NodeHandle::new(
                        TabId::new("tab-1"),
                        bip_types::identity::FrameId::new("frame-1"),
                        bip_types::identity::PageEpoch::new("epoch-1"),
                        bip_types::identity::GraphRevision(1),
                        SemanticNodeId::new("node-1"),
                        bip_types::identity::Origin {
                            kind: bip_types::identity::OriginKind::Tuple,
                            serialization: Some("https://example.test".to_owned()),
                            opaque_id: None,
                        },
                    ),
                )
                .unwrap_or_else(|| unreachable!()),
                expected_state: Some(task_engine::action::DisclosureState::Expanded),
            }),
            None,
            IdempotencyKey::new("click-1"),
            true,
            None,
            digest(),
        )
    }

    #[test]
    fn canonical_intent_round_trips_with_its_frozen_projections() {
        let value = navigate();
        let encoded = proposal(&value).unwrap_or_else(|_| unreachable!());
        assert!(!encoded.canonical_intent.is_empty());
        assert_eq!(unproposal(encoded), Ok(value));
    }

    #[test]
    fn malformed_canonical_intent_is_refused() {
        let mut encoded = proposal(&navigate()).unwrap_or_else(|_| unreachable!());
        encoded.canonical_intent.clear();
        assert_eq!(unproposal(encoded), Err(ConversionError::InvalidValue));
    }

    #[test]
    fn every_legacy_projection_must_equal_the_canonical_intent() {
        let baseline = proposal(&navigate()).unwrap_or_else(|_| unreachable!());

        let mut tool = baseline.clone();
        tool.tool_name = "browser.dom.read".to_owned();
        assert_eq!(unproposal(tool), Err(ConversionError::InvalidValue));

        let mut class = baseline.clone();
        class.action_class = PersistedActionClass::ObservePage;
        assert_eq!(unproposal(class), Err(ConversionError::InvalidValue));

        let mut tab = baseline.clone();
        tab.tab_id = "tab-2".to_owned();
        assert_eq!(unproposal(tab), Err(ConversionError::InvalidValue));

        let mut destination = baseline.clone();
        destination.destination_address = Some("https://example.test/other".to_owned());
        assert_eq!(unproposal(destination), Err(ConversionError::InvalidValue));

        let mut recovery = baseline;
        recovery.idempotency = PersistedIdempotencyClass::PureRead;
        assert_eq!(unproposal(recovery), Err(ConversionError::InvalidValue));

        let mut node = proposal(&click()).unwrap_or_else(|_| unreachable!());
        node.node_id = Some("node-2".to_owned());
        assert_eq!(unproposal(node), Err(ConversionError::InvalidValue));
    }

    #[test]
    fn frozen_v14_payload_exercises_a_domain_valid_canonical_proposal() {
        let text = include_str!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../../../../contracts/core-service/golden/task-transaction-v14.hex"
        ));
        let bytes = text
            .trim()
            .as_bytes()
            .chunks_exact(2)
            .map(|pair| {
                core::str::from_utf8(pair)
                    .ok()
                    .and_then(|value| u8::from_str_radix(value, 16).ok())
                    .unwrap_or_default()
            })
            .collect::<Vec<_>>();
        let batch =
            core_service_types::decode_transaction_batch(&bytes).unwrap_or_else(|_| unreachable!());
        let persisted = batch
            .journal_entries
            .into_iter()
            .find_map(|entry| match entry {
                core_service_types::PersistedJournalEntry::Command { record } => {
                    match record.envelope.command {
                        core_service_types::PersistedCommand::ProposeAction { proposal } => {
                            Some(proposal)
                        }
                        _ => None,
                    }
                }
                core_service_types::PersistedJournalEntry::Event { .. } => None,
            });
        let restored = persisted
            .ok_or(ConversionError::InvalidValue)
            .and_then(unproposal);
        assert!(restored.is_ok());
    }
}
