// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fail-closed conversion from the generated Core Service start command.

mod refresh;

use std::collections::BTreeSet;

use bip_types::identity::{
    ApprovalReceiptReference, MonotonicMillis, ProfileId, SkillVersionId, TabId, TaskId,
};
use core_service_types as wire;
use task_engine::{
    BrowserSessionId, BudgetKind, BuiltinSkillReference, ConsentedSource, ControlMode,
    IdempotencyKey, Milestone, PolicyVersion, ProviderRouteId, ScopePreview, SourceId, SourceScope,
    TaskBudgets, TaskKind, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId, UtcMillis, WorkspaceId,
    REVIEWED_NO_MODEL_ROUTE_ID,
};

use crate::contract::{MAX_IDEMPOTENCY_KEY_BYTES, MAX_OPERATION_ID_BYTES};
use crate::ports::{InitialConsentAdmission, TaskEngineLoad, TaskIdEntropy};
use crate::product_capabilities::{TaskStartAdmission, ACTIVE};

use self::refresh::{
    decode as decode_library_refresh, validate_start as validate_library_refresh_start,
};
use super::start_shape::{admits_generated_start, validate_start_workflow, StartTaskDecodeError};

/// Maximum UTF-8 bytes in a task goal at the utility boundary.
pub const MAX_TASK_GOAL_BYTES: usize = 65_536;
/// Maximum reviewed tool names frozen into one task seed.
pub const MAX_TASK_TOOL_ALLOWLIST: usize = 128;
/// Maximum UTF-8 bytes in one tool name.
pub const MAX_TASK_TOOL_NAME_BYTES: usize = 128;

/// Converts the only generated start-task shape into canonical reducer input.
pub fn decode_start_task(
    command: &wire::StartTaskCommand,
    operation: &wire::OperationEnvelope,
) -> Result<TaskEngineLoad, StartTaskDecodeError> {
    match ACTIVE.task_start_admission(decode_milestone(command.milestone)) {
        TaskStartAdmission::Admit => {}
        TaskStartAdmission::Disabled => {
            return Err(StartTaskDecodeError::CapabilityProfileDisabled)
        }
        TaskStartAdmission::MilestoneMismatch => {
            return Err(StartTaskDecodeError::CapabilityProfileMismatch)
        }
    }
    validate_start_identity(command, operation)?;
    let workspace_id = command
        .workspace_id
        .as_deref()
        .map(WorkspaceId::parse)
        .transpose()
        .map_err(|_| StartTaskDecodeError::InvalidWorkspaceId)?;
    let template_id = match command.template_id {
        wire::TaskTemplateId::CompareProducts => TaskTemplateId::CompareProducts,
        wire::TaskTemplateId::SummarizeEvidence => TaskTemplateId::SummarizeEvidence,
        wire::TaskTemplateId::BuildSourceTable => TaskTemplateId::BuildSourceTable,
        wire::TaskTemplateId::WebErrand => TaskTemplateId::WebErrand,
    };
    let provider_route = decode_provider_route(command.consent_preview.provider_route)?;
    if !admits_generated_start(template_id, command, provider_route.as_ref()) {
        return Err(StartTaskDecodeError::InvalidProviderRoute);
    }
    let deadline = decode_task_deadline(command)?;
    let tools = decode_tools(&command.tool_allowlist)?;
    let budgets = decode_budgets(&command.budgets)?;
    let sources = decode_consent_sources(&command.consent_preview.sources)?;
    validate_library_refresh_start(command, template_id, &sources)?;
    validate_start_workflow(command, template_id, &tools, &sources)?;
    validate_consent_budget(command, sources.len())?;
    let preview = consent_preview(command, &sources, provider_route.clone(), budgets.clone());
    let seed = start_seed(
        command,
        workspace_id,
        template_id,
        tools,
        provider_route,
        budgets,
        deadline,
    )?;
    let entropy = TaskIdEntropy::new(command.task_id_seed)
        .map_err(|_| StartTaskDecodeError::InvalidEntropy)?;
    let start_key = derived_identity(&operation.idempotency_key, "initial-start")?;
    let consent_key = derived_identity(&operation.idempotency_key, "initial-consent")?;
    let start_trace = derived_identity(&command.trace_id, "initial-start")?;
    let consent_trace = derived_identity(&command.trace_id, "initial-consent")?;
    Ok(TaskEngineLoad::fresh(
        seed,
        IdempotencyKey::new(operation.idempotency_key.clone()),
        TraceId::new(command.trace_id.clone()),
        entropy,
        InitialConsentAdmission {
            preview,
            receipt: ApprovalReceiptReference::new(command.initial_consent_receipt_id.clone()),
            start_key: IdempotencyKey::new(start_key),
            start_trace: TraceId::new(start_trace),
            consent_key: IdempotencyKey::new(consent_key),
            consent_trace: TraceId::new(consent_trace),
        },
    ))
}

fn validate_start_identity(
    command: &wire::StartTaskCommand,
    operation: &wire::OperationEnvelope,
) -> Result<(), StartTaskDecodeError> {
    validate_identity(&command.task_id, MAX_OPERATION_ID_BYTES)?;
    validate_identity(&command.browser_profile_id, MAX_OPERATION_ID_BYTES)?;
    validate_identity(
        &command.browser_session_id,
        task_engine::MAX_BROWSER_SESSION_ID_BYTES,
    )?;
    validate_identity(&command.trace_id, MAX_OPERATION_ID_BYTES)?;
    validate_identity(
        &command.initial_consent_receipt_id,
        wire::MAX_IDENTIFIER_BYTES,
    )
    .map_err(|_| StartTaskDecodeError::InvalidConsentReceipt)?;
    validate_identity(&operation.idempotency_key, MAX_IDEMPOTENCY_KEY_BYTES)?;
    if operation.task_revision != 0 {
        return Err(StartTaskDecodeError::InvalidCreationRevision);
    }
    if command.goal.len() > MAX_TASK_GOAL_BYTES {
        return Err(StartTaskDecodeError::GoalTooLarge);
    }
    if command.skill_version_id.is_some() && command.builtin_skill.is_some()
        || command
            .builtin_skill
            .as_ref()
            .is_some_and(|reference| reference.version == 0)
    {
        return Err(StartTaskDecodeError::InvalidBuiltinSkillBinding);
    }
    Ok(())
}

fn decode_task_deadline(
    command: &wire::StartTaskCommand,
) -> Result<Option<(u64, u64)>, StartTaskDecodeError> {
    match (
        command.has_task_deadline,
        command.task_deadline_monotonic_ms,
        command.task_deadline_utc_ms,
    ) {
        (false, 0, 0) => Ok(None),
        (true, monotonic, utc) if monotonic > 0 && utc > 0 => Ok(Some((monotonic, utc))),
        _ => Err(StartTaskDecodeError::InvalidDeadline),
    }
}

fn validate_consent_budget(
    command: &wire::StartTaskCommand,
    source_count: usize,
) -> Result<(), StartTaskDecodeError> {
    let maximum_new_source_cap = u32::try_from(wire::MAX_NEW_SOURCE_CAP)
        .map_err(|_| StartTaskDecodeError::ConsentBudgetMismatch)?;
    let expected_sources = u64::try_from(source_count)
        .ok()
        .and_then(|count| count.checked_add(u64::from(command.consent_preview.new_source_cap)))
        .ok_or(StartTaskDecodeError::ConsentBudgetMismatch)?;
    let stated_max_sources = command
        .budgets
        .iter()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxSources)
        .map(|budget| budget.limit);
    if stated_max_sources != Some(expected_sources)
        || command.consent_preview.new_source_cap > maximum_new_source_cap
        || (!command.consent_preview.source_discovery_enabled
            && command.consent_preview.new_source_cap != 0)
    {
        return Err(StartTaskDecodeError::ConsentBudgetMismatch);
    }
    Ok(())
}

fn consent_preview(
    command: &wire::StartTaskCommand,
    sources: &[ConsentedSource],
    provider_route: Option<ProviderRouteId>,
    budgets: TaskBudgets,
) -> ScopePreview {
    let mut scope = SourceScope::new();
    for source in sources {
        scope = scope.include(source.source_id);
    }
    ScopePreview {
        scope,
        sources: sources.to_vec(),
        source_discovery_enabled: command.consent_preview.source_discovery_enabled,
        new_source_cap: command.consent_preview.new_source_cap,
        provider_route,
        budgets,
    }
}

fn start_seed(
    command: &wire::StartTaskCommand,
    workspace_id: Option<WorkspaceId>,
    template_id: TaskTemplateId,
    tools: Vec<String>,
    provider_route: Option<ProviderRouteId>,
    budgets: TaskBudgets,
    deadline: Option<(u64, u64)>,
) -> Result<TaskSeed, StartTaskDecodeError> {
    Ok(TaskSeed {
        task_id: TaskId::new(command.task_id.clone()),
        workspace_id,
        browser_profile_id: ProfileId::new(command.browser_profile_id.clone()),
        kind: match command.kind {
            wire::TaskKind::Research => TaskKind::Research,
            wire::TaskKind::Errand => TaskKind::Errand,
        },
        user_goal: command.goal.clone(),
        control_mode: match command.control_mode {
            wire::TaskControlMode::User => ControlMode::User,
            wire::TaskControlMode::Shared => ControlMode::Shared,
            wire::TaskControlMode::Assistant => ControlMode::Assistant,
        },
        snapshot: TaskSnapshot {
            template_id,
            assistant_config_version: command.assistant_config_version,
            skill_version_id: command.skill_version_id.clone().map(SkillVersionId),
            builtin_skill: command
                .builtin_skill
                .as_ref()
                .map(|reference| BuiltinSkillReference {
                    skill_id: crate::builtin_skills::builtin_id_from_wire(reference.skill_id),
                    version: reference.version,
                }),
            tool_allowlist: tools,
            capability_policy_version: PolicyVersion(command.policy_version),
            provider_route,
            consented_sources: Vec::new(),
            source_discovery_enabled: false,
            remaining_new_source_cap: 0,
            discovery_tab_id: None,
            library_refresh: command
                .library_refresh
                .as_ref()
                .map(decode_library_refresh)
                .transpose()?,
            browser_session_id: BrowserSessionId::new(command.browser_session_id.clone())
                .map_err(|_| StartTaskDecodeError::IdentityTooLong)?,
            milestone: decode_milestone(command.milestone),
        },
        budgets,
        deadline: deadline.map(|(monotonic, _)| MonotonicMillis(monotonic)),
        deadline_utc: deadline.map(|(_, utc)| UtcMillis(utc)),
        predecessor_task_id: command.predecessor_task_id.clone().map(TaskId::new),
    })
}

fn derived_identity(value: &str, suffix: &str) -> Result<String, StartTaskDecodeError> {
    let derived = format!("{value}:{suffix}");
    validate_identity(&derived, wire::MAX_IDENTIFIER_BYTES)?;
    Ok(derived)
}

fn decode_provider_route(
    value: wire::TaskProviderRoute,
) -> Result<Option<ProviderRouteId>, StartTaskDecodeError> {
    let label = match value {
        wire::TaskProviderRoute::NotConfigured => {
            return Err(StartTaskDecodeError::ProviderNotConfigured);
        }
        wire::TaskProviderRoute::DirectUserKey => "direct_user_key",
        wire::TaskProviderRoute::ManagedService => "managed_service",
        wire::TaskProviderRoute::NoModelRequired => REVIEWED_NO_MODEL_ROUTE_ID,
    };
    ProviderRouteId::new(label)
        .map(Some)
        .map_err(|_| StartTaskDecodeError::InvalidProviderRoute)
}

fn decode_consent_sources(
    values: &[wire::TaskConsentSource],
) -> Result<Vec<ConsentedSource>, StartTaskDecodeError> {
    if values.len() > wire::MAX_TASK_CONSENT_SOURCES {
        return Err(StartTaskDecodeError::InvalidConsentPreview);
    }
    let mut sources = Vec::with_capacity(values.len());
    for value in values {
        validate_identity(&value.tab_id, wire::MAX_IDENTIFIER_BYTES)
            .map_err(|_| StartTaskDecodeError::InvalidConsentPreview)?;
        if value.normalized_origin.len() > wire::MAX_NORMALIZED_ORIGIN_BYTES {
            return Err(StartTaskDecodeError::InvalidConsentPreview);
        }
        let source_id = SourceId::parse(&value.source_id)
            .map_err(|_| StartTaskDecodeError::InvalidConsentPreview)?;
        if sources
            .last()
            .is_some_and(|previous: &ConsentedSource| previous.source_id >= source_id)
        {
            return Err(StartTaskDecodeError::InvalidConsentPreview);
        }
        let origin = policy_engine::origin::normalize_serialization(&value.normalized_origin)
            .map_err(|_| StartTaskDecodeError::InvalidConsentPreview)?;
        if origin.display() != value.normalized_origin {
            return Err(StartTaskDecodeError::InvalidConsentPreview);
        }
        if value.canonical_locator.as_deref().is_some_and(|locator| {
            let Some(host) = origin.host() else {
                return true;
            };
            locator.len() > wire::MAX_SOURCE_LOCATOR_BYTES
                || locator.contains(['?', '#', '@'])
                || locator
                    .strip_prefix("https://")
                    .or_else(|| locator.strip_prefix("http://"))
                    .is_none_or(|rest| {
                        let authority = rest.split('/').next().unwrap_or_default();
                        authority != host
                            && authority.strip_prefix(host).is_none_or(|suffix| {
                                suffix.strip_prefix(':').is_none_or(|port| {
                                    port.is_empty()
                                        || !port.bytes().all(|byte| byte.is_ascii_digit())
                                })
                            })
                    })
        }) {
            return Err(StartTaskDecodeError::InvalidConsentPreview);
        }
        sources.push(ConsentedSource {
            source_id,
            tab_id: TabId::new(value.tab_id.clone()),
            normalized_origin: value.normalized_origin.clone(),
            canonical_locator: value.canonical_locator.clone(),
        });
    }
    Ok(sources)
}

fn validate_identity(value: &str, max_bytes: usize) -> Result<(), StartTaskDecodeError> {
    if value.is_empty() {
        return Err(StartTaskDecodeError::EmptyIdentity);
    }
    if value.len() > max_bytes {
        return Err(StartTaskDecodeError::IdentityTooLong);
    }
    Ok(())
}

fn decode_tools(values: &[String]) -> Result<Vec<String>, StartTaskDecodeError> {
    if values.len() > MAX_TASK_TOOL_ALLOWLIST {
        return Err(StartTaskDecodeError::TooManyTools);
    }
    let mut seen = BTreeSet::new();
    for value in values {
        if value.is_empty()
            || value.len() > MAX_TASK_TOOL_NAME_BYTES
            || !value
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
            || !seen.insert(value)
        {
            return Err(StartTaskDecodeError::InvalidToolName);
        }
    }
    Ok(values.to_vec())
}

fn decode_budgets(values: &[wire::TaskBudget]) -> Result<TaskBudgets, StartTaskDecodeError> {
    let mut budgets = TaskBudgets::none();
    let mut seen = BTreeSet::new();
    for budget in values {
        let kind = match budget.kind {
            wire::TaskBudgetKind::MaxSources => BudgetKind::MaxSources,
            wire::TaskBudgetKind::MaxWorkingTabs => BudgetKind::MaxWorkingTabs,
            wire::TaskBudgetKind::MaxWallTimeMs => BudgetKind::MaxWallTimeMillis,
            wire::TaskBudgetKind::MaxModelRequests => BudgetKind::MaxModelRequests,
            wire::TaskBudgetKind::MaxInputUnits => BudgetKind::MaxInputTokensOrBytes,
            wire::TaskBudgetKind::MaxOutputUnits => BudgetKind::MaxOutputTokensOrBytes,
            wire::TaskBudgetKind::MaxCostUnits => BudgetKind::MaxCost,
            wire::TaskBudgetKind::MaxNavigationDepth => BudgetKind::MaxNavigationDepth,
            wire::TaskBudgetKind::MaxRetriesPerStep => BudgetKind::MaxRetriesPerStep,
            wire::TaskBudgetKind::MaxArtifactBytes => BudgetKind::MaxArtifactBytes,
        };
        if !seen.insert(kind) {
            return Err(StartTaskDecodeError::DuplicateBudget);
        }
        budgets = budgets.with(kind, budget.limit);
    }
    Ok(budgets)
}

const fn decode_milestone(value: wire::TaskMilestone) -> Milestone {
    match value {
        wire::TaskMilestone::M0 => Milestone::M0,
        wire::TaskMilestone::M1 => Milestone::M1,
        wire::TaskMilestone::M2 => Milestone::M2,
        wire::TaskMilestone::M3 => Milestone::M3,
        wire::TaskMilestone::M4 => Milestone::M4,
        wire::TaskMilestone::M5 => Milestone::M5,
        wire::TaskMilestone::M6 => Milestone::M6,
        wire::TaskMilestone::M7 => Milestone::M7,
        wire::TaskMilestone::M8 => Milestone::M8,
    }
}

#[cfg(test)]
mod tests;
