// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Compound reducer-value projections for the transaction codec.

use std::collections::BTreeSet;

use bip_types::identity::{
    ActionId, ApprovalReceiptReference, MonotonicMillis, ProfileId, SkillVersionId, TabId, TaskId,
};
use core_service_types as wire;
use task_engine::{
    ArtifactId, BrowserSessionId, LibraryRefreshContext, LibraryRefreshSource, PlanDraft,
    PolicyVersion, ProviderRouteId, ScopePreview, SourceId, SourceScope, StepDraft, TaskBudgets,
    TaskResult, TaskSeed, TaskSnapshot, UnmetRequirement, UtcMillis, WorkspaceId,
};

use super::consent::{
    sources as consented_sources, unsources as unconsented_sources, valid_tab_id,
};
use super::enum_task::{
    budget, control, gap, milestone, step_kind, template, unbudget, uncontrol, ungap, unmilestone,
    unstep_kind, untemplate,
};
use super::ConversionError;

mod builtin_skill;

use builtin_skill::{persisted_builtin_skill, restored_builtin_skill};

pub(super) fn artifact_id(value: String) -> Result<ArtifactId, ConversionError> {
    if value.is_empty()
        || value.len() > wire::MAX_IDENTIFIER_BYTES
        || value.bytes().any(|byte| byte < 0x20)
    {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(ArtifactId::new(value))
}

pub(super) fn seed(value: &TaskSeed) -> wire::PersistedTaskSeed {
    wire::PersistedTaskSeed {
        task_id: value.task_id.0.clone(),
        workspace_id: value.workspace_id.map(WorkspaceId::to_text),
        browser_profile_id: value.browser_profile_id.0.clone(),
        kind: match value.kind {
            task_engine::TaskKind::Research => wire::PersistedTaskKind::Research,
            task_engine::TaskKind::Errand => wire::PersistedTaskKind::Errand,
        },
        user_goal: value.user_goal.clone(),
        control_mode: control(value.control_mode),
        snapshot: snapshot(&value.snapshot),
        budgets: budgets(&value.budgets),
        deadline_monotonic_ms: value.deadline.map(|deadline| deadline.0),
        predecessor_task_id: value.predecessor_task_id.as_ref().map(|id| id.0.clone()),
        deadline_utc_ms: value.deadline_utc.map(|deadline| deadline.0),
    }
}

pub(super) fn unseed(value: wire::PersistedTaskSeed) -> Result<TaskSeed, ConversionError> {
    if value.task_id.is_empty() || value.browser_profile_id.is_empty() || value.user_goal.is_empty()
    {
        return Err(ConversionError::InvalidIdentifier);
    }
    let workspace_id = value
        .workspace_id
        .as_deref()
        .map(WorkspaceId::parse)
        .transpose()
        .map_err(|_| ConversionError::InvalidIdentifier)?;
    let predecessor_task_id = value.predecessor_task_id.map(TaskId);
    Ok(TaskSeed {
        task_id: TaskId(value.task_id),
        workspace_id,
        browser_profile_id: ProfileId(value.browser_profile_id),
        kind: match value.kind {
            wire::PersistedTaskKind::Research => task_engine::TaskKind::Research,
            wire::PersistedTaskKind::Errand => task_engine::TaskKind::Errand,
        },
        user_goal: value.user_goal,
        control_mode: uncontrol(value.control_mode),
        snapshot: unsnapshot(value.snapshot)?,
        budgets: unbudgets(value.budgets)?,
        deadline: value.deadline_monotonic_ms.map(MonotonicMillis),
        deadline_utc: value.deadline_utc_ms.map(UtcMillis),
        predecessor_task_id,
    })
}

fn snapshot(value: &TaskSnapshot) -> wire::PersistedTaskSnapshot {
    wire::PersistedTaskSnapshot {
        assistant_config_version: value.assistant_config_version,
        skill_version_id: value.skill_version_id.as_ref().map(|id| id.0.clone()),
        builtin_skill: value.builtin_skill.map(persisted_builtin_skill),
        tool_allowlist: value.tool_allowlist.clone(),
        capability_policy_version: value.capability_policy_version.0,
        provider_route: value
            .provider_route
            .as_ref()
            .map(|route| route.as_str().to_owned()),
        milestone: milestone(value.milestone),
        template_id: template(value.template_id),
        consented_sources: consented_sources(&value.consented_sources),
        source_discovery_enabled: value.source_discovery_enabled,
        browser_session_id: value.browser_session_id.as_str().to_owned(),
        remaining_new_source_cap: value.remaining_new_source_cap,
        discovery_tab_id: value
            .discovery_tab_id
            .as_ref()
            .map(|tab_id| tab_id.as_str().to_owned()),
        library_refresh: value.library_refresh.as_ref().map(refresh_context),
    }
}

fn unsnapshot(value: wire::PersistedTaskSnapshot) -> Result<TaskSnapshot, ConversionError> {
    validate_names(&value.tool_allowlist)?;
    if value.skill_version_id.is_some() && value.builtin_skill.is_some()
        || value.remaining_new_source_cap > task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP
        || value
            .discovery_tab_id
            .as_deref()
            .is_some_and(|tab_id| !valid_tab_id(tab_id))
    {
        return Err(ConversionError::InvalidValue);
    }
    let provider_route = value
        .provider_route
        .as_deref()
        .map(ProviderRouteId::new)
        .transpose()
        .map_err(|_| ConversionError::InvalidIdentifier)?;
    Ok(TaskSnapshot {
        template_id: untemplate(value.template_id),
        assistant_config_version: value.assistant_config_version,
        skill_version_id: value.skill_version_id.map(SkillVersionId),
        builtin_skill: value
            .builtin_skill
            .map(|reference| restored_builtin_skill(&reference))
            .transpose()?,
        tool_allowlist: value.tool_allowlist,
        capability_policy_version: PolicyVersion(value.capability_policy_version),
        provider_route,
        consented_sources: unconsented_sources(value.consented_sources)?,
        source_discovery_enabled: value.source_discovery_enabled,
        remaining_new_source_cap: value.remaining_new_source_cap,
        discovery_tab_id: value.discovery_tab_id.map(TabId::new),
        library_refresh: value.library_refresh.map(unrefresh_context).transpose()?,
        browser_session_id: BrowserSessionId::new(value.browser_session_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
        milestone: unmilestone(value.milestone),
    })
}

fn refresh_context(value: &LibraryRefreshContext) -> wire::PersistedLibraryRefreshContext {
    wire::PersistedLibraryRefreshContext {
        preview_id: value.preview_id.clone(),
        library_revision: value.library_revision,
        collection_id: value.collection_id.to_text(),
        source_workspace_revision: value.source_workspace_revision,
        sources: value
            .sources
            .iter()
            .map(|source| wire::PersistedLibraryRefreshSource {
                source_id: source.source_id.to_text(),
                title: source.title.clone(),
                host: source.host.clone(),
                canonical_locator: source.canonical_locator.clone(),
                original_content_digest: source.original_content_digest,
            })
            .collect(),
    }
}

fn unrefresh_context(
    value: wire::PersistedLibraryRefreshContext,
) -> Result<LibraryRefreshContext, ConversionError> {
    let context = LibraryRefreshContext {
        preview_id: value.preview_id,
        library_revision: value.library_revision,
        collection_id: WorkspaceId::parse(&value.collection_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
        source_workspace_revision: value.source_workspace_revision,
        sources: value
            .sources
            .into_iter()
            .map(|source| {
                Ok(LibraryRefreshSource {
                    source_id: SourceId::parse(&source.source_id)
                        .map_err(|_| ConversionError::InvalidIdentifier)?,
                    title: source.title,
                    host: source.host,
                    canonical_locator: source.canonical_locator,
                    original_content_digest: source.original_content_digest,
                })
            })
            .collect::<Result<Vec<_>, ConversionError>>()?,
    };
    if !context.is_well_formed() {
        return Err(ConversionError::InvalidValue);
    }
    Ok(context)
}

fn budgets(value: &TaskBudgets) -> Vec<wire::PersistedBudget> {
    value
        .stated()
        .map(|(kind, limit)| wire::PersistedBudget {
            kind: budget(kind),
            limit,
        })
        .collect()
}

fn unbudgets(values: Vec<wire::PersistedBudget>) -> Result<TaskBudgets, ConversionError> {
    let mut seen = BTreeSet::new();
    let mut result = TaskBudgets::none();
    for value in values {
        let kind = unbudget(value.kind);
        if !seen.insert(kind) {
            return Err(ConversionError::InvalidValue);
        }
        result = result.with(kind, value.limit);
    }
    Ok(result)
}

pub(super) fn scope(value: &SourceScope) -> wire::PersistedSourceScope {
    wire::PersistedSourceScope {
        included: value.included().iter().map(|id| id.to_text()).collect(),
        excluded: value.excluded().iter().map(|id| id.to_text()).collect(),
    }
}

pub(super) fn unscope(value: wire::PersistedSourceScope) -> Result<SourceScope, ConversionError> {
    let mut seen = BTreeSet::new();
    let mut result = SourceScope::new();
    for raw in value.included {
        let id = SourceId::parse(&raw).map_err(|_| ConversionError::InvalidIdentifier)?;
        if !seen.insert(id) {
            return Err(ConversionError::InvalidValue);
        }
        result = result.include(id);
    }
    for raw in value.excluded {
        let id = SourceId::parse(&raw).map_err(|_| ConversionError::InvalidIdentifier)?;
        if !seen.insert(id) {
            return Err(ConversionError::InvalidValue);
        }
        result.exclude(&id);
    }
    Ok(result)
}

pub(super) fn preview(value: &ScopePreview) -> wire::PersistedScopePreview {
    wire::PersistedScopePreview {
        scope: scope(&value.scope),
        provider_route: value
            .provider_route
            .as_ref()
            .map(|route| route.as_str().to_owned()),
        budgets: budgets(&value.budgets),
        sources: consented_sources(&value.sources),
        source_discovery_enabled: value.source_discovery_enabled,
        new_source_cap: value.new_source_cap,
    }
}

pub(super) fn unpreview(
    value: wire::PersistedScopePreview,
) -> Result<ScopePreview, ConversionError> {
    if value.new_source_cap > task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP {
        return Err(ConversionError::InvalidValue);
    }
    let scope = unscope(value.scope)?;
    let sources = unconsented_sources(value.sources)?;
    if scope.included()
        != sources
            .iter()
            .map(|source| source.source_id)
            .collect::<Vec<_>>()
    {
        return Err(ConversionError::InvalidValue);
    }
    Ok(ScopePreview {
        scope,
        sources,
        source_discovery_enabled: value.source_discovery_enabled,
        new_source_cap: value.new_source_cap,
        provider_route: value
            .provider_route
            .as_deref()
            .map(ProviderRouteId::new)
            .transpose()
            .map_err(|_| ConversionError::InvalidIdentifier)?,
        budgets: unbudgets(value.budgets)?,
    })
}

pub(super) fn plan(value: &PlanDraft) -> Result<wire::PersistedPlanDraft, ConversionError> {
    let mut steps = Vec::with_capacity(value.steps.len());
    for step in &value.steps {
        let mut dependencies = Vec::with_capacity(step.dependencies.len());
        for dependency in &step.dependencies {
            dependencies
                .push(u64::try_from(*dependency).map_err(|_| ConversionError::NumericOverflow)?);
        }
        steps.push(wire::PersistedStepDraft {
            kind: step_kind(step.kind),
            description: step.description.clone(),
            dependencies,
        });
    }
    Ok(wire::PersistedPlanDraft {
        summary: value.summary.clone(),
        steps,
    })
}

pub(super) fn unplan(value: wire::PersistedPlanDraft) -> Result<PlanDraft, ConversionError> {
    let mut steps = Vec::with_capacity(value.steps.len());
    for step in value.steps {
        let mut dependencies = Vec::with_capacity(step.dependencies.len());
        for dependency in step.dependencies {
            dependencies
                .push(usize::try_from(dependency).map_err(|_| ConversionError::NumericOverflow)?);
        }
        steps.push(StepDraft {
            kind: unstep_kind(step.kind),
            description: step.description,
            dependencies,
        });
    }
    let result = PlanDraft {
        summary: value.summary,
        steps,
    };
    result
        .validate()
        .map_err(|_| ConversionError::InvalidValue)?;
    Ok(result)
}

pub(super) fn result(value: &TaskResult) -> wire::PersistedTaskResult {
    wire::PersistedTaskResult {
        artifact_ids: value
            .artifact_ids
            .iter()
            .map(|id| id.as_str().to_owned())
            .collect(),
        unmet: value
            .unmet
            .iter()
            .map(|item| wire::PersistedUnmetRequirement {
                subject: item.subject.clone(),
                reason: gap(item.reason),
            })
            .collect(),
        fact_count: value.fact_count,
        source_count: value.source_count,
    }
}

pub(super) fn unresult(value: wire::PersistedTaskResult) -> TaskResult {
    TaskResult {
        artifact_ids: value
            .artifact_ids
            .into_iter()
            .map(ArtifactId::new)
            .collect(),
        unmet: value
            .unmet
            .into_iter()
            .map(|item| UnmetRequirement {
                subject: item.subject,
                reason: ungap(item.reason),
            })
            .collect(),
        fact_count: value.fact_count,
        source_count: value.source_count,
    }
}

pub(super) fn approval(raw: String) -> Result<ApprovalReceiptReference, ConversionError> {
    if raw.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(ApprovalReceiptReference(raw))
}

pub(super) fn action(raw: String) -> Result<ActionId, ConversionError> {
    if raw.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(ActionId(raw))
}

fn validate_names(values: &[String]) -> Result<(), ConversionError> {
    let mut seen = BTreeSet::new();
    if values
        .iter()
        .any(|value| !valid_name(value) || !seen.insert(value))
    {
        return Err(ConversionError::InvalidValue);
    }
    Ok(())
}

pub(super) fn valid_name(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 128
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
}

pub(super) fn valid_digest(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}
