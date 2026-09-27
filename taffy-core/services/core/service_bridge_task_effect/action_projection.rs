// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The action-family binding bodies the effect projection fills.
//!
//! Apart from `projection` for the reason the enum tables are: `project_effect`
//! is the index of the binding surface, and the four bodies here — the shared
//! action facts, the policy ask, the approval and the grant-spending dispatch —
//! are what a reader would otherwise step over to see it.

use core_runtime::ports::ActionEffectFacts;
use core_runtime::wire;
use core_runtime::{ActionClass, ActionIntent, BrowserIntent, StoreIntent, TaskId};

use super::enums::{action_class, action_operation, control, postcondition, risk};
use super::TaskGrantFacts;
use crate::ffi;
use crate::service_bridge_runtime::ServiceBridge;

pub(super) fn project_policy(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    out: &mut ffi::BridgeTaskEffect,
    facts: ActionEffectFacts,
) -> Result<(), ()> {
    project_common_action(out, &facts)?;
    project_transient_search_query(bridge, task_id, out, &facts)?;
    project_transient_store_query(bridge, task_id, out, &facts)?;
    project_discovery_authority(bridge, task_id, out, &facts)?;
    out.principal = if let Some(skill) = facts.skill_version_id {
        out.principal_id = skill;
        wire::PolicyPrincipalKind::Skill as u8
    } else {
        wire::PolicyPrincipalKind::Assistant as u8
    };
    out.data_classes = vec![wire::BipSensitivity::NotSensitive as u8];
    out.context_risk = risk(facts.proposal.action_class()) as u8;
    out.control_mode = control(facts.control_mode) as u8;
    out.policy_version = facts.policy_version.0;
    if let Some(approval) = facts.approval {
        out.has_approval = true;
        out.approval_receipt_id = approval.receipt.0;
        out.approval_expires_at_monotonic_ms = approval.expires_at.0;
        out.approval_expires_at_utc_ms = approval.expires_at_utc.0;
        out.approval_browser_session_id = approval.browser_session_id.as_str().to_owned();
    }
    Ok(())
}

fn project_discovery_authority(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    out: &mut ffi::BridgeTaskEffect,
    facts: &ActionEffectFacts,
) -> Result<(), ()> {
    // The two moves a zero-source errand may make from its blank tab: the
    // search it was built for, and a typed navigate to a site the model
    // already knows (decision 0106 section 2 — the address is a lead, and the
    // browser going there is what makes it a source). Both carry the same
    // bounded fact; where either lands is still policy's question.
    if !matches!(
        facts.proposal.intent(),
        ActionIntent::Browser(
            BrowserIntent::Search { .. } | BrowserIntent::Navigate { new_tab: false, .. }
        )
    ) {
        return Ok(());
    }
    let Some(discovery) = bridge
        .runtime
        .as_ref()
        .and_then(|runtime| runtime.core().task(task_id))
        .and_then(core_runtime::TaskEnginePort::discovery_authority_facts)
    else {
        return Ok(());
    };
    // An action in some other tab is not a broken discovery fact; it is an
    // action discovery has nothing to say about. The task may hold a page and
    // a blank tab at the same time, so most of its moves are in a tab that is
    // not the discovery tab, and refusing those would refuse the effect — the
    // projection fails, the commit result is invalid, and the core goes down
    // with every task in the profile. That is what happened when this was one
    // clause among the integrity checks below (decision 0224).
    if discovery.discovery_tab_id != facts.proposal.tab_id().0 {
        return Ok(());
    }
    // These are integrity checks and stay refusals. A skill principal holds no
    // errand discovery authority, and a session or cap the core answered
    // out of range is a disagreement, not an inapplicable fact.
    if facts.skill_version_id.is_some()
        || discovery.browser_session_id.is_empty()
        || discovery.remaining_new_source_cap == 0
        || discovery.remaining_new_source_cap > core_runtime::MAX_WEB_ERRAND_NEW_SOURCE_CAP
    {
        return Err(());
    }
    out.has_policy_discovery = true;
    out.policy_discovery_tab_id = discovery.discovery_tab_id;
    out.policy_discovery_browser_session_id = discovery.browser_session_id;
    out.policy_discovery_remaining_new_source_cap = discovery.remaining_new_source_cap;
    Ok(())
}

pub(super) fn project_approval(
    out: &mut ffi::BridgeTaskEffect,
    facts: ActionEffectFacts,
) -> Result<(), ()> {
    project_common_action(out, &facts)
}

fn project_common_action(
    out: &mut ffi::BridgeTaskEffect,
    facts: &ActionEffectFacts,
) -> Result<(), ()> {
    if facts.proposal.proposal_digest.value.len() != 64 {
        return Err(());
    }
    out.action_id = facts.action_id.clone();
    out.action_class = action_class(facts.proposal.action_class()) as u8;
    out.action_operation = action_operation(facts.proposal.intent()) as u8;
    out.tool_name = facts.proposal.tool_name().to_owned();
    out.canonical_intent = facts.proposal.intent().encode_canonical().map_err(|_| ())?;
    if out.canonical_intent.is_empty()
        || out.canonical_intent.len() > wire::MAX_CANONICAL_ACTION_INTENT_BYTES
    {
        return Err(());
    }
    out.proposal_digest = facts.proposal.proposal_digest.value.clone();
    out.action_idempotency_key = facts.proposal.idempotency_key.as_str().to_owned();
    out.tab_id = facts.proposal.tab_id().0.clone();
    // DomQuery's optional `within` is a Rust-side result filter, not the scope
    // of work performed by the browser. Keep it only in canonical_intent and
    // mint a document observation even if the intent accessor ever regresses.
    if !matches!(
        facts.proposal.intent(),
        ActionIntent::Browser(BrowserIntent::DomQuery { .. })
    ) {
        if let Some(node_id) = facts.proposal.node_id() {
            out.has_node_id = true;
            out.node_id = node_id.0.clone();
        }
    }
    if let Some(address) = facts.proposal.destination_address() {
        out.has_destination_address = true;
        out.destination_address = address.to_owned();
    }
    if let ActionIntent::Browser(BrowserIntent::Search { query, .. })
    | ActionIntent::Store(
        StoreIntent::HistorySearch { query, .. } | StoreIntent::BookmarksSearch { query, .. },
    ) = facts.proposal.intent()
    {
        out.has_operand_handle = true;
        out.operand_handle = query.handle().to_owned();
    }
    match facts.proposal.intent() {
        ActionIntent::Browser(BrowserIntent::TabsList {
            browser_session_id, ..
        }) => {
            out.has_task_tab_binding = true;
            out.task_tab_browser_session_id = browser_session_id.as_str().to_owned();
        }
        ActionIntent::Browser(
            BrowserIntent::TabsActivate { target, .. } | BrowserIntent::TabsClose { target, .. },
        ) => {
            out.has_task_tab_binding = true;
            out.task_tab_browser_session_id = target.browser_session_id().as_str().to_owned();
            out.has_task_tab_target = true;
            out.task_tab_target_tab_id = target.tab_id().0.clone();
            out.task_tab_target_frame_id = target.frame_id().0.clone();
            out.task_tab_target_page_epoch = target.page_epoch().0.clone();
            out.task_tab_target_graph_revision = target.graph_revision().0;
        }
        ActionIntent::Browser(
            BrowserIntent::DownloadStart {
                browser_session_id, ..
            }
            | BrowserIntent::DownloadFromLink {
                browser_session_id, ..
            }
            | BrowserIntent::DownloadList {
                browser_session_id, ..
            },
        ) => {
            out.has_task_download_binding = true;
            out.task_download_browser_session_id = browser_session_id.as_str().to_owned();
        }
        ActionIntent::Browser(BrowserIntent::DownloadCancel {
            browser_session_id,
            download_id,
            ..
        }) => {
            out.has_task_download_binding = true;
            out.task_download_browser_session_id = browser_session_id.as_str().to_owned();
            out.task_download_id = download_id.clone();
        }
        ActionIntent::Browser(
            BrowserIntent::FormFill {
                value_request,
                value_from,
                ..
            }
            | BrowserIntent::FormSelect {
                value_request,
                value_from,
                ..
            },
        ) => {
            out.action_input_kind = wire::TaskActionInputKind::SuppliedValue as u8;
            out.has_supplied_value = true;
            out.supplied_value_request_id = value_request.as_str().to_owned();
            out.supplied_value_index = *value_from;
        }
        ActionIntent::Browser(BrowserIntent::FormToggle { checked, .. }) => {
            out.action_input_kind = wire::TaskActionInputKind::ToggleState as u8;
            out.has_toggle_state = true;
            out.toggle_checked = *checked;
        }
        ActionIntent::Store(intent) => {
            let limit = intent.limit();
            if limit == 0 || limit > core_runtime::MAX_STORE_RESULTS {
                return Err(());
            }
            out.has_task_store_binding = true;
            out.task_store_limit = limit;
        }
        _ => {}
    }
    Ok(())
}

/// The words behind a store search, resolved from the generation-resident
/// operand the model named. A search kind with no resident words is refused
/// rather than sent empty: an empty query is a whole-store listing wearing a
/// search's identity.
fn project_transient_store_query(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    out: &mut ffi::BridgeTaskEffect,
    facts: &ActionEffectFacts,
) -> Result<(), ()> {
    let is_store_search = matches!(
        facts.proposal.intent(),
        ActionIntent::Store(
            StoreIntent::HistorySearch { .. } | StoreIntent::BookmarksSearch { .. }
        )
    );
    if !is_store_search {
        return Ok(());
    }
    let query = bridge
        .runtime
        .as_ref()
        .and_then(|runtime| {
            runtime
                .core()
                .transient_store_query(task_id.as_str(), &facts.proposal)
        })
        .ok_or(())?;
    if query.is_empty() || query.len() > wire::MAX_TASK_STORE_QUERY_BYTES {
        return Err(());
    }
    out.task_store_has_query = true;
    out.task_store_query = query.to_owned();
    Ok(())
}

fn project_transient_search_query(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    out: &mut ffi::BridgeTaskEffect,
    facts: &ActionEffectFacts,
) -> Result<(), ()> {
    let is_search = matches!(
        facts.proposal.intent(),
        ActionIntent::Browser(BrowserIntent::Search { .. })
    );
    if !is_search {
        return Ok(());
    }
    let query = bridge
        .runtime
        .as_ref()
        .and_then(|runtime| {
            runtime
                .core()
                .transient_search_query(task_id.as_str(), &facts.proposal)
        })
        .ok_or(())?;
    if query.is_empty() || query.len() > wire::MAX_TRANSIENT_SEARCH_QUERY_BYTES {
        return Err(());
    }
    out.has_transient_search_query = true;
    out.transient_search_query = query.to_owned();
    Ok(())
}

pub(super) fn project_action(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    out: &mut ffi::BridgeTaskEffect,
    facts: ActionEffectFacts,
) -> Result<(), ()> {
    project_common_action(out, &facts)?;
    project_transient_search_query(bridge, task_id, out, &facts)?;
    project_transient_store_query(bridge, task_id, out, &facts)?;
    let capability_id = facts.capability_id.ok_or(())?;
    let dispatch_id = facts.dispatch_id.ok_or(())?;
    let grant: &TaskGrantFacts = bridge
        .task_grants
        .get(&(task_id.as_str().to_owned(), facts.action_id.clone()))
        .ok_or(())?;
    if grant.capability_id != capability_id {
        return Err(());
    }
    match facts.proposal.intent() {
        ActionIntent::Browser(
            BrowserIntent::Search { .. }
            | BrowserIntent::LinkOpen { .. }
            | BrowserIntent::DownloadFromLink { .. },
        ) => {
            let destination = grant.destination_address.as_ref().ok_or(())?;
            out.has_destination_address = true;
            out.destination_address = destination.clone();
        }
        ActionIntent::Browser(BrowserIntent::Navigate { .. })
        | ActionIntent::Browser(BrowserIntent::TabsOpen { .. }) => {
            if grant.destination_address.as_deref() != facts.proposal.destination_address() {
                return Err(());
            }
        }
        _ => {}
    }
    out.capability_id = capability_id;
    out.dispatch_id = dispatch_id;
    out.frame_id = grant.frame_id.clone();
    out.page_epoch = grant.page_epoch.clone();
    out.graph_revision = grant.graph_revision;
    out.normalized_origin = grant.normalized_origin.clone();
    if let Some(opaque_origin_id) = grant.opaque_origin_id.as_ref() {
        out.has_opaque_origin_id = true;
        out.opaque_origin_id = opaque_origin_id.clone();
    }
    if let Some(destination) = grant.destination_origin.as_ref() {
        out.has_destination_origin = true;
        out.destination_origin = destination.clone();
    }
    out.preconditions = vec![
        wire::TaskActionPrecondition::DocumentUnchanged as u8,
        wire::TaskActionPrecondition::GraphRevisionAtLeast as u8,
    ];
    if out.has_node_id {
        out.preconditions
            .push(wire::TaskActionPrecondition::NodePresent as u8);
    }
    if out.has_destination_origin {
        out.preconditions
            .push(wire::TaskActionPrecondition::DestinationUnchanged as u8);
    }
    out.postcondition = postcondition(facts.proposal.intent()) as u8;
    if facts.proposal.action_class() == ActionClass::ObservePage
        && !matches!(
            facts.proposal.intent(),
            ActionIntent::Browser(
                BrowserIntent::TabsList { .. } | BrowserIntent::DownloadList { .. }
            )
        )
    {
        out.observation_scope = wire::ObservationScope::CurrentDocument as u8;
        out.observation_max_bytes =
            u32::try_from(wire::MAX_TASK_OBSERVATION_TOTAL_BYTES).map_err(|_| ())?;
        out.observation_max_nodes =
            u32::try_from(wire::MAX_TASK_OBSERVATION_NODES).map_err(|_| ())?;
        out.observation_max_text_bytes =
            u32::try_from(wire::MAX_TASK_OBSERVATION_TEXT_BYTES).map_err(|_| ())?;
        out.observation_max_frames =
            u32::try_from(wire::MAX_TASK_OBSERVATION_FRAMES).map_err(|_| ())?;
        out.observation_deadline_ms =
            u32::try_from(wire::MAX_TASK_OBSERVATION_DEADLINE_MS).map_err(|_| ())?;
    }
    Ok(())
}
