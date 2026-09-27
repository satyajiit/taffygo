// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One request for values and the fills that follow it (decision 0238).
//!
//! The page is the myAadhaar download form as the snapshot numbers it, on the
//! errand's discovery tab: a labelled line that takes no value, the identity
//! number, a name field anybody could type, and the CAPTCHA's answer.

use bip_types::identity::{
    ActionId, DispatchId, FrameId, GraphRevision, MonotonicMillis, NodeHandle, Origin, OriginKind,
    PageEpoch, SemanticNodeId, TabId,
};
use bip_types::snapshot::Sensitivity;
use bip_types::ActionResultCode;
use task_engine::action::{ActionIntent, ActionOutcome, BrowserIntent};
use task_engine::agent::{ModelToolCall, TurnPage, TurnResidency};
use task_engine::authority::{Authorization, CapabilityId, ProposalDecision};
use task_engine::{
    held_value_fill_key, ArgumentValue, CallVerdict, Command, FieldNodeIds, FieldValueAskOutcome,
    FieldValueRequestId, HandleTable, JournalEntry, ManualClock, ModelHandle, ModelStopReason,
    PageReadability, Reducer, RenderShape, SequentialIds, SuppliedArgument, SuppliedValueCount,
    TaskJournal, ValueTarget,
};

use super::agent::{record_turn_in, reply, Digest};
use super::errand::{errand_seed, prepared_errand_admitting_each, DISCOVERY_TAB};
use super::{Fixture, TestReducer};

/// The two tools the errand admits beside its defaults.
pub const TOOLS: [&str; 2] = ["user.request_values", "browser.form.fill"];

/// The page and the number each of its lines was offered under.
pub struct AadhaarPage {
    pub page: TurnPage,
    pub region: ModelHandle,
    pub id: ModelHandle,
    pub name: ModelHandle,
    pub captcha: ModelHandle,
}

fn node(node_id: &str) -> NodeHandle {
    NodeHandle::new(
        TabId::new(DISCOVERY_TAB),
        FrameId::new("frame_1"),
        PageEpoch::new("epoch_1"),
        GraphRevision(9),
        SemanticNodeId::new(node_id),
        Origin {
            kind: OriginKind::Tuple,
            serialization: Some("https://myaadhaar.example".to_owned()),
            opaque_id: None,
        },
    )
}

pub fn aadhaar_page() -> AadhaarPage {
    let mut handles = HandleTable::new();
    let mut issue = |id: &str, target: ValueTarget, sensitivity: Sensitivity| {
        handles
            .issue_classified(node(id), target, sensitivity)
            .unwrap_or_else(|| unreachable!("an empty table issues numbers"))
    };
    let region = issue("form-region", ValueTarget::Container, Sensitivity::Identity);
    let id = issue("aadhaar-number", ValueTarget::Field, Sensitivity::Identity);
    let name = issue("full-name", ValueTarget::Field, Sensitivity::NotSensitive);
    let captcha = issue(
        "captcha-answer",
        ValueTarget::Field,
        Sensitivity::ChallengeResponse,
    );
    AadhaarPage {
        page: TurnPage::new(
            TabId::new(DISCOVERY_TAB),
            handles,
            RenderShape {
                readability: PageReadability::Readable,
                offered_nodes: 4,
                omitted_nodes: 0,
                unreadable_nodes: 0,
                unreadable_text_bytes: 0,
                digest: [3_u8; 32],
            },
        )
        .with_discovery_tab(Some(TabId::new(DISCOVERY_TAB))),
        region,
        id,
        name,
        captcha,
    }
}

/// An errand on the discovery tab that may ask for values and fill fields.
pub fn errand() -> Fixture {
    prepared_errand_admitting_each(4, &TOOLS)
}

/// The command the model's one `user.request_values` naming `handle` becomes.
pub fn request_naming(fixture: &mut Fixture, handle: ModelHandle) -> (Command, TurnResidency) {
    let ask = ModelToolCall::new(
        "user.request_values",
        vec![SuppliedArgument::new(
            "form",
            ArgumentValue::Handle(handle.value()),
        )],
    );
    let residency = record_turn_in(
        fixture,
        aadhaar_page().page,
        ModelStopReason::ToolCall,
        vec![ask],
    );
    let command =
        next(fixture, &residency).unwrap_or_else(|| unreachable!("the ask is the next command"));
    (command, residency)
}

/// The request a `RequestFieldValues` command names.
pub fn request_of(command: &Command) -> FieldValueRequestId {
    let Command::RequestFieldValues { request_id, .. } = command else {
        unreachable!("expected a request for values, got {command:?}");
    };
    request_id.clone()
}

/// The companions a `RequestFieldValues` command carries, as plain ids.
pub fn companions_of(command: &Command) -> Vec<String> {
    let Command::RequestFieldValues {
        companion_node_ids, ..
    } = command
    else {
        unreachable!("expected a request for values, got {command:?}");
    };
    companion_node_ids
        .as_slice()
        .iter()
        .map(|id| id.0.clone())
        .collect()
}

pub fn ids(values: &[&str]) -> FieldNodeIds {
    FieldNodeIds::new(values.iter().map(|id| SemanticNodeId::new(*id)).collect())
        .unwrap_or_else(|_| unreachable!("a bounded distinct list"))
}

pub fn two_values() -> SuppliedValueCount {
    SuppliedValueCount::new(2).unwrap_or_else(|_| unreachable!("two values"))
}

/// An errand that asked about the identity number and was answered with two
/// values, minted for the identity number and the CAPTCHA, in that order.
pub fn answered_with_two_values() -> (Fixture, TurnResidency, FieldValueRequestId) {
    let mut fixture = errand();
    let (request, residency) = request_naming(&mut fixture, aadhaar_page().id);
    let request_id = request_of(&request);
    fixture.must_apply(request);
    fixture.must_apply(Command::SupplyFieldValues {
        request_id: request_id.clone(),
        supplied: two_values(),
        outcome: Some(FieldValueAskOutcome::Answered),
        field_node_ids: Some(ids(&["aadhaar-number", "captcha-answer"])),
    });
    (fixture, residency, request_id)
}

pub fn next(fixture: &Fixture, residency: &TurnResidency) -> Option<Command> {
    fixture
        .reducer
        .next_agent_command(Some(residency), &Digest)
        .unwrap_or_else(|error| unreachable!("{error:?}"))
}

/// Whether `command` proposes exactly the fill a model's
/// `browser.form.fill { field, value_from: index }` would have produced, under
/// the task's own key and drawing on no budget.
pub fn is_fill_of(
    command: Option<&Command>,
    request: &FieldValueRequestId,
    index: u32,
    field: &str,
) -> bool {
    let Some(Command::ProposeAction(proposal)) = command else {
        return false;
    };
    proposal.idempotency_key == held_value_fill_key(request, index)
        && proposal.budget_draw.is_none()
        && proposal.intent()
            == &ActionIntent::Browser(BrowserIntent::FormFill {
                tab: TabId::new(DISCOVERY_TAB),
                field: SemanticNodeId::new(field),
                value_request: request.clone(),
                value_from: index,
            })
}

/// Whether the task ever proposed the fill of `index`.
pub fn proposed_fill(fixture: &Fixture, request: &FieldValueRequestId, index: u32) -> bool {
    let key = held_value_fill_key(request, index);
    fixture
        .reducer
        .actions()
        .any(|action| action.proposal().idempotency_key == key)
}

/// Applies a fill proposal and names the action it minted.
pub fn apply_fill(fixture: &mut Fixture, command: Command) -> ActionId {
    let Command::ProposeAction(proposal) = &command else {
        unreachable!("expected a proposal, got {command:?}");
    };
    let key = proposal.idempotency_key.clone();
    fixture.must_apply(command);
    fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key == key)
        .map_or_else(
            || unreachable!("the proposal minted one action"),
            |action| action.action_id().clone(),
        )
}

/// Authorizes the fill and records `code` as the browser's outcome for it.
pub fn answer_fill(fixture: &mut Fixture, action_id: ActionId, index: u32, code: ActionResultCode) {
    let dispatch = DispatchId::new(format!("dispatch-fill-{index}"));
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new(format!("capability-fill-{index}")),
        })),
        dispatch_id: Some(dispatch.clone()),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code,
            dispatch_id: Some(dispatch),
            observed_at: MonotonicMillis(5_000),
            observation: None,
            discovered_source: None,
        }),
    });
}

pub fn settle_fill(fixture: &mut Fixture, command: Command, index: u32, code: ActionResultCode) {
    let action_id = apply_fill(fixture, command);
    answer_fill(fixture, action_id, index, code);
}

/// What the reducer does with a model turn whose one call is
/// `browser.form.fill { field, value_from }`, asked once the placing is over.
pub fn verdict_of_model_fill(
    fixture: &mut Fixture,
    residency: &TurnResidency,
    field: ModelHandle,
    value_from: u32,
) -> Option<CallVerdict> {
    let Some(Command::RequestModelTurn { call_id }) = next(fixture, residency) else {
        unreachable!("the model is asked once the placing is over");
    };
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let fill = ModelToolCall::new(
        "browser.form.fill",
        vec![
            SuppliedArgument::new("field", ArgumentValue::Handle(field.value())),
            SuppliedArgument::new("value_from", ArgumentValue::SuppliedValue(value_from)),
        ],
    );
    let turn = TurnResidency::read(
        call_id,
        aadhaar_page().page,
        reply(ModelStopReason::ToolCall, vec![fill]),
    )
    .unwrap_or_else(|| unreachable!("a readable reply"));
    fixture
        .reducer
        .turn_dispositions(&turn)
        .first()
        .map(|disposition| disposition.verdict)
}

/// The journal as the core's durable store gives it back: every command, with
/// the two lists of fields decision 0238 adds dropped, because the persisted
/// format does not carry them.
fn as_restored(journal: &TaskJournal) -> TaskJournal {
    let entries: Vec<JournalEntry> = journal
        .entries()
        .iter()
        .cloned()
        .map(|mut entry| {
            if let JournalEntry::Command(record) = &mut entry {
                match &mut record.envelope.command {
                    Command::RequestFieldValues {
                        companion_node_ids, ..
                    } => *companion_node_ids = FieldNodeIds::none(),
                    Command::SupplyFieldValues {
                        outcome,
                        field_node_ids,
                        ..
                    } => {
                        *outcome = None;
                        *field_node_ids = None;
                    }
                    _ => {}
                }
            }
            entry
        })
        .collect();
    TaskJournal::from_entries(&entries)
        .unwrap_or_else(|_| unreachable!("the same journal, less two lists"))
}

/// The errand rebuilt from what the durable store would give back.
pub fn rebuilt_from_the_store(fixture: &Fixture) -> TestReducer {
    let mut seed = errand_seed();
    seed.snapshot
        .tool_allowlist
        .extend(TOOLS.iter().map(|tool| (*tool).to_owned()));
    let (rebuilt, _) = Reducer::replay(
        seed,
        super::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        &as_restored(fixture.reducer.journal()),
    )
    .unwrap_or_else(|error| unreachable!("{error:?}"));
    rebuilt
}
