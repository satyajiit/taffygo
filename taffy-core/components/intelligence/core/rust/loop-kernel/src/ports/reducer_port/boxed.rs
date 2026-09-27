// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Transparent owned delegation for dynamic task-engine ports.
//!
//! # Every method is forwarded by hand, and a missing one is silent
//!
//! `TaskEnginePort` gives most of its methods a default, so a method left out
//! of this file compiles, links, and answers the default for every boxed task
//! — which is every task in the shipping runtime. Nothing fails and no test
//! that constructs a reducer directly can see it.
//!
//! It has already happened once: `is_terminal` and `updated_at_utc_millis`
//! were added for decision 0149 and implemented on `Reducer`, the host suite
//! passed, the Chromium build passed, and the phone showed exactly the stale
//! pill the change was written to remove — because the runtime holds
//! `Box<dyn TaskEnginePort>` and was reading `false` and `0` from here.
//!
//! **Adding a method to `TaskEnginePort` is not finished until it is forwarded
//! below.**

use bip_types::identity::{ActionId, TaskId};
use task_engine::{Accepted, CommandEnvelope, Refusal, TaskJournal, WorkspaceId};

use super::super::{
    ActionEffectFacts, BuiltinSkillBindingFacts, ModelTurnFacts, TaskDiscoveryAuthorityFacts,
    TaskEnginePort, TaskViewFacts, TaskViewFactsError,
};

impl<T> TaskEnginePort for Box<T>
where
    T: TaskEnginePort + ?Sized,
{
    fn task_id(&self) -> &TaskId {
        self.as_ref().task_id()
    }

    fn workspace_id(&self) -> Option<&WorkspaceId> {
        self.as_ref().workspace_id()
    }

    fn revision(&self) -> u64 {
        self.as_ref().revision()
    }

    fn updated_at_utc_millis(&self) -> u64 {
        self.as_ref().updated_at_utc_millis()
    }

    fn is_terminal(&self) -> bool {
        self.as_ref().is_terminal()
    }

    fn capability_policy_version(&self) -> task_engine::PolicyVersion {
        self.as_ref().capability_policy_version()
    }

    fn action_effect_facts(&self, action_id: &ActionId) -> Option<ActionEffectFacts> {
        self.as_ref().action_effect_facts(action_id)
    }

    fn discovery_authority_facts(&self) -> Option<TaskDiscoveryAuthorityFacts> {
        self.as_ref().discovery_authority_facts()
    }

    fn journal(&self) -> &TaskJournal {
        self.as_ref().journal()
    }

    fn view_facts(&self) -> Result<TaskViewFacts, TaskViewFactsError> {
        self.as_ref().view_facts()
    }

    fn next_reviewed_command(
        &self,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::WorkflowError> {
        self.as_ref().next_reviewed_command(digest)
    }

    fn next_procedure_command(
        &self,
        procedure: &procedure_engine::Procedure,
        observed: procedure_engine::ObservedFields<'_>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, procedure_engine::ReplayRefusal> {
        self.as_ref()
            .next_procedure_command(procedure, observed, digest)
    }

    fn next_procedure_command_with_page(
        &self,
        procedure: &procedure_engine::Procedure,
        observed: procedure_engine::ObservedFields<'_>,
        page: Option<procedure_engine::ReplayPage<'_>>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, procedure_engine::ReplayRefusal> {
        self.as_ref()
            .next_procedure_command_with_page(procedure, observed, page, digest)
    }

    fn browser_session_id(&self) -> Option<&task_engine::BrowserSessionId> {
        self.as_ref().browser_session_id()
    }

    fn skill_version_id(&self) -> Option<&str> {
        self.as_ref().skill_version_id()
    }

    fn builtin_skill_reference(&self) -> Option<task_engine::BuiltinSkillReference> {
        self.as_ref().builtin_skill_reference()
    }

    fn builtin_skill_binding_facts(&self) -> Option<BuiltinSkillBindingFacts> {
        self.as_ref().builtin_skill_binding_facts()
    }

    fn library_refresh_context(&self) -> Option<task_engine::LibraryRefreshContext> {
        self.as_ref().library_refresh_context()
    }

    fn model_turn_facts(&self) -> ModelTurnFacts {
        self.as_ref().model_turn_facts()
    }

    fn refused_on_sight(
        &self,
        residency: &task_engine::TurnResidency,
    ) -> Vec<(u32, task_engine::NotAttempted)> {
        self.as_ref().refused_on_sight(residency)
    }

    fn model_turn_in_flight(&self) -> Option<String> {
        self.as_ref().model_turn_in_flight()
    }

    fn pre_model_observation_sources(&self) -> Vec<task_engine::ConsentedSource> {
        self.as_ref().pre_model_observation_sources()
    }

    fn next_pre_model_observation(
        &self,
        live_observations: &[task_engine::LiveSourceObservation],
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<task_engine::PreModelObservation, task_engine::AgentError> {
        self.as_ref()
            .next_pre_model_observation(live_observations, digest)
    }

    fn next_agent_command(
        &self,
        residency: Option<&task_engine::TurnResidency>,
        digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::AgentError> {
        self.as_ref().next_agent_command(residency, digest)
    }

    fn pending_loop_call<'a>(
        &self,
        residency: &'a task_engine::TurnResidency,
    ) -> Option<(
        u32,
        &'a task_engine::ModelToolCall,
        &'static task_engine::ToolEntry,
    )> {
        self.as_ref().pending_loop_call(residency)
    }

    fn apply(&mut self, envelope: CommandEnvelope) -> Result<Accepted, Refusal> {
        self.as_mut().apply(envelope)
    }
}
