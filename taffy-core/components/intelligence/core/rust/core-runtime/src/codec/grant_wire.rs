// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact projection of a policy-minted grant into the Core Host contract.

use bip_types::action::PrincipalKind;
use bip_types::sensitivity::{Sensitivity, SensitivitySet};
use core_service_types as wire;
use policy_engine::{
    ActionClass, ActionOperationKind, AuthoritySubject, MintedGrant, NormalizedOrigin, RiskClass,
    TaskDiscoveryAuthorityFact,
};

/// Why a minted grant could not cross into the browser-owned ledger.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GrantWireError {
    InvalidIdentity,
    InvalidDigest,
    InvalidPrincipal,
    InvalidGeneration,
    InvalidLifetime,
    InvalidApproval,
    InvalidOperation,
    InvalidScope,
}

/// Core Service projection implemented only for policy-engine grants.
pub trait MintedGrantWire {
    /// Produces the complete generated ledger registration record.
    fn to_wire(&self) -> Result<wire::MintedCapabilityGrant, GrantWireError>;
}

impl MintedGrantWire for MintedGrant {
    fn to_wire(&self) -> Result<wire::MintedCapabilityGrant, GrantWireError> {
        validate_grant(self)?;
        Ok(wire::MintedCapabilityGrant {
            capability_id: self.capability_id.as_str().to_owned(),
            service_generation: self.service_generation,
            policy_version: self.policy_version.0,
            actor_lease_id: self.actor_lease_id.as_str().to_owned(),
            task_id: self
                .authority_subject
                .task_id()
                .map_or_else(String::new, |task_id| task_id.0.clone()),
            action_id: self.action_id.0.clone(),
            action_class: action_class(self.action_class),
            operation_kind: operation_kind(self.operation_kind),
            canonical_intent_digest: self.canonical_intent_digest,
            principal: principal(self)?,
            proposal_digest: self.proposal_digest.value.clone(),
            idempotency_key: self.idempotency_key.as_str().to_owned(),
            scope: wire::PolicyCapabilityScope {
                profile_id: self.scope.profile_id.0.clone(),
                tab_id: self.scope.tab_id.0.clone(),
                frame_id: self.scope.frame_id.0.clone(),
                page_epoch: self.scope.page_epoch.0.clone(),
                origin: origin(&self.scope.origin),
                node_id: self.scope.node_id.as_ref().map(|id| id.0.clone()),
                destination_scope: self.scope.destination_scope.as_ref().map(origin),
                destination_address: self.scope.destination_address.clone(),
                required_graph_revision: self.scope.required_graph_revision.0,
                allowed_redirects: self
                    .scope
                    .allowed_redirects
                    .origins()
                    .iter()
                    .map(origin)
                    .collect(),
            },
            data_classes: data_classes(self.data_classes),
            effective_risk: risk(self.effective_risk),
            approval: self
                .approval
                .as_ref()
                .map(|approval| wire::PolicyApprovalFact {
                    receipt_reference: approval.receipt.0.clone(),
                    proposal_digest: approval.proposal_digest.value.clone(),
                    service_generation: approval.service_generation,
                    expires_at_monotonic_ms: approval.expires_at.0,
                    expires_at_utc_ms: approval.expires_at_utc_ms,
                    browser_session_id: approval.browser_session_id.clone(),
                }),
            issued_at_monotonic_ms: self.issued_at.0,
            expires_at_monotonic_ms: self.expires_at.0,
            authority_subject: authority_subject(&self.authority_subject),
            discovery: self
                .discovery
                .as_ref()
                .map(|discovery| wire::TaskDiscoveryAuthorityFact {
                    discovery_tab_id: discovery.discovery_tab_id.as_str().to_owned(),
                    browser_session_id: discovery.browser_session_id.clone(),
                    remaining_new_source_cap: discovery.remaining_new_source_cap,
                }),
        })
    }
}

fn validate_grant(grant: &MintedGrant) -> Result<(), GrantWireError> {
    let identities = [
        grant.capability_id.as_str(),
        grant.actor_lease_id.as_str(),
        grant.idempotency_key.as_str(),
        grant.scope.profile_id.0.as_str(),
        grant.scope.tab_id.0.as_str(),
        grant.scope.frame_id.0.as_str(),
        grant.scope.page_epoch.0.as_str(),
    ];
    if identities.iter().any(|value| value.is_empty()) {
        return Err(GrantWireError::InvalidIdentity);
    }
    match &grant.authority_subject {
        AuthoritySubject::Task(task_id) => {
            if task_id.as_str().is_empty()
                || task_id.as_str().starts_with("direct-intent-")
                || grant.action_id.as_str().is_empty()
            {
                return Err(GrantWireError::InvalidIdentity);
            }
        }
        AuthoritySubject::DirectUserIntent(direct_intent_id) => {
            if direct_intent_id.as_str().is_empty()
                || !direct_intent_id.as_str().starts_with("direct-intent-")
                || !grant.action_id.as_str().is_empty()
                || grant.action_class != ActionClass::ObservePage
                || grant.operation_kind != ActionOperationKind::DomRead
                || grant.effective_risk != RiskClass::LocalRead
                || grant.approval.is_some()
                || grant.discovery.is_some()
            {
                return Err(GrantWireError::InvalidIdentity);
            }
        }
    }
    if grant.service_generation == 0 {
        return Err(GrantWireError::InvalidGeneration);
    }
    if let Some(discovery) = grant.discovery.as_ref() {
        validate_discovery_grant(grant, discovery)?;
    }
    if grant.action_class != grant.operation_kind.action_class() {
        return Err(GrantWireError::InvalidOperation);
    }
    if !grant
        .operation_kind
        .node_scope_is_valid(grant.scope.node_id.is_some())
    {
        return Err(GrantWireError::InvalidScope);
    }
    let destination_address = grant.scope.destination_address.as_deref();
    if destination_address.is_some_and(|value| {
        value.is_empty()
            || value.len() > wire::MAX_DESTINATION_ADDRESS_BYTES
            || value.chars().any(char::is_control)
    }) || (grant.operation_kind.requires_destination_address() && destination_address.is_none())
        || (!grant.operation_kind.admits_destination_address() && destination_address.is_some())
    {
        return Err(GrantWireError::InvalidScope);
    }
    if grant.issued_at.0 >= grant.expires_at.0 {
        return Err(GrantWireError::InvalidLifetime);
    }
    validate_digest(&grant.proposal_digest.value)?;
    if let Some(approval) = &grant.approval {
        validate_digest(&approval.proposal_digest.value)?;
        if approval.service_generation != grant.service_generation
            || approval.expires_at.0 < grant.expires_at.0
            || approval.proposal_digest != grant.proposal_digest
            || approval.receipt.0.is_empty()
        {
            return Err(GrantWireError::InvalidApproval);
        }
    }
    Ok(())
}

/// The one shape a discovery grant is minted in: a task's assistant principal
/// on its opaque discovery tab, a search or a navigate, a tuple destination
/// with its address, a cap inside the bound, and nothing a page could have
/// narrowed.
fn validate_discovery_grant(
    grant: &MintedGrant,
    discovery: &TaskDiscoveryAuthorityFact,
) -> Result<(), GrantWireError> {
    let opaque_origin_is_bounded = match &grant.scope.origin {
        NormalizedOrigin::Opaque { opaque_id } => valid_identity(opaque_id),
        NormalizedOrigin::Tuple { .. } => false,
    };
    if !matches!(grant.authority_subject, AuthoritySubject::Task(_))
        || grant.action_class != ActionClass::OpenLink
        || !matches!(
            grant.operation_kind,
            ActionOperationKind::Search | ActionOperationKind::Navigate
        )
        || grant.principal.kind != PrincipalKind::Assistant
        || grant.principal.skill_version_id.is_some()
        || grant.effective_risk != RiskClass::ReversibleDisclosure
        || grant.data_classes != SensitivitySet::EMPTY
        || grant.approval.is_some()
        || grant.scope.tab_id != discovery.discovery_tab_id
        || !valid_identity(discovery.discovery_tab_id.as_str())
        || !valid_identity(&discovery.browser_session_id)
        || !(1..=policy_engine::MAX_TASK_DISCOVERY_SOURCE_CAP)
            .contains(&discovery.remaining_new_source_cap)
        || grant.scope.node_id.is_some()
        || grant.scope.destination_scope.is_none()
        || grant.scope.destination_address.is_none()
        || !grant.scope.allowed_redirects.origins().is_empty()
        || grant.scope.required_graph_revision.0 != 0
        || !opaque_origin_is_bounded
    {
        return Err(GrantWireError::InvalidScope);
    }
    Ok(())
}

fn valid_identity(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= wire::MAX_IDENTIFIER_BYTES
        && !value.chars().any(char::is_control)
}

fn authority_subject(value: &AuthoritySubject) -> wire::AuthoritySubject {
    let kind = match value {
        AuthoritySubject::Task(_) => wire::AuthoritySubjectKind::Task,
        AuthoritySubject::DirectUserIntent(_) => wire::AuthoritySubjectKind::DirectUserIntent,
    };
    wire::AuthoritySubject {
        kind,
        authority_subject_id: value.as_str().to_owned(),
    }
}

fn validate_digest(value: &str) -> Result<(), GrantWireError> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err(GrantWireError::InvalidDigest);
    }
    Ok(())
}

fn principal(grant: &MintedGrant) -> Result<wire::PolicyPrincipal, GrantWireError> {
    let (kind, skill_version_id) = match (&grant.principal.kind, &grant.principal.skill_version_id)
    {
        (PrincipalKind::Assistant, None) => (wire::PolicyPrincipalKind::Assistant, None),
        (PrincipalKind::Skill, Some(id)) if !id.0.is_empty() => {
            (wire::PolicyPrincipalKind::Skill, Some(id.0.clone()))
        }
        _ => return Err(GrantWireError::InvalidPrincipal),
    };
    Ok(wire::PolicyPrincipal {
        kind,
        skill_version_id,
    })
}

const fn action_class(value: ActionClass) -> wire::PolicyActionClass {
    match value {
        ActionClass::ObservePage => wire::PolicyActionClass::ObservePage,
        ActionClass::ScrollIntoView => wire::PolicyActionClass::ScrollIntoView,
        ActionClass::OpenLink => wire::PolicyActionClass::OpenLink,
        ActionClass::CreateTaskTab => wire::PolicyActionClass::CreateTaskTab,
        ActionClass::SyntheticClick => wire::PolicyActionClass::SyntheticClick,
        ActionClass::MoveFocus => wire::PolicyActionClass::MoveFocus,
        ActionClass::FillField => wire::PolicyActionClass::FillField,
        ActionClass::SelectOption => wire::PolicyActionClass::SelectOption,
        ActionClass::ToggleControl => wire::PolicyActionClass::ToggleControl,
        ActionClass::SubmitForm => wire::PolicyActionClass::SubmitForm,
        ActionClass::StartDownload => wire::PolicyActionClass::StartDownload,
        ActionClass::UploadFile => wire::PolicyActionClass::UploadFile,
        ActionClass::SendMessage => wire::PolicyActionClass::SendMessage,
        ActionClass::Purchase => wire::PolicyActionClass::Purchase,
        ActionClass::ExtractCredential => wire::PolicyActionClass::ExtractCredential,
        ActionClass::BypassAccessControl => wire::PolicyActionClass::BypassAccessControl,
        ActionClass::ExecuteToolJob => wire::PolicyActionClass::ExecuteToolJob,
        ActionClass::LibraryRead => wire::PolicyActionClass::LibraryRead,
        ActionClass::LibraryWrite => wire::PolicyActionClass::LibraryWrite,
        ActionClass::MemoryRead => wire::PolicyActionClass::MemoryRead,
        ActionClass::MemoryWrite => wire::PolicyActionClass::MemoryWrite,
        ActionClass::ControlTab => wire::PolicyActionClass::ControlTab,
        ActionClass::ProfileStoreRead => wire::PolicyActionClass::ProfileStoreRead,
    }
}

const fn operation_kind(value: ActionOperationKind) -> wire::TaskActionOperationKind {
    match value {
        ActionOperationKind::Navigate => wire::TaskActionOperationKind::Navigate,
        ActionOperationKind::Search => wire::TaskActionOperationKind::Search,
        ActionOperationKind::HistoryBack => wire::TaskActionOperationKind::HistoryBack,
        ActionOperationKind::HistoryForward => wire::TaskActionOperationKind::HistoryForward,
        ActionOperationKind::TabsOpen => wire::TaskActionOperationKind::TabsOpen,
        ActionOperationKind::TabsList => wire::TaskActionOperationKind::TabsList,
        ActionOperationKind::TabsActivate => wire::TaskActionOperationKind::TabsActivate,
        ActionOperationKind::TabsClose => wire::TaskActionOperationKind::TabsClose,
        ActionOperationKind::DomQuery => wire::TaskActionOperationKind::DomQuery,
        ActionOperationKind::DomRead => wire::TaskActionOperationKind::DomRead,
        ActionOperationKind::DomClick => wire::TaskActionOperationKind::DomClick,
        ActionOperationKind::DomFocus => wire::TaskActionOperationKind::DomFocus,
        ActionOperationKind::DomScroll => wire::TaskActionOperationKind::DomScroll,
        ActionOperationKind::FormInspect => wire::TaskActionOperationKind::FormInspect,
        ActionOperationKind::FormFill => wire::TaskActionOperationKind::FormFill,
        ActionOperationKind::FormSelect => wire::TaskActionOperationKind::FormSelect,
        ActionOperationKind::FormToggle => wire::TaskActionOperationKind::FormToggle,
        ActionOperationKind::FormSubmit => wire::TaskActionOperationKind::FormSubmit,
        ActionOperationKind::DownloadStart => wire::TaskActionOperationKind::DownloadStart,
        ActionOperationKind::DownloadList => wire::TaskActionOperationKind::DownloadList,
        ActionOperationKind::DownloadCancel => wire::TaskActionOperationKind::DownloadCancel,
        ActionOperationKind::SelectionRead => wire::TaskActionOperationKind::SelectionRead,
        ActionOperationKind::ImageDescribe => wire::TaskActionOperationKind::ImageDescribe,
        ActionOperationKind::ImageReadText => wire::TaskActionOperationKind::ImageReadText,
        ActionOperationKind::VideoInspect => wire::TaskActionOperationKind::VideoInspect,
        ActionOperationKind::PdfInspect => wire::TaskActionOperationKind::PdfInspect,
        ActionOperationKind::PageScreenshotInspect => {
            wire::TaskActionOperationKind::PageScreenshotInspect
        }
        ActionOperationKind::LinkOpen => wire::TaskActionOperationKind::LinkOpen,
        ActionOperationKind::ToolJob => wire::TaskActionOperationKind::ToolJob,
        ActionOperationKind::LibrarySearch => wire::TaskActionOperationKind::LibrarySearch,
        ActionOperationKind::LibrarySave => wire::TaskActionOperationKind::LibrarySave,
        ActionOperationKind::LibraryRemove => wire::TaskActionOperationKind::LibraryRemove,
        ActionOperationKind::MemorySearch => wire::TaskActionOperationKind::MemorySearch,
        ActionOperationKind::MemorySave => wire::TaskActionOperationKind::MemorySave,
        ActionOperationKind::MemoryUpdate => wire::TaskActionOperationKind::MemoryUpdate,
        ActionOperationKind::MemoryDelete => wire::TaskActionOperationKind::MemoryDelete,
        ActionOperationKind::Reload => wire::TaskActionOperationKind::Reload,
        ActionOperationKind::StopLoading => wire::TaskActionOperationKind::StopLoading,
        ActionOperationKind::HistorySearch => wire::TaskActionOperationKind::HistorySearch,
        ActionOperationKind::HistoryRecent => wire::TaskActionOperationKind::HistoryRecent,
        ActionOperationKind::BookmarksSearch => wire::TaskActionOperationKind::BookmarksSearch,
        ActionOperationKind::BookmarksList => wire::TaskActionOperationKind::BookmarksList,
        ActionOperationKind::OpenTabsList => wire::TaskActionOperationKind::OpenTabsList,
    }
}

fn origin(value: &NormalizedOrigin) -> wire::PolicyOrigin {
    match value {
        NormalizedOrigin::Tuple { .. } => wire::PolicyOrigin {
            kind: wire::PolicyOriginKind::Tuple,
            serialization: Some(value.display()),
            opaque_id: None,
        },
        NormalizedOrigin::Opaque { opaque_id } => wire::PolicyOrigin {
            kind: wire::PolicyOriginKind::Opaque,
            serialization: None,
            opaque_id: Some(opaque_id.clone()),
        },
    }
}

fn data_classes(value: SensitivitySet) -> Vec<wire::BipSensitivity> {
    let members = value.members();
    if members.is_empty() {
        vec![wire::BipSensitivity::NotSensitive]
    } else {
        members.into_iter().map(sensitivity).collect()
    }
}

const fn sensitivity(value: Sensitivity) -> wire::BipSensitivity {
    match value {
        Sensitivity::NotSensitive => wire::BipSensitivity::NotSensitive,
        Sensitivity::Personal => wire::BipSensitivity::Personal,
        Sensitivity::Account => wire::BipSensitivity::Account,
        Sensitivity::Payment => wire::BipSensitivity::Payment,
        Sensitivity::Identity => wire::BipSensitivity::Identity,
        Sensitivity::Health => wire::BipSensitivity::Health,
        Sensitivity::Financial => wire::BipSensitivity::Financial,
        Sensitivity::Legal => wire::BipSensitivity::Legal,
        Sensitivity::PrivateCommunication => wire::BipSensitivity::PrivateCommunication,
        Sensitivity::Administration => wire::BipSensitivity::Administration,
        Sensitivity::Credential => wire::BipSensitivity::Credential,
        Sensitivity::UnknownSensitive => wire::BipSensitivity::UnknownSensitive,
        Sensitivity::OneTimeCode => wire::BipSensitivity::OneTimeCode,
        Sensitivity::ChallengeResponse => wire::BipSensitivity::ChallengeResponse,
    }
}

const fn risk(value: RiskClass) -> wire::PolicyRiskClass {
    match value {
        RiskClass::LocalRead => wire::PolicyRiskClass::LocalRead,
        RiskClass::ReversibleDisclosure => wire::PolicyRiskClass::ReversibleDisclosure,
        RiskClass::SensitiveDisclosure => wire::PolicyRiskClass::SensitiveDisclosure,
        RiskClass::ExcludedCommitment => wire::PolicyRiskClass::ExcludedCommitment,
        RiskClass::ProhibitedAbuse => wire::PolicyRiskClass::ProhibitedAbuse,
    }
}

#[cfg(test)]
mod tests;
