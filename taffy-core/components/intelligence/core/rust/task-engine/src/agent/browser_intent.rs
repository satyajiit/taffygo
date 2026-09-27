// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection from one validated browser call into its closed operation.

use crate::action::{
    ActionIntent, BrowserIntent, DisclosureState, DomQueryRole, MediaOperation, MediaToolIntent,
    ObservedNodeHandle, OpaqueOperandKind, PythonEntrypoint, ScrollDirection, ToolJobIntent,
};
use crate::tool::{ArgumentValue, ToolEntry, ToolRuntime};
use crate::workflow::WorkflowDigest;
use crate::{BrowserSessionId, SuppliedFieldValues};

use super::target::CallTarget;
use super::{AgentError, ModelToolCall, TurnResidency};

pub(super) struct BrowserIntentContext<'a> {
    pub(super) turn_ordinal: u64,
    pub(super) sequence: u32,
    pub(super) residency: &'a TurnResidency,
    pub(super) target: &'a CallTarget,
    pub(super) browser_session_id: &'a BrowserSessionId,
    pub(super) supplied_field_values: Option<&'a SuppliedFieldValues>,
    pub(super) digest: &'a dyn WorkflowDigest,
}

pub(super) fn browser_intent(
    call: &ModelToolCall,
    context: &BrowserIntentContext<'_>,
) -> Result<ActionIntent, AgentError> {
    let tab = context.target.tab_id.clone();
    let intent = match call.tool_name.as_str() {
        "browser.navigate" => BrowserIntent::Navigate {
            tab,
            address: address(call, "address")?,
            // Opening a task-owned tab is a different authority and has the
            // dedicated `browser.tabs.open` row. The canonical intent keeps
            // this legacy field so old records still decode fail-closed.
            new_tab: false,
        },
        "browser.search" => BrowserIntent::Search {
            tab,
            query: bind(
                context.residency,
                context.turn_ordinal,
                context.sequence,
                OpaqueOperandKind::SearchQuery,
                context.digest,
            )?,
        },
        "browser.back" => BrowserIntent::HistoryBack { tab },
        "browser.forward" => BrowserIntent::HistoryForward { tab },
        "browser.reload" => BrowserIntent::Reload { tab },
        "browser.stop_loading" => BrowserIntent::StopLoading { tab },
        "browser.tabs.open" => BrowserIntent::TabsOpen {
            context: tab,
            address: optional_address(call, "address"),
        },
        "browser.tabs.list" => BrowserIntent::TabsList {
            context: tab,
            browser_session_id: context.browser_session_id.clone(),
        },
        "browser.tabs.activate" => BrowserIntent::TabsActivate {
            context: tab,
            target: required_task_tab(context.target)?,
        },
        "browser.tabs.close" => BrowserIntent::TabsClose {
            context: tab,
            target: required_task_tab(context.target)?,
        },
        "browser.dom.query" => dom_query_intent(
            tab,
            context.turn_ordinal,
            context.sequence,
            context.residency,
            call,
            context.target,
            context.digest,
        )?,
        "browser.dom.read" => BrowserIntent::DomRead { tab, target: None },
        "browser.link.open" => BrowserIntent::LinkOpen {
            target: required_observed_node(context.target)?,
        },
        "browser.dom.scroll" => dom_scroll_intent(tab, call, context.target)?,
        "browser.form.inspect" => BrowserIntent::FormInspect {
            tab,
            form: required_node(context.target)?,
        },
        "browser.form.fill" => form_value_intent(tab, call, context, false)?,
        "browser.form.select" => form_value_intent(tab, call, context, true)?,
        "browser.form.toggle" => BrowserIntent::FormToggle {
            tab,
            field: required_node(context.target)?,
            checked: flag(call, "checked").ok_or(AgentError::ContradictoryReply)?,
        },
        "browser.form.submit" => BrowserIntent::FormSubmit {
            tab,
            control: required_node(context.target)?,
        },
        "browser.download.start" => BrowserIntent::DownloadStart {
            tab,
            address: address(call, "address")?,
            browser_session_id: context.browser_session_id.clone(),
        },
        "browser.download.from_link" => BrowserIntent::DownloadFromLink {
            target: required_observed_node(context.target)?,
            browser_session_id: context.browser_session_id.clone(),
        },
        "browser.download.list" => BrowserIntent::DownloadList {
            tab,
            browser_session_id: context.browser_session_id.clone(),
        },
        "browser.download.cancel" => download_cancel_intent(tab, context.target)?,
        "browser.selection.read" => BrowserIntent::SelectionRead { tab },
        "page.images.describe" => BrowserIntent::ImageDescribe {
            tab,
            target: required_node(context.target)?,
        },
        "page.images.read_text" => BrowserIntent::ImageReadText {
            tab,
            target: required_node(context.target)?,
        },
        "page.video.inspect" => BrowserIntent::VideoInspect {
            tab,
            target: required_node(context.target)?,
        },
        "page.pdf.inspect" => BrowserIntent::PdfInspect { tab },
        "page.screenshot.inspect" => BrowserIntent::PageScreenshotInspect { tab },
        "browser.dom.click" | "browser.dom.focus" => exact_node_intent(call, context.target)?,
        _ => return Err(AgentError::ContradictoryReply),
    };
    Ok(ActionIntent::Browser(intent))
}

fn download_cancel_intent(
    tab: bip_types::identity::TabId,
    target: &CallTarget,
) -> Result<BrowserIntent, AgentError> {
    let (browser_session_id, download) = required_task_download(target)?;
    Ok(BrowserIntent::DownloadCancel {
        tab,
        browser_session_id: browser_session_id.clone(),
        download_id: download.download_id().to_owned(),
    })
}

fn form_value_intent(
    tab: bip_types::identity::TabId,
    call: &ModelToolCall,
    context: &BrowserIntentContext<'_>,
    select: bool,
) -> Result<BrowserIntent, AgentError> {
    let (value_request, value_from) =
        supplied_value(context.supplied_field_values, call, "value_from")?;
    let field = required_node(context.target)?;
    Ok(if select {
        BrowserIntent::FormSelect {
            tab,
            field,
            value_request,
            value_from,
        }
    } else {
        BrowserIntent::FormFill {
            tab,
            field,
            value_request,
            value_from,
        }
    })
}

fn required_task_tab(target: &CallTarget) -> Result<crate::action::TaskTabTarget, AgentError> {
    target
        .task_tab
        .clone()
        .ok_or(AgentError::ContradictoryReply)
}

fn required_task_download(
    target: &CallTarget,
) -> Result<(&BrowserSessionId, &crate::TaskDownloadSnapshot), AgentError> {
    target
        .task_download
        .as_ref()
        .map(|(session, download)| (session, download))
        .ok_or(AgentError::ContradictoryReply)
}

fn dom_query_intent(
    tab: bip_types::identity::TabId,
    turn_ordinal: u64,
    sequence: u32,
    residency: &TurnResidency,
    call: &ModelToolCall,
    target: &CallTarget,
    digest: &dyn WorkflowDigest,
) -> Result<BrowserIntent, AgentError> {
    let role = match choice(call, "role") {
        Some(role) => Some(DomQueryRole::from_choice(role).ok_or(AgentError::ContradictoryReply)?),
        None => None,
    };
    let text = if argument(call, "text").is_some() {
        Some(bind(
            residency,
            turn_ordinal,
            sequence,
            OpaqueOperandKind::DomQueryText,
            digest,
        )?)
    } else {
        None
    };
    Ok(BrowserIntent::DomQuery {
        tab,
        within: target.node_id.clone(),
        role,
        text,
        limit: count(call, "limit"),
    })
}

fn dom_scroll_intent(
    tab: bip_types::identity::TabId,
    call: &ModelToolCall,
    target: &CallTarget,
) -> Result<BrowserIntent, AgentError> {
    let direction = ScrollDirection::from_choice(
        choice(call, "direction").ok_or(AgentError::ContradictoryReply)?,
    )
    .ok_or(AgentError::ContradictoryReply)?;
    if direction != ScrollDirection::ToNode {
        return Err(AgentError::ContradictoryReply);
    }
    Ok(BrowserIntent::DomScroll {
        tab,
        direction,
        target: Some(required_node(target)?),
    })
}

pub(super) fn tool_job_intent(
    entry: &ToolEntry,
    runtime: ToolRuntime,
    call: &ModelToolCall,
    context: &BrowserIntentContext<'_>,
) -> Result<ActionIntent, AgentError> {
    if runtime == ToolRuntime::Media {
        let (browser_session_id, source) = context
            .target
            .task_download
            .as_ref()
            .ok_or(AgentError::ContradictoryReply)?;
        let tool_name = entry
            .canonical_name(&call.tool_name)
            .ok_or(AgentError::UnnameableTool)?;
        let operation =
            MediaOperation::from_tool_name(&tool_name).ok_or(AgentError::ContradictoryReply)?;
        let max_frames = if operation == MediaOperation::SampleFrames {
            u32::try_from(count(call, "max_frames").unwrap_or(6))
                .ok()
                .filter(|value| (1..=12).contains(value))
                .ok_or(AgentError::ContradictoryReply)?
        } else {
            0
        };
        return Ok(ActionIntent::MediaTool(MediaToolIntent {
            tool_name,
            operation,
            source_id: source.download_id().to_owned(),
            source_browser_session_id: browser_session_id.clone(),
            source_bytes: source.received_bytes(),
            tab: context.target.tab_id.clone(),
            node: None,
            max_frames,
            idempotency: entry.idempotency,
        }));
    }
    if runtime != ToolRuntime::Python {
        return Err(AgentError::ContradictoryReply);
    }
    let tool_name = entry
        .canonical_name(&call.tool_name)
        .ok_or(AgentError::UnnameableTool)?;
    let entrypoint = PythonEntrypoint::from_choice(
        choice(call, "entrypoint").ok_or(AgentError::ContradictoryReply)?,
    )
    .ok_or(AgentError::ContradictoryReply)?;
    Ok(ActionIntent::ToolJob(ToolJobIntent {
        tool_name,
        runtime,
        entrypoint,
        title: bind(
            context.residency,
            context.turn_ordinal,
            context.sequence,
            OpaqueOperandKind::PythonTitle,
            context.digest,
        )?,
        content: bind(
            context.residency,
            context.turn_ordinal,
            context.sequence,
            OpaqueOperandKind::PythonContent,
            context.digest,
        )?,
        tab: context.target.tab_id.clone(),
        node: context.target.node_id.clone(),
        idempotency: entry.idempotency,
    }))
}

fn bind(
    residency: &TurnResidency,
    ordinal: u64,
    sequence: u32,
    kind: OpaqueOperandKind,
    digest: &dyn WorkflowDigest,
) -> Result<crate::action::OpaqueOperandRef, AgentError> {
    residency
        .action_operands()
        .bind(ordinal, sequence, kind, digest)
        .map_err(|_| AgentError::DigestUnavailable)
}

fn argument<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a ArgumentValue> {
    call.arguments
        .iter()
        .find(|argument| argument.name == name)
        .map(|argument| &argument.value)
}

fn address(call: &ModelToolCall, name: &str) -> Result<String, AgentError> {
    optional_address(call, name).ok_or(AgentError::ContradictoryReply)
}

fn optional_address(call: &ModelToolCall, name: &str) -> Option<String> {
    match argument(call, name) {
        Some(ArgumentValue::Address(value)) => Some(value.clone()),
        _ => None,
    }
}

fn flag(call: &ModelToolCall, name: &str) -> Option<bool> {
    match argument(call, name) {
        Some(ArgumentValue::Flag(value)) => Some(*value),
        _ => None,
    }
}

fn choice<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a str> {
    match argument(call, name) {
        Some(ArgumentValue::Choice(value)) => Some(value),
        _ => None,
    }
}

fn count(call: &ModelToolCall, name: &str) -> Option<u64> {
    match argument(call, name) {
        Some(ArgumentValue::Count(value)) => Some(*value),
        _ => None,
    }
}

fn supplied_value(
    supplied: Option<&SuppliedFieldValues>,
    call: &ModelToolCall,
    name: &str,
) -> Result<(crate::FieldValueRequestId, u32), AgentError> {
    let Some(ArgumentValue::SuppliedValue(index)) = argument(call, name) else {
        return Err(AgentError::ContradictoryReply);
    };
    let supplied = supplied.ok_or(AgentError::ContradictoryReply)?;
    if !supplied.contains(*index) {
        return Err(AgentError::ContradictoryReply);
    }
    Ok((supplied.request_id().clone(), *index))
}

fn required_node(target: &CallTarget) -> Result<bip_types::identity::SemanticNodeId, AgentError> {
    target.node_id.clone().ok_or(AgentError::ContradictoryReply)
}

fn required_observed_node(target: &CallTarget) -> Result<ObservedNodeHandle, AgentError> {
    ObservedNodeHandle::from_node_handle(required_handle(target)?)
        .ok_or(AgentError::ContradictoryReply)
}

fn exact_node_intent(
    call: &ModelToolCall,
    target: &CallTarget,
) -> Result<BrowserIntent, AgentError> {
    let target = required_observed_node(target)?;
    match call.tool_name.as_str() {
        // `expected_state` is optional. Naming it asks for a disclosure
        // control's exact result; leaving it out is an ordinary press. A value
        // that is present and unrecognised is still a contradiction.
        "browser.dom.click" => Ok(BrowserIntent::DomClick {
            target,
            expected_state: match choice(call, "expected_state") {
                None => None,
                Some(value) => Some(
                    DisclosureState::from_choice(value).ok_or(AgentError::ContradictoryReply)?,
                ),
            },
        }),
        "browser.dom.focus" => Ok(BrowserIntent::DomFocus { target }),
        _ => Err(AgentError::ContradictoryReply),
    }
}

fn required_handle(target: &CallTarget) -> Result<&bip_types::identity::NodeHandle, AgentError> {
    target
        .node_handle
        .as_ref()
        .ok_or(AgentError::ContradictoryReply)
}
