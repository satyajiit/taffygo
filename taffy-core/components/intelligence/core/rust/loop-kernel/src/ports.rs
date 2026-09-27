// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Task reducer construction and replay ports.

use bip_types::identity::{ActionId, TaskId};
use task_engine::{
    Accepted, ArtifactCustody, ArtifactId, CommandEnvelope, Refusal, TaskJournal, WorkspaceId,
};

mod browser_facts;
mod factory;
mod facts;
mod models;
mod reducer_port;

pub use factory::{
    apply_initial_consent, InitialConsentAdmission, OpenedTaskEngine, ReducerFactory,
    TaskEngineFactory, TaskEngineLoad, TaskIdSourceFactory,
};
pub use facts::{
    AcceptedTaskConsentFacts, ActionEffectFacts, BuiltinSkillBindingFacts,
    CommittedActionApprovalFacts, ModelTurnFacts, PendingActionFacts, PendingPermissionFacts,
    PlanProgressFacts, PortError, TaskActivityFacts, TaskArtifactFacts,
    TaskDiscoveryAuthorityFacts, TaskIdEntropy, TaskViewFacts, TaskViewFactsError,
    TASK_ID_ENTROPY_BYTES,
};
pub use models::ModelRouterPort;

#[cfg(test)]
pub(super) use reducer_port::{
    classify_action_state, is_projected_finished_step, ActionProjectionClass,
};

/// The deterministic reducer surface the composition runtime needs.
pub trait TaskEnginePort {
    /// Identity of the one aggregate this runtime drives.
    fn task_id(&self) -> &TaskId;

    /// Browser-manager incarnation frozen into this task's accepted context.
    fn browser_session_id(&self) -> Option<&task_engine::BrowserSessionId> {
        None
    }

    /// Optional durable workspace identity.
    fn workspace_id(&self) -> Option<&WorkspaceId>;

    /// Current durable aggregate revision.
    fn revision(&self) -> u64;

    /// When this aggregate last changed, in milliseconds since the Unix epoch.
    ///
    /// The one fact that orders tasks against each other. A revision counts a
    /// single task's own transitions and says nothing across two of them, and
    /// the runtime holds its sessions in a map keyed by identity, so without
    /// this the "most recent task" is whichever identity sorts first — which is
    /// how a bar came to speak for a task that had ended the day before.
    ///
    /// **Required, unlike most of this trait.** It was written with a default
    /// of zero and the shipping runtime read that default for every task, from
    /// the hand-written `Box<T>` forward that had not been extended — the
    /// change compiled, the host suite passed, and the phone was unmoved. A
    /// default here is a *wrong* answer rather than an absent one, so there is
    /// none to fall through to.
    fn updated_at_utc_millis(&self) -> u64;

    /// Whether the task has ended, in any of the four ways it can.
    ///
    /// Required for the reason [`Self::updated_at_utc_millis`] gives: a task
    /// silently reported as still going is a task nothing ever lets go of.
    fn is_terminal(&self) -> bool;

    /// Trusted owner that can supply one accepted artifact's export bytes.
    fn artifact_custody(&self, _artifact_id: &ArtifactId) -> Option<ArtifactCustody> {
        None
    }

    /// Policy bundle frozen into this task at creation.
    fn capability_policy_version(&self) -> task_engine::PolicyVersion;

    /// Exact reducer-held action proposal and durable authority correlation.
    fn action_effect_facts(&self, action_id: &ActionId) -> Option<ActionEffectFacts>;

    /// Exact discovery authority while a zero-source errand is still in its
    /// browser-owned bootstrap tab. Any accepted source closes this context.
    fn discovery_authority_facts(&self) -> Option<TaskDiscoveryAuthorityFacts> {
        None
    }

    /// Complete append-only journal owned by this reducer.
    fn journal(&self) -> &TaskJournal;

    /// Complete immutable UI projection facts from the canonical aggregate.
    fn view_facts(&self) -> Result<TaskViewFacts, TaskViewFactsError>;

    /// Computes the next command for the one reviewed no-model workflow.
    ///
    /// The returned command is not applied here. The profile runtime wraps it
    /// in a replay-stable envelope and sends it through the normal two-phase
    /// browser-owned storage commit before any effect may be published.
    fn next_reviewed_command(
        &self,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::WorkflowError>;

    /// Computes the next command for an explicitly selected saved procedure.
    ///
    /// This is the same reducer and submission path as the other schedulers;
    /// only the pure decision table differs. Unknown field purposes are passed
    /// explicitly so fills are handed back to the person, never guessed.
    fn next_procedure_command(
        &self,
        procedure: &procedure_engine::Procedure,
        observed: procedure_engine::ObservedFields<'_>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, procedure_engine::ReplayRefusal> {
        let _ = (procedure, observed, digest);
        Err(procedure_engine::ReplayRefusal::NotRunnable)
    }

    /// Replays over one transient, freshly observed page. Implementations
    /// without page-aware replay preserve their original closed behavior.
    fn next_procedure_command_with_page(
        &self,
        procedure: &procedure_engine::Procedure,
        observed: procedure_engine::ObservedFields<'_>,
        page: Option<procedure_engine::ReplayPage<'_>>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, procedure_engine::ReplayRefusal> {
        let _ = page;
        self.next_procedure_command(procedure, observed, digest)
    }

    /// Immutable selected procedure version, when this task has one.
    fn skill_version_id(&self) -> Option<&str> {
        None
    }

    /// Immutable compiled built-in reference, when this task has one.
    fn builtin_skill_reference(&self) -> Option<task_engine::BuiltinSkillReference> {
        None
    }

    /// Durable compiled built-in binding and the start facts it constrained.
    fn builtin_skill_binding_facts(&self) -> Option<BuiltinSkillBindingFacts> {
        None
    }

    /// Exact approved Library refresh context, when this is the reviewed
    /// provider-free revisit workflow.
    fn library_refresh_context(&self) -> Option<task_engine::LibraryRefreshContext> {
        None
    }

    /// The immutable task facts one model turn is composed from.
    ///
    /// The transcript is the one field carrying something a person wrote, and
    /// it is here because a turn has to be composed from it. Nothing else on
    /// this record is content: a route identity, a name list and a milestone.
    ///
    /// Pure, like every other reader on this trait. The conversation is
    /// rebuilt from the reducer's own durable records rather than remembered,
    /// which is what keeps turn *n + 1* the same question after a replay as it
    /// was before one.
    fn model_turn_facts(&self) -> ModelTurnFacts;

    /// Which calls of `residency`'s reply the reducer refuses on sight, and
    /// why, as `Reducer::turn_dispositions` answers for the task as it stands.
    ///
    /// A refused call never becomes an action, so the journal-built
    /// transcript never shows it, and a model that is not told its call was
    /// refused makes the same call again: a phone spent ten turns on
    /// 2026-09-18 re-naming one number the page no longer held. The composer
    /// remembers these beside the turn so the next request can say so.
    ///
    /// **Required**, for the reason [`Self::updated_at_utc_millis`] gives: a
    /// default of "none refused" would be a wrong answer, not an absent one.
    fn refused_on_sight(
        &self,
        residency: &task_engine::TurnResidency,
    ) -> Vec<(u32, task_engine::NotAttempted)>;

    /// The call the browser is currently holding for this task, if any.
    ///
    /// Correlation and nothing else. A completion naming a different call
    /// answers a turn this task is not waiting on, and attributing it would
    /// record one turn's reply against another turn's request.
    fn model_turn_in_flight(&self) -> Option<String>;

    /// The exact sources a pre-model whole-document pass must observe.
    ///
    /// Test doubles that exercise unrelated schedulers may omit it. The
    /// canonical reducer returns the current durable sources still included
    /// in scope, including verified discoveries; its decision method below
    /// performs the full durable configuration check.
    fn pre_model_observation_sources(&self) -> Vec<task_engine::ConsentedSource> {
        Vec::new()
    }

    /// Decides the transient multi-source prerequisite before a paid turn.
    fn next_pre_model_observation(
        &self,
        _live_observations: &[task_engine::LiveSourceObservation],
        _digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<task_engine::PreModelObservation, task_engine::AgentError> {
        // A neutral default keeps port doubles about other schedulers small.
        // Shipping reducers override this; no browser task is a port double.
        Ok(task_engine::PreModelObservation::Ready)
    }

    /// Computes the next command for the assistant loop.
    ///
    /// Pure, like [`Self::next_reviewed_command`], and not applied here.
    /// `residency` is the turn's transient half — the reply and the projection
    /// it answered — and it is an argument rather than reducer state because
    /// it is not durable: the journal holds the shape of a turn and never its
    /// content, so a replay reconstructs the state this reads without
    /// reconstructing any prose.
    fn next_agent_command(
        &self,
        residency: Option<&task_engine::TurnResidency>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::AgentError>;

    /// The next loop-local call the walk is waiting on, if any.
    ///
    /// Default none: tests that do not drive the assistant table have no loop
    /// tools to settle.
    fn pending_loop_call<'a>(
        &self,
        _residency: &'a task_engine::TurnResidency,
    ) -> Option<(
        u32,
        &'a task_engine::ModelToolCall,
        &'static task_engine::ToolEntry,
    )> {
        None
    }

    /// Applies one replayable command.
    fn apply(&mut self, envelope: CommandEnvelope) -> Result<Accepted, Refusal>;
}

#[cfg(test)]
mod tests;
