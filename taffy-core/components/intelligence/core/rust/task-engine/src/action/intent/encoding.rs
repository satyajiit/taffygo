// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tagged, length-prefixed canonical encoding of one action intent.

mod browser_operation;
mod decode;
mod downloads;
mod store;

pub(super) use self::decode::decode;

use super::{
    ActionIntent, BrowserIntent, DomQueryRole, LibraryIntent, MediaToolIntent, MemoryIntent,
    MemoryScopeIntent, ScrollDirection,
};
use crate::tool::{IdempotencyClass, ToolRuntime};

pub(super) const PREFIX: &[u8] = b"taffy.action-intent.v1";

pub(super) fn encode(intent: &ActionIntent) -> Vec<u8> {
    let mut out = PREFIX.to_vec();
    match intent {
        ActionIntent::Browser(intent) => encode_browser(&mut out, intent),
        ActionIntent::Library(intent) => encode_library(&mut out, intent),
        ActionIntent::Memory(intent) => encode_memory(&mut out, intent),
        ActionIntent::Store(intent) => store::encode(&mut out, intent),
        ActionIntent::MediaTool(intent) => encode_media_tool(&mut out, intent),
        ActionIntent::ToolJob(intent) => {
            field(&mut out, 0, &[255]);
            field(&mut out, 1, intent.tool_name.as_bytes());
            field(&mut out, 2, &[runtime_tag(intent.runtime)]);
            field(&mut out, 3, intent.tab.as_str().as_bytes());
            optional_text(
                &mut out,
                4,
                intent
                    .node
                    .as_ref()
                    .map(bip_types::identity::SemanticNodeId::as_str),
            );
            field(&mut out, 5, &[idempotency_tag(intent.idempotency)]);
            field(&mut out, 6, &[intent.entrypoint.wire_tag()]);
            opaque(&mut out, 7, &intent.title);
            opaque(&mut out, 8, &intent.content);
        }
    }
    out
}

fn encode_media_tool(out: &mut Vec<u8>, intent: &MediaToolIntent) {
    field(out, 0, &[252]);
    field(out, 1, intent.tool_name.as_bytes());
    field(out, 2, &[intent.operation.wire_tag()]);
    field(out, 3, intent.source_id.as_bytes());
    field(out, 4, intent.source_browser_session_id.as_str().as_bytes());
    field(out, 5, intent.tab.as_str().as_bytes());
    optional_text(
        out,
        6,
        intent
            .node
            .as_ref()
            .map(bip_types::identity::SemanticNodeId::as_str),
    );
    field(out, 7, &intent.max_frames.to_le_bytes());
    field(out, 8, &[idempotency_tag(intent.idempotency)]);
    field(out, 9, &intent.source_bytes.to_le_bytes());
}

fn encode_memory(out: &mut Vec<u8>, intent: &MemoryIntent) {
    field(out, 0, &[253]);
    match intent {
        MemoryIntent::Search { tab, query, limit } => {
            field(out, 1, &[0]);
            field(out, 2, tab.as_str().as_bytes());
            opaque(out, 3, query);
            field(out, 4, &limit.to_le_bytes());
        }
        MemoryIntent::Save {
            tab,
            statement,
            scope,
            expires_at_epoch_ms,
        } => {
            field(out, 1, &[1]);
            field(out, 2, tab.as_str().as_bytes());
            opaque(out, 3, statement);
            encode_memory_scope(out, 4, 5, scope);
            optional_u64(out, 6, *expires_at_epoch_ms);
        }
        MemoryIntent::Update {
            tab,
            memory_id,
            record_revision,
            statement,
            scope,
            expires_at_epoch_ms,
        } => {
            field(out, 1, &[2]);
            field(out, 2, tab.as_str().as_bytes());
            field(out, 3, memory_id.as_bytes());
            field(out, 4, &record_revision.to_le_bytes());
            opaque(out, 5, statement);
            encode_memory_scope(out, 6, 7, scope);
            optional_u64(out, 8, *expires_at_epoch_ms);
        }
        MemoryIntent::Delete {
            tab,
            memory_id,
            record_revision,
        } => {
            field(out, 1, &[3]);
            field(out, 2, tab.as_str().as_bytes());
            field(out, 3, memory_id.as_bytes());
            field(out, 4, &record_revision.to_le_bytes());
        }
    }
}

fn encode_memory_scope(
    out: &mut Vec<u8>,
    kind_tag: u8,
    workspace_tag: u8,
    scope: &MemoryScopeIntent,
) {
    match scope {
        MemoryScopeIntent::AllTasks => {
            field(out, kind_tag, &[0]);
            optional_text(out, workspace_tag, None);
        }
        MemoryScopeIntent::Workspace { workspace_id } => {
            field(out, kind_tag, &[1]);
            optional_text(out, workspace_tag, Some(workspace_id));
        }
    }
}

fn encode_library(out: &mut Vec<u8>, intent: &LibraryIntent) {
    field(out, 0, &[254]);
    match intent {
        LibraryIntent::Search { tab, query, limit } => {
            field(out, 1, &[0]);
            field(out, 2, tab.as_str().as_bytes());
            opaque(out, 3, query);
            field(out, 4, &limit.to_le_bytes());
        }
        LibraryIntent::Save {
            tab,
            workspace_id,
            workspace_revision,
            fact_id,
            entry_revision,
        } => {
            field(out, 1, &[1]);
            field(out, 2, tab.as_str().as_bytes());
            field(out, 3, workspace_id.as_bytes());
            field(out, 4, &workspace_revision.to_le_bytes());
            field(out, 5, fact_id.as_bytes());
            field(out, 6, &entry_revision.to_le_bytes());
        }
        LibraryIntent::Remove {
            tab,
            entry_id,
            entry_revision,
        } => {
            field(out, 1, &[2]);
            field(out, 2, tab.as_str().as_bytes());
            field(out, 3, entry_id.as_bytes());
            field(out, 4, &entry_revision.to_le_bytes());
        }
    }
}

fn encode_browser(out: &mut Vec<u8>, intent: &BrowserIntent) {
    field(out, 0, &[browser_operation::tag(intent)]);
    field(out, 1, intent.tab_id().as_str().as_bytes());
    match intent {
        BrowserIntent::Navigate {
            address, new_tab, ..
        } => {
            field(out, 2, address.as_bytes());
            field(out, 3, &[u8::from(*new_tab)]);
        }
        BrowserIntent::Search { query, .. } => opaque(out, 2, query),
        BrowserIntent::TabsOpen { address, .. } => optional_text(out, 2, address.as_deref()),
        BrowserIntent::TabsList {
            browser_session_id, ..
        }
        | BrowserIntent::DownloadList {
            browser_session_id, ..
        } => field(out, 2, browser_session_id.as_str().as_bytes()),
        BrowserIntent::DownloadFromLink {
            target,
            browser_session_id,
        } => {
            downloads::from_link(out, target, browser_session_id);
        }
        BrowserIntent::DownloadCancel {
            browser_session_id,
            download_id,
            ..
        } => downloads::cancel(out, browser_session_id, download_id),
        BrowserIntent::TabsActivate { target, .. } | BrowserIntent::TabsClose { target, .. } => {
            encode_observed_tab_target(out, target);
        }
        BrowserIntent::DomQuery {
            within,
            role,
            text,
            limit,
            ..
        } => encode_dom_query(out, within.as_ref(), *role, text.as_ref(), *limit),
        BrowserIntent::DomRead { target, .. } => encode_optional_node(out, 2, target.as_ref()),
        BrowserIntent::DomClick {
            target,
            expected_state,
            ..
        } => encode_dom_activation(out, target, *expected_state),
        BrowserIntent::DomFocus { target } | BrowserIntent::LinkOpen { target } => {
            encode_observed_node_target(out, target);
        }
        BrowserIntent::FormInspect { form: target, .. }
        | BrowserIntent::FormSubmit {
            control: target, ..
        }
        | BrowserIntent::ImageDescribe { target, .. }
        | BrowserIntent::ImageReadText { target, .. }
        | BrowserIntent::VideoInspect { target, .. } => field(out, 2, target.as_str().as_bytes()),
        BrowserIntent::DomScroll {
            direction, target, ..
        } => {
            field(out, 2, &[ScrollDirection::wire_tag(*direction)]);
            optional_text(
                out,
                3,
                target
                    .as_ref()
                    .map(bip_types::identity::SemanticNodeId::as_str),
            );
        }
        BrowserIntent::FormFill {
            field: target,
            value_request,
            value_from,
            ..
        }
        | BrowserIntent::FormSelect {
            field: target,
            value_request,
            value_from,
            ..
        } => encode_supplied_form_value(out, target, value_request, *value_from),
        BrowserIntent::FormToggle {
            field: target,
            checked,
            ..
        } => {
            field(out, 2, target.as_str().as_bytes());
            field(out, 3, &[u8::from(*checked)]);
        }
        BrowserIntent::DownloadStart {
            address,
            browser_session_id,
            ..
        } => downloads::start(out, address, browser_session_id),
        BrowserIntent::HistoryBack { .. }
        | BrowserIntent::HistoryForward { .. }
        | BrowserIntent::Reload { .. }
        | BrowserIntent::StopLoading { .. }
        | BrowserIntent::SelectionRead { .. }
        | BrowserIntent::PdfInspect { .. }
        | BrowserIntent::PageScreenshotInspect { .. } => {}
    }
}

fn encode_optional_node(
    out: &mut Vec<u8>,
    tag: u8,
    node: Option<&bip_types::identity::SemanticNodeId>,
) {
    optional_text(
        out,
        tag,
        node.map(bip_types::identity::SemanticNodeId::as_str),
    );
}

fn encode_supplied_form_value(
    out: &mut Vec<u8>,
    target: &bip_types::identity::SemanticNodeId,
    value_request: &crate::field_values::FieldValueRequestId,
    value_from: u32,
) {
    field(out, 2, target.as_str().as_bytes());
    field(out, 3, value_request.as_str().as_bytes());
    field(out, 4, &value_from.to_le_bytes());
}

fn encode_observed_tab_target(out: &mut Vec<u8>, target: &super::TaskTabTarget) {
    field(out, 2, target.browser_session_id().as_str().as_bytes());
    field(out, 3, target.tab_id().as_str().as_bytes());
    field(out, 4, target.frame_id().as_str().as_bytes());
    field(out, 5, target.page_epoch().as_str().as_bytes());
    field(out, 6, &target.graph_revision().0.to_le_bytes());
}

fn encode_dom_activation(
    out: &mut Vec<u8>,
    target: &super::ObservedNodeHandle,
    expected_state: Option<super::DisclosureState>,
) {
    encode_observed_node_target(out, target);
    field(
        out,
        8,
        &[super::DisclosureState::wire_tag_of(expected_state)],
    );
}

fn encode_dom_query(
    out: &mut Vec<u8>,
    within: Option<&bip_types::identity::SemanticNodeId>,
    role: Option<DomQueryRole>,
    text: Option<&super::OpaqueOperandRef>,
    limit: Option<u64>,
) {
    optional_text(
        out,
        2,
        within.map(bip_types::identity::SemanticNodeId::as_str),
    );
    optional_byte(out, 3, role.map(DomQueryRole::wire_tag));
    optional_opaque(out, 4, text);
    optional_u64(out, 5, limit);
}

fn encode_observed_node_target(out: &mut Vec<u8>, target: &super::ObservedNodeHandle) {
    field(out, 2, target.frame_id().as_str().as_bytes());
    field(out, 3, target.page_epoch().as_str().as_bytes());
    field(out, 4, &target.graph_revision().0.to_le_bytes());
    field(out, 5, target.node_id().as_str().as_bytes());
    field(out, 6, &[target.expected_origin_kind().wire_tag()]);
    field(out, 7, target.expected_origin_value().as_bytes());
}

fn opaque(out: &mut Vec<u8>, tag: u8, value: &super::OpaqueOperandRef) {
    let mut bytes = Vec::new();
    field(&mut bytes, 0, value.handle().as_bytes());
    field(&mut bytes, 1, &[value.kind().wire_tag()]);
    field(&mut bytes, 2, value.digest());
    field(out, tag, &bytes);
}

fn optional_opaque(out: &mut Vec<u8>, tag: u8, value: Option<&super::OpaqueOperandRef>) {
    match value {
        Some(value) => {
            let mut bytes = vec![1];
            opaque(&mut bytes, 0, value);
            field(out, tag, &bytes);
        }
        None => field(out, tag, &[0]),
    }
}

fn field(out: &mut Vec<u8>, tag: u8, bytes: &[u8]) {
    out.push(tag);
    out.extend_from_slice(&u64::try_from(bytes.len()).unwrap_or(u64::MAX).to_le_bytes());
    out.extend_from_slice(bytes);
}

fn optional_text(out: &mut Vec<u8>, tag: u8, value: Option<&str>) {
    match value {
        Some(value) => {
            let mut bytes = Vec::with_capacity(value.len().saturating_add(1));
            bytes.push(1);
            bytes.extend_from_slice(value.as_bytes());
            field(out, tag, &bytes);
        }
        None => field(out, tag, &[0]),
    }
}

fn optional_byte(out: &mut Vec<u8>, tag: u8, value: Option<u8>) {
    match value {
        Some(value) => field(out, tag, &[1, value]),
        None => field(out, tag, &[0]),
    }
}

fn optional_u64(out: &mut Vec<u8>, tag: u8, value: Option<u64>) {
    match value {
        Some(value) => {
            let mut bytes = vec![1];
            bytes.extend_from_slice(&value.to_le_bytes());
            field(out, tag, &bytes);
        }
        None => field(out, tag, &[0]),
    }
}

fn runtime_tag(runtime: ToolRuntime) -> u8 {
    match runtime {
        ToolRuntime::Media => 0,
        ToolRuntime::Python => 1,
        ToolRuntime::LocalModel => 2,
        ToolRuntime::Wasm => 3,
    }
}

fn idempotency_tag(value: IdempotencyClass) -> u8 {
    match value {
        IdempotencyClass::PureRead => 0,
        IdempotencyClass::IdempotentWrite => 1,
        IdempotencyClass::ConditionallyIdempotent => 2,
        IdempotencyClass::Consequential => 3,
    }
}
