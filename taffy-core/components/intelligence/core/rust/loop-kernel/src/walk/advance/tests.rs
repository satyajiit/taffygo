// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::identity::{ActionId, TabId, TaskId};
use task_engine::{
    Accepted, AgentError, ArgumentValue, Command, CommandEnvelope, FailureReason, HandleTable,
    Milestone, ModelCallId, ModelReply, ModelStopReason, ModelToolCall, PolicyVersion, Refusal,
    RefusalReason, RenderShape, SuppliedArgument, TaskJournal, TaskState, ToolEntry, TurnPage,
    TurnResidency, TurnUsage, WorkflowDigest, WorkflowError, WorkspaceId,
};

use crate::context::{TaskTranscript, TranscriptBudget};
use crate::ports::{
    ActionEffectFacts, ModelTurnFacts, TaskEnginePort, TaskViewFacts, TaskViewFactsError,
};
use crate::state::LoopState;

use super::{artifact_failure_command, settle_loop_calls, settle_loop_calls_with_configuration};

mod discovery;

fn image_discovery_residency(call_id: &str) -> TurnResidency {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![
            ModelToolCall::new(
                task_engine::SEARCH_TOOLS,
                vec![SuppliedArgument::new(
                    "query",
                    ArgumentValue::Text("image".to_owned()),
                )],
            ),
            ModelToolCall::new(
                task_engine::ACTIVATE_TOOL,
                vec![SuppliedArgument::new(
                    "name",
                    ArgumentValue::Text("page.images.describe".to_owned()),
                )],
            ),
        ],
    };
    let page = TurnPage::new(
        TabId::new("tab-1"),
        HandleTable::new(),
        RenderShape::empty([0; 32]),
    );
    TurnResidency::read(ModelCallId::new(call_id), page, reply)
        .unwrap_or_else(|| unreachable!("two bounded calls are resident"))
}

#[test]
fn artifact_preflight_refusal_is_a_replay_stable_terminal_not_a_core_error() {
    let first = artifact_failure_command(
        7,
        "artifact-turn-1-call-0",
        FailureReason::BudgetExhausted,
        3,
        1_000,
    )
    .unwrap_or_else(|error| unreachable!("bounded identities are valid: {error:?}"));
    let again = artifact_failure_command(
        7,
        "artifact-turn-1-call-0",
        FailureReason::BudgetExhausted,
        3,
        1_000,
    )
    .unwrap_or_else(|error| unreachable!("the same input is valid: {error:?}"));

    assert_eq!(first.envelope, again.envelope);
    assert_eq!(first.operation, again.operation);
    assert_eq!(
        first.envelope.command,
        Command::FailTask {
            reason: FailureReason::BudgetExhausted,
        }
    );
    assert_eq!(first.operation.task_revision, 7);
    assert_eq!(first.operation.service_generation, 3);
}

#[test]
fn the_walk_settles_the_native_table_and_keeps_its_csv_on_residency() {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![ModelToolCall::new(
            task_engine::TABLE_RESHAPE_TOOL,
            vec![
                SuppliedArgument::new(
                    "rows",
                    ArgumentValue::Text("name,value\r\nb,2\r\na,1\r\n".to_owned()),
                ),
                SuppliedArgument::new(
                    "recipe",
                    ArgumentValue::Text(
                        r#"{"operations":[{"op":"order","by":[{"column":"value","direction":"ascending","mode":"decimal"}]}]}"#
                            .to_owned(),
                    ),
                ),
            ],
        )],
    };
    let page = TurnPage::new(
        TabId::new("tab-1"),
        HandleTable::new(),
        RenderShape::empty([0; 32]),
    );
    let residency = TurnResidency::read(ModelCallId::new("model-task-1"), page, reply)
        .unwrap_or_else(|| unreachable!("one bounded call is resident"));
    let mut state = LoopState {
        residency: Some(residency),
        ..LoopState::default()
    };

    settle_loop_calls(&LoopTask::new(), &mut state);

    let residency = state
        .residency
        .as_ref()
        .unwrap_or_else(|| unreachable!("the residency remains live"));
    assert_eq!(
        residency.loop_outcome(0),
        Some(&task_engine::LoopOutcome::TableReshaped {
            rows: 2,
            columns: 2,
        })
    );
    assert_eq!(
        residency.loop_result(0),
        Some(
            [
                "reshaped 2 rows and 2 columns".to_owned(),
                "name,value\r\na,1\r\nb,2\r\n".to_owned(),
            ]
            .as_slice()
        )
    );
}

#[test]
fn loop_settlement_cannot_see_or_activate_a_name_review_excluded() {
    let mut state = LoopState {
        residency: Some(image_discovery_residency("model-task-1")),
        ..LoopState::default()
    };
    let task = LoopTask::with_allowlist(&[
        task_engine::SEARCH_TOOLS,
        task_engine::ACTIVATE_TOOL,
        "page.pdf.inspect",
    ]);

    settle_loop_calls(&task, &mut state);

    let residency = state
        .residency
        .as_ref()
        .unwrap_or_else(|| unreachable!("the residency remains live"));
    assert_eq!(
        residency.loop_outcome(0),
        Some(&task_engine::LoopOutcome::Searched { hits: 0 })
    );
    assert_eq!(
        residency.loop_outcome(1),
        Some(&task_engine::LoopOutcome::Activated { name_known: false })
    );
    assert!(residency.loop_result(0).is_none());
    assert!(residency.loop_result(1).is_none());
    assert!(state.activated.is_empty());
}

#[test]
fn live_configuration_narrowing_applies_to_loop_settlement_too() {
    let mut state = LoopState {
        residency: Some(image_discovery_residency("model-task-2")),
        ..LoopState::default()
    };
    let configured = [
        task_engine::SEARCH_TOOLS.to_owned(),
        task_engine::ACTIVATE_TOOL.to_owned(),
        "page.pdf.inspect".to_owned(),
    ];

    settle_loop_calls_with_configuration(&LoopTask::new(), &mut state, Some(&configured));

    let residency = state
        .residency
        .as_ref()
        .unwrap_or_else(|| unreachable!("the residency remains live"));
    assert_eq!(
        residency.loop_outcome(0),
        Some(&task_engine::LoopOutcome::Searched { hits: 0 })
    );
    assert_eq!(
        residency.loop_outcome(1),
        Some(&task_engine::LoopOutcome::Activated { name_known: false })
    );
    assert!(state.activated.is_empty());
}

struct LoopTask {
    id: TaskId,
    journal: TaskJournal,
    tool_allowlist: Vec<String>,
    /// The reducer state the view facts report; `None` reports no facts.
    state: Option<TaskState>,
}

impl LoopTask {
    fn new() -> Self {
        Self {
            id: TaskId::new("task-1"),
            journal: TaskJournal::new(),
            tool_allowlist: Vec::new(),
            state: None,
        }
    }

    fn with_allowlist(names: &[&str]) -> Self {
        Self {
            tool_allowlist: names.iter().map(|name| (*name).to_owned()).collect(),
            ..Self::new()
        }
    }
}

impl TaskEnginePort for LoopTask {
    fn task_id(&self) -> &TaskId {
        &self.id
    }

    fn workspace_id(&self) -> Option<&WorkspaceId> {
        None
    }

    fn revision(&self) -> u64 {
        1
    }

    fn updated_at_utc_millis(&self) -> u64 {
        0
    }

    fn is_terminal(&self) -> bool {
        false
    }

    fn capability_policy_version(&self) -> PolicyVersion {
        PolicyVersion(1)
    }

    fn action_effect_facts(&self, _action_id: &ActionId) -> Option<ActionEffectFacts> {
        None
    }

    fn journal(&self) -> &TaskJournal {
        &self.journal
    }

    fn view_facts(&self) -> Result<TaskViewFacts, TaskViewFactsError> {
        let state = self
            .state
            .ok_or(TaskViewFactsError::MissingTerminalFailure)?;
        Ok(TaskViewFacts {
            task_id: self.id.as_str().to_owned(),
            revision: self.revision(),
            state,
            execution_phase: None,
            goal: String::new(),
            template_id: task_engine::TaskTemplateId::WebErrand,
            workspace_id: None,
            plan_progress: None,
            terminal_failure: None,
            pause_cause: None,
            model_attempt: None,
            last_move_refused: false,
            reply_being_reasked: false,
            pending_action: None,
            pending_permission: None,
            accepted_consent: None,
            committed_action_approvals: Vec::new(),
            outcome_unknown_actions: 0,
            artifacts: Vec::new(),
            activity: Vec::new(),
            allowed_controls: Vec::new(),
            pending_handover: None,
            pending_field_values: None,
        })
    }

    fn next_reviewed_command(
        &self,
        _digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        Ok(None)
    }

    fn refused_on_sight(
        &self,
        _residency: &task_engine::TurnResidency,
    ) -> Vec<(u32, task_engine::NotAttempted)> {
        Vec::new()
    }

    fn model_turn_facts(&self) -> ModelTurnFacts {
        ModelTurnFacts {
            task_id: self.id.as_str().to_owned(),
            attempts_started: 1,
            candidate_ordinal: 0,
            can_afford_model_attempt: true,
            transcript: TaskTranscript::new(
                "reshape the table".to_owned(),
                Vec::new(),
                TranscriptBudget::default(),
            ),
            provider_route_id: None,
            tool_allowlist: self.tool_allowlist.clone(),
            milestone: Milestone::M7,
            template_id: task_engine::TaskTemplateId::SummarizeEvidence,
            source_count: 0,
            remaining_new_source_cap: 0,
            empty_page_tab_id: None,
            discovery_tab_id: None,
            persons_pages: task_engine::PersonsPages::default(),
            activated: Vec::new(),
            nested_goal: None,
            thinking: None,
        }
    }

    fn model_turn_in_flight(&self) -> Option<String> {
        None
    }

    fn next_agent_command(
        &self,
        _residency: Option<&TurnResidency>,
        _digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, AgentError> {
        Ok(None)
    }

    fn pending_loop_call<'a>(
        &self,
        residency: &'a TurnResidency,
    ) -> Option<(u32, &'a ModelToolCall, &'static ToolEntry)> {
        let sequence = residency.pending_loop_sequence()?;
        let call = residency.call(sequence)?;
        let entry = task_engine::resolve(&call.tool_name, Milestone::M7).entry()?;
        Some((sequence, call, entry))
    }

    fn apply(&mut self, envelope: CommandEnvelope) -> Result<Accepted, Refusal> {
        Err(Refusal {
            reason: RefusalReason::NotRunning,
            state: TaskState::Running,
            command: envelope.kind(),
            revision: 1,
        })
    }
}

#[test]
fn a_model_call_travels_under_the_long_deadline_and_everything_else_the_short_one() {
    use super::{command_deadline_ms, MODEL_CALL_DEADLINE_MS, WORKFLOW_COMMAND_DEADLINE_MS};
    assert_eq!(
        command_deadline_ms(&Command::RequestModelTurn {
            call_id: ModelCallId::new("model-task-1"),
        }),
        MODEL_CALL_DEADLINE_MS
    );
    assert_eq!(
        command_deadline_ms(&Command::RequestModelAttempt {
            call_id: ModelCallId::new("model-task-1"),
            attempt_ordinal: 1,
            candidate_ordinal: 0,
            kind: task_engine::ModelAttemptKind::Retry,
        }),
        MODEL_CALL_DEADLINE_MS
    );
    assert_eq!(
        command_deadline_ms(&Command::ExecutorStarted),
        WORKFLOW_COMMAND_DEADLINE_MS
    );
}
