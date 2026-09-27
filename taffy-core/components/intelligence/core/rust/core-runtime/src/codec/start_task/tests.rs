// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One generated start command per workflow shape, shared by every decoder test.

mod errand;
mod local_replay;
mod model_routes;
mod reviewed;

use core_service_types as wire;

pub(super) fn operation() -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: "open-task".to_owned(),
        service_generation: 1,
        task_revision: 0,
        deadline_monotonic_ms: 1_000,
        idempotency_key: "create-task".to_owned(),
    }
}

pub(super) fn start() -> wire::StartTaskCommand {
    wire::StartTaskCommand {
        task_id: "task-a".to_owned(),
        workspace_id: None,
        browser_profile_id: "profile-a".to_owned(),
        browser_session_id: "browser-session-1".to_owned(),
        kind: wire::TaskKind::Research,
        goal: "research".to_owned(),
        control_mode: wire::TaskControlMode::Assistant,
        provider_route_id: Some("no_model_required".to_owned()),
        assistant_config_version: 1,
        policy_version: 1,
        skill_version_id: None,
        builtin_skill: None,
        tool_allowlist: vec!["browser.dom.read".to_owned()],
        milestone: wire::TaskMilestone::M7,
        budgets: reviewed_budgets(),
        has_task_deadline: false,
        task_deadline_monotonic_ms: 0,
        task_deadline_utc_ms: 0,
        predecessor_task_id: None,
        trace_id: "trace-a".to_owned(),
        task_id_seed: core::array::from_fn(|index| u8::try_from(index).unwrap_or_default()),
        template_id: wire::TaskTemplateId::BuildSourceTable,
        consent_preview: wire::TaskConsentPreview {
            sources: vec![wire::TaskConsentSource {
                source_id: "00112233445566778899aabbccddeeff".to_owned(),
                tab_id: "tab-a".to_owned(),
                normalized_origin: "https://example.test".to_owned(),
                canonical_locator: None,
            }],
            source_discovery_enabled: false,
            new_source_cap: 0,
            provider_route: wire::TaskProviderRoute::NoModelRequired,
        },
        initial_consent_receipt_id: "initial-consent-a".to_owned(),
        library_refresh: None,
    }
}

pub(super) fn reviewed_budgets() -> Vec<wire::TaskBudget> {
    vec![
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxSources,
            limit: 1,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxModelRequests,
            limit: 0,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxInputUnits,
            limit: 0,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxOutputUnits,
            limit: 0,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxCostUnits,
            limit: 0,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxRetriesPerStep,
            limit: crate::codec::start_shape::NO_MODEL_MAX_RETRIES_PER_STEP,
        },
    ]
}

/// Sets the model-request budget and the retry budget that must move with it.
///
/// They are one decision: a retry budget over a zero request budget would
/// authorize an attempt the request budget already refuses, and the decoder
/// checks the pair (decision 0218).
pub(super) fn set_model_budget(command: &mut wire::StartTaskCommand, limit: u64) {
    let retries = if limit == 0 {
        crate::codec::start_shape::NO_MODEL_MAX_RETRIES_PER_STEP
    } else {
        crate::codec::start_shape::MAX_RETRIES_PER_STEP
    };
    for budget in &mut command.budgets {
        match budget.kind {
            wire::TaskBudgetKind::MaxModelRequests => budget.limit = limit,
            wire::TaskBudgetKind::MaxRetriesPerStep => budget.limit = retries,
            _ => {}
        }
    }
}

pub(super) fn selected_source_research(template: wire::TaskTemplateId) -> wire::StartTaskCommand {
    let mut command = start();
    command.template_id = template;
    command.control_mode = wire::TaskControlMode::Assistant;
    command.provider_route_id = Some("direct_user_key".to_owned());
    command.consent_preview.provider_route = wire::TaskProviderRoute::DirectUserKey;
    set_model_budget(
        &mut command,
        super::super::start_shape::DIRECT_USER_KEY_MAX_MODEL_REQUESTS,
    );
    command
}

pub(super) fn direct_start() -> wire::StartTaskCommand {
    let mut start = start();
    start.provider_route_id = Some("direct_user_key".to_owned());
    start.consent_preview.provider_route = wire::TaskProviderRoute::DirectUserKey;
    set_model_budget(
        &mut start,
        crate::codec::start_shape::DIRECT_USER_KEY_MAX_MODEL_REQUESTS,
    );
    start
}

pub(super) fn managed_start() -> wire::StartTaskCommand {
    let mut start = start();
    start.provider_route_id = Some("managed_service".to_owned());
    start.consent_preview.provider_route = wire::TaskProviderRoute::ManagedService;
    set_model_budget(
        &mut start,
        crate::codec::start_shape::MANAGED_SERVICE_MAX_MODEL_REQUESTS,
    );
    start
}

/// An errand nobody named a site for, which is the case the shape exists
/// for.
pub(super) fn errand_start(new_source_cap: u32) -> wire::StartTaskCommand {
    let mut start = start();
    start.goal = "get me the document that site issues me".to_owned();
    start.template_id = wire::TaskTemplateId::WebErrand;
    start.provider_route_id = Some("direct_user_key".to_owned());
    start.consent_preview.provider_route = wire::TaskProviderRoute::DirectUserKey;
    start.consent_preview.sources = Vec::new();
    start.consent_preview.source_discovery_enabled = true;
    start.consent_preview.new_source_cap = new_source_cap;
    start.budgets = vec![
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxSources,
            limit: u64::from(new_source_cap),
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxModelRequests,
            limit: crate::codec::start_shape::ERRAND_MAX_MODEL_REQUESTS,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxInputUnits,
            limit: 0,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxOutputUnits,
            limit: 0,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxCostUnits,
            limit: 0,
        },
        wire::TaskBudget {
            kind: wire::TaskBudgetKind::MaxRetriesPerStep,
            limit: crate::codec::start_shape::MAX_RETRIES_PER_STEP,
        },
    ];
    start
}
