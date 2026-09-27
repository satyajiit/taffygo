// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fixtures for the assistant loop: a deterministic digest adapter, a page
//! projection with one issued number, and a task holding one recorded reply.
//!
//! Shared by the decision-table suites and by the replay properties, so the
//! loop is driven from one description of what a turn looks like rather than
//! from two that could drift apart.

use bip_types::identity::{
    FrameId, GraphRevision, NodeHandle, Origin, OriginKind, PageEpoch, SemanticNodeId, TabId,
};
use task_engine::agent::{ModelReply, ModelToolCall, TurnPage, TurnResidency};
use task_engine::{
    ArgumentValue, Command, HandleTable, ModelHandle, ModelStopReason, PageReadability,
    RenderShape, SuppliedArgument, TurnUsage, ValueTarget, WorkflowDigest, WorkflowError,
};

use super::Fixture;

/// A deterministic stand-in for the browser's digest adapter.
///
/// It is not SHA-256 and does not claim to be. What every test here needs from
/// it is that the same material always folds to the same bytes, which is what
/// makes a proposal digest reproducible across a replay.
pub struct Digest;

impl WorkflowDigest for Digest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], WorkflowError> {
        let mut output = [0_u8; 32];
        let width = output.len();
        for (index, byte) in input.iter().enumerate() {
            if let Some(slot) = output.get_mut(index % width) {
                *slot = slot.wrapping_mul(31).wrapping_add(*byte);
            }
        }
        Ok(output)
    }
}

fn origin() -> Origin {
    Origin {
        kind: OriginKind::Tuple,
        serialization: Some("https://example.test".to_owned()),
        opaque_id: None,
    }
}

/// One node of the fixture page, with the six facts a handle freezes.
pub fn node(node_id: &str) -> NodeHandle {
    node_on(node_id, "epoch_1")
}

/// [`node`], read from the document `epoch` of the same tab and frame.
pub fn node_on(node_id: &str, epoch: &str) -> NodeHandle {
    NodeHandle::new(
        TabId::new("tab_1"),
        FrameId::new("frame_1"),
        PageEpoch::new(epoch),
        GraphRevision(4),
        SemanticNodeId::new(node_id),
        origin(),
    )
}

/// [`page`], composed from the document `epoch` of the same tab, so a suite
/// can walk from one document to the next.
pub fn page_on(epoch: &str) -> (TurnPage, ModelHandle) {
    let mut handles = HandleTable::new();
    let handle = handles
        .issue_marked(node_on("n-1", epoch), ValueTarget::None)
        .unwrap_or_else(|| unreachable!("the empty fixture handle table accepts one node"));
    (
        TurnPage::new(TabId::new("tab_1"), handles, readable_shape()),
        handle,
    )
}

/// A projection that offered one node, and the number it issued for it.
pub fn page() -> (TurnPage, ModelHandle) {
    page_offering(ValueTarget::None)
}

/// [`page`], whose one line is a form a person's values can go into, which
/// `user.request_values` may name and `browser.form.fill` may not.
pub fn form_page() -> (TurnPage, ModelHandle) {
    page_offering(ValueTarget::Container)
}

/// [`page`], whose one line is a field that can take text.
pub fn field_page() -> (TurnPage, ModelHandle) {
    page_offering(ValueTarget::Field)
}

fn page_offering(value_target: ValueTarget) -> (TurnPage, ModelHandle) {
    let mut handles = HandleTable::new();
    let handle = handles
        .issue_marked(node("n-1"), value_target)
        .unwrap_or_else(|| unreachable!("the empty fixture handle table accepts one node"));
    (
        TurnPage::new(TabId::new("tab_1"), handles, readable_shape()),
        handle,
    )
}

/// The shape of a projection that offered its one node.
fn readable_shape() -> RenderShape {
    RenderShape {
        readability: PageReadability::Readable,
        offered_nodes: 1,
        omitted_nodes: 0,
        unreadable_nodes: 0,
        unreadable_text_bytes: 0,
        digest: [7_u8; 32],
    }
}

/// A projection taken from no page at all, which is what a turn is composed
/// from before the first read and after a settled navigation retired the
/// bytes the last one left.
pub fn page_with_no_tab() -> TurnPage {
    TurnPage::new(
        TabId::new(String::new()),
        HandleTable::new(),
        RenderShape {
            readability: PageReadability::Empty,
            offered_nodes: 0,
            omitted_nodes: 0,
            unreadable_nodes: 0,
            unreadable_text_bytes: 0,
            digest: [0_u8; 32],
        },
    )
}

/// A reply that ended with `stop` and named `calls`.
pub fn reply(stop: ModelStopReason, calls: Vec<ModelToolCall>) -> ModelReply {
    ModelReply {
        stop,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 1,
        tool_calls: calls,
    }
}

/// A whole-document call on the read-oriented tool.
pub fn read_call(_handle: ModelHandle) -> ModelToolCall {
    ModelToolCall::new("browser.dom.read", vec![])
}

/// An in-tab navigation to `address`. No node: the address is the operand.
pub fn navigate_call(address: &str) -> ModelToolCall {
    ModelToolCall::new(
        "browser.navigate",
        vec![SuppliedArgument::new(
            "address",
            ArgumentValue::Address(address.to_owned()),
        )],
    )
}

/// A loop-local search for deferred tools matching `query`.
pub fn search_tools_call(query: &str) -> ModelToolCall {
    ModelToolCall::new(
        task_engine::SEARCH_TOOLS,
        vec![SuppliedArgument::new(
            "query",
            ArgumentValue::Text(query.to_owned()),
        )],
    )
}

/// A loop-local request to load the deferred tool `name`.
pub fn activate_tool_call(name: &str) -> ModelToolCall {
    ModelToolCall::new(
        task_engine::ACTIVATE_TOOL,
        vec![SuppliedArgument::new(
            "name",
            ArgumentValue::Text(name.to_owned()),
        )],
    )
}

/// A loop-local nested pass with `goal`.
pub fn spawn_run_call(goal: &str) -> ModelToolCall {
    ModelToolCall::new(
        task_engine::SPAWN_RUN,
        vec![SuppliedArgument::new(
            "goal",
            ArgumentValue::Text(goal.to_owned()),
        )],
    )
}

/// The call this task is waiting on, or a name no turn carries.
///
/// The absent case matters to a generated script: it drives the cell where a
/// completion names a call the reducer is not holding, which has to refuse
/// rather than be attributed to whatever turn is nearest.
fn outstanding_call(fixture: &Fixture) -> task_engine::ModelCallId {
    fixture.reducer.model_turn().map_or_else(
        || task_engine::ModelCallId::new("model-call-absent"),
        |turn| turn.call_id().clone(),
    )
}

/// Ask for the turn this task would derive the identity of.
pub fn request_turn(fixture: &Fixture) -> Command {
    Command::RequestModelTurn {
        call_id: fixture.reducer.next_model_call_id(),
    }
}

/// Record a reply to whatever call the task is holding.
pub fn record_turn(fixture: &Fixture) -> Command {
    Command::RecordModelTurn {
        call_id: outstanding_call(fixture),
        digest: Box::new(super::turn_digest()),
    }
}

/// Record that the call the task is holding produced no readable reply.
pub fn record_gap(fixture: &Fixture, step: usize) -> Command {
    Command::RecordModelTurnGap {
        call_id: outstanding_call(fixture),
        gap: if step.is_multiple_of(2) {
            task_engine::TurnGap::Cancelled
        } else {
            task_engine::TurnGap::OutcomeUnknown
        },
    }
}

/// A running task that has not yet started a turn.
pub fn running() -> Fixture {
    super::in_state(task_engine::TaskState::Running, false)
}

/// A running task holding one recorded reply of `stop` carrying `calls`.
pub fn with_recorded_turn(
    stop: ModelStopReason,
    calls: Vec<ModelToolCall>,
) -> (Fixture, TurnResidency) {
    let (page, _) = page();
    with_recorded_turn_on(page, stop, calls)
}

/// [`with_recorded_turn`], composed from an exact projection.
pub fn with_recorded_turn_on(
    page: TurnPage,
    stop: ModelStopReason,
    calls: Vec<ModelToolCall>,
) -> (Fixture, TurnResidency) {
    let mut fixture = running();
    let residency = record_turn_in(&mut fixture, page, stop, calls);
    (fixture, residency)
}

/// Records one reply of `stop` carrying `calls` against `fixture`, composed
/// from `page`, and answers the residency it left.
pub fn record_turn_in(
    fixture: &mut Fixture,
    page: TurnPage,
    stop: ModelStopReason,
    calls: Vec<ModelToolCall>,
) -> TurnResidency {
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let residency = TurnResidency::read(call_id, page, reply(stop, calls))
        .unwrap_or_else(|| unreachable!("the fixture page is explicitly readable"));
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });
    residency
}
