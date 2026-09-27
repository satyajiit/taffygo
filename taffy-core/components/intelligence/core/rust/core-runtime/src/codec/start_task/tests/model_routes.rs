// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A start on the person's own key or the managed route, with its bounded model budget.

use super::{direct_start, managed_start, operation};
use crate::codec::start_shape::StartTaskDecodeError;
use crate::codec::start_task::decode_start_task;
use crate::ports::TaskEngineLoad;
use core_service_types as wire;
use task_engine::ProviderRouteId;

#[test]
fn direct_user_key_start_decodes_with_the_bounded_model_budget() {
    let Ok(TaskEngineLoad::Fresh { seed, .. }) = decode_start_task(&direct_start(), &operation())
    else {
        unreachable!("a DirectUserKey start with the factory budget decodes")
    };
    assert_eq!(
        seed.snapshot
            .provider_route
            .as_ref()
            .map(ProviderRouteId::as_str),
        Some("direct_user_key")
    );
}

#[test]
fn a_zero_model_budget_on_direct_user_key_is_not_the_reviewed_refusal() {
    let mut start = direct_start();
    start
        .budgets
        .iter_mut()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxModelRequests)
        .into_iter()
        .for_each(|budget| budget.limit = 0);
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
}

#[test]
fn a_direct_user_key_start_still_refuses_discovery_and_an_empty_allowlist() {
    let mut discovery = direct_start();
    discovery.consent_preview.source_discovery_enabled = true;
    assert!(matches!(
        decode_start_task(&discovery, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));

    let mut empty = direct_start();
    empty.tool_allowlist.clear();
    assert!(matches!(
        decode_start_task(&empty, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
}

#[test]
fn a_shared_direct_user_key_start_is_not_the_agent_workflow() {
    let mut start = direct_start();
    start.control_mode = wire::TaskControlMode::Shared;
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
}

#[test]
fn a_direct_user_key_start_with_query_scroll_navigate_and_ask_decodes() {
    let mut start = direct_start();
    start.tool_allowlist = vec![
        "browser.dom.query".to_owned(),
        "browser.dom.read".to_owned(),
        "browser.dom.scroll".to_owned(),
        "browser.navigate".to_owned(),
        "user.ask".to_owned(),
    ];
    let Ok(TaskEngineLoad::Fresh { seed, .. }) = decode_start_task(&start, &operation()) else {
        unreachable!("DirectUserKey with query, scroll, navigate, and ask tools decodes")
    };
    assert_eq!(
        seed.snapshot.tool_allowlist,
        vec![
            "browser.dom.query".to_owned(),
            "browser.dom.read".to_owned(),
            "browser.dom.scroll".to_owned(),
            "browser.navigate".to_owned(),
            "user.ask".to_owned()
        ]
    );
}

#[test]
fn a_direct_user_key_start_can_narrow_to_typed_dom_activation() {
    let mut start = direct_start();
    start.tool_allowlist = vec![
        "browser.dom.read".to_owned(),
        "browser.dom.click".to_owned(),
    ];
    let Ok(TaskEngineLoad::Fresh { seed, .. }) = decode_start_task(&start, &operation()) else {
        unreachable!("the observation floor may be narrowed with typed disclosure activation")
    };
    assert_eq!(seed.snapshot.tool_allowlist, start.tool_allowlist);
}

#[test]
fn managed_service_start_decodes_with_the_bounded_model_budget() {
    let Ok(TaskEngineLoad::Fresh { seed, .. }) = decode_start_task(&managed_start(), &operation())
    else {
        unreachable!("a ManagedService start with the bounded budget decodes")
    };
    assert_eq!(
        seed.snapshot
            .provider_route
            .as_ref()
            .map(ProviderRouteId::as_str),
        Some("managed_service")
    );
}

#[test]
fn managed_service_start_needs_exactly_the_bounded_model_budget() {
    let mut start = managed_start();
    start
        .budgets
        .iter_mut()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxModelRequests)
        .into_iter()
        .for_each(|budget| {
            budget.limit = 0;
        });
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
}

#[test]
fn managed_service_start_requires_assistant_control() {
    let mut start = managed_start();
    start.control_mode = wire::TaskControlMode::Shared;
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
}
