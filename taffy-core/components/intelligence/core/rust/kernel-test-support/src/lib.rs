// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Scripted host-test doubles for the core service's ports.
//!
//! One double per port, substitutable without a build feature — the port
//! charter in `core-runtime/src/ports.rs` is what makes this crate possible,
//! and this crate is where the doubles live so three test modules stop
//! carrying private copies of one fake. Dev-dependency only: nothing here
//! ships, and no GN target may ever name this crate.

use bip_types::identity::{ActionId, TaskId};
use core_runtime::account::{DigestError, Sha256Port};
use core_runtime::context::{TaskTranscript, TranscriptBudget};
use core_runtime::ports::{
    ActionEffectFacts, ModelTurnFacts, TaskEnginePort, TaskViewFacts, TaskViewFactsError,
};
use task_engine::{
    Accepted, AgentError, Command, CommandEnvelope, Milestone, PolicyVersion, Refusal,
    RefusalReason, TaskJournal, TaskState, TurnResidency, WorkflowDigest, WorkflowError,
    WorkspaceId,
};

/// A task double whose scheduler answers are scripted by the test.
///
/// The double answers the walk's read surface and refuses every `apply`, so a
/// test drives scheduling decisions without constructing a durable reducer.
/// Fields are public on purpose: a test states its scenario as a literal.
#[derive(Debug)]
pub struct ScriptedTask {
    pub id: TaskId,
    pub revision: u64,
    /// The consented provider route the task carries, if any.
    pub route: Option<&'static str>,
    /// What `next_reviewed_command` answers.
    pub reviewed: Option<Command>,
    /// What `next_agent_command` answers.
    pub agent: Option<Command>,
    /// What `next_procedure_command` answers.
    pub procedure: Option<Command>,
    pub journal: TaskJournal,
    /// The conversation the double carries, when a test needs a real one.
    /// `None` composes the empty default.
    pub transcript: Option<TaskTranscript>,
}

impl ScriptedTask {
    /// A double with no route and no scripted commands.
    #[must_use]
    pub fn new(id: TaskId, revision: u64) -> Self {
        Self {
            id,
            revision,
            route: None,
            reviewed: None,
            agent: None,
            procedure: None,
            journal: TaskJournal::new(),
            transcript: None,
        }
    }
}

impl TaskEnginePort for ScriptedTask {
    fn task_id(&self) -> &TaskId {
        &self.id
    }

    fn workspace_id(&self) -> Option<&WorkspaceId> {
        None
    }

    fn revision(&self) -> u64 {
        self.revision
    }

    fn updated_at_utc_millis(&self) -> u64 {
        self.revision
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
        Err(TaskViewFactsError::MissingTerminalFailure)
    }

    fn next_reviewed_command(
        &self,
        _digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, WorkflowError> {
        Ok(self.reviewed.clone())
    }

    fn next_procedure_command(
        &self,
        _procedure: &procedure_engine::Procedure,
        _observed: procedure_engine::ObservedFields<'_>,
        _digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, procedure_engine::ReplayRefusal> {
        Ok(self.procedure.clone())
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
            transcript: self.transcript.clone().unwrap_or_else(|| {
                TaskTranscript::new(
                    "redacted test goal".to_owned(),
                    Vec::new(),
                    TranscriptBudget::default(),
                )
            }),
            provider_route_id: self.route.map(str::to_owned),
            tool_allowlist: Vec::new(),
            milestone: Milestone::M3,
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
        Ok(self.agent.clone())
    }

    fn apply(&mut self, envelope: CommandEnvelope) -> Result<Accepted, Refusal> {
        Err(Refusal {
            reason: RefusalReason::NotRunning,
            state: TaskState::Running,
            command: envelope.kind(),
            revision: self.revision,
        })
    }
}

/// A deterministic stand-in for the browser's SHA-256 adapter.
///
/// Not a hash — a fold with the mixing a test needs to tell two inputs apart.
/// Production strength comes from the real adapter; a host test needs only
/// determinism and distinctness.
#[derive(Clone, Copy, Debug, Default)]
pub struct ReferenceDigest;

impl Sha256Port for ReferenceDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        let mut output = [0_u8; 32];
        for (index, byte) in input.iter().copied().enumerate() {
            let slot = index % output.len();
            if let Some(cell) = output.get_mut(slot) {
                *cell = cell.rotate_left(1) ^ byte;
            }
        }
        Ok(output)
    }
}
