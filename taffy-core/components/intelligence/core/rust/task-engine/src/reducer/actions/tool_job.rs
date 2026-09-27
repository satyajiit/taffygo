// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Settling the one action that runs a tool job rather than a page effect.
//!
//! A job is dispatched, supervised and answered outside the page protocol, so
//! its terminal carries a status and counts instead of an `ActionResultCode`.
//! That difference is the whole reason this is its own file: everything in the
//! parent module reasons about page results, refusal codes and observations,
//! and none of those exist here.

use bip_types::identity::ActionId;

use super::Outcome;
use crate::action::ActionState;
use crate::artifact::ArtifactRecord;
use crate::command::CommandKind;
use crate::event::{EventKind, EventSubject, TaskEvent};
use crate::ids::IdSource;
use crate::reducer::Reducer;
use crate::time::Clock;
use crate::transition::RefusalReason;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Whether a job terminal names the one dispatched tool job it settles.
    pub(in crate::reducer) fn tool_job_outcome_matches(
        &self,
        action_id: &ActionId,
        job_id: &crate::ids::ToolJobId,
    ) -> bool {
        let Some(action) = self.actions.get(action_id.as_str()) else {
            return false;
        };
        action.state() == ActionState::Dispatching
            && action.proposal().action_class() == crate::authority::ActionClass::ExecuteToolJob
            && *job_id == crate::tool::job_id_for_action(self.task.task_id(), action_id)
    }

    /// Records what one tool job produced, and settles its action.
    ///
    /// No refusal-ledger entry and no reconcile effect, deliberately. The
    /// ledger counts page-protocol refusal codes a model keeps re-earning,
    /// and a job has none; a failed job is visible to the model as its
    /// exchange's outcome word. And an unknown outcome has no reconciler to
    /// ask — the broker that supervised the job is the only witness — so it
    /// settles as exactly what it is rather than starting a recovery nobody
    /// can perform.
    pub(in crate::reducer) fn on_record_tool_job_outcome(
        &mut self,
        action_id: &ActionId,
        job_id: &crate::ids::ToolJobId,
        outcome: &crate::tool::ToolJobOutcome,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        if !self.tool_job_outcome_matches(action_id, job_id) {
            return Err(RefusalReason::ActionOutcomeMismatch);
        }
        let produced_artifact = (outcome.status == crate::tool::ToolJobStatus::Succeeded)
            .then(|| {
                self.actions
                    .get(action_id.as_str())
                    .and_then(|action| action.proposal().intent().produced_artifact_kind())
            })
            .flatten();
        let produces_an_artifact = produced_artifact.is_some();
        let artifact_id = produced_artifact
            .map(|_| crate::ids::ArtifactId::new(format!("artifact-{}", action_id.as_str())));
        if let Some(artifact_id) = &artifact_id {
            if self.artifacts.contains_key(artifact_id.as_str()) {
                return Err(RefusalReason::ArtifactAlreadyReady);
            }
            if self.artifacts.len() >= crate::reducer::MAX_ARTIFACTS_PER_TASK {
                return Err(RefusalReason::ArtifactRegisterFull);
            }
            if self.retained_tool_artifact_count
                >= crate::reducer::MAX_RETAINED_TOOL_ARTIFACTS_PER_TASK
                || outcome.output_bytes
                    > crate::reducer::MAX_RETAINED_TOOL_ARTIFACT_BYTES_PER_TASK
                        - self.retained_tool_artifact_bytes
            {
                return Err(RefusalReason::ArtifactRegisterFull);
            }
        }
        let Some(action) = self.actions.get_mut(action_id.as_str()) else {
            return Err(RefusalReason::UnknownAction);
        };
        let state = match outcome.status {
            crate::tool::ToolJobStatus::Succeeded => ActionState::Verified,
            // Unavailable is a spent attempt like any failure — the dispatch
            // journaled the intent and no executor ran it — and the action
            // record has no state that says less.
            crate::tool::ToolJobStatus::Failed | crate::tool::ToolJobStatus::Unavailable => {
                ActionState::Failed
            }
            crate::tool::ToolJobStatus::Cancelled => ActionState::Cancelled,
            crate::tool::ToolJobStatus::OutcomeUnknown => ActionState::OutcomeUnknown,
        };
        if !action.settle_tool_job(state) {
            return Err(RefusalReason::ActionOutcomeMismatch);
        }
        let mut events = vec![
            TaskEvent::record(EventKind::ActionVerificationCompleted, kind)
                .about(EventSubject::Action(action_id.clone())),
        ];
        if let (Some(artifact_kind), Some(artifact_id)) = (produced_artifact, artifact_id) {
            // The bytes are already complete and verified behind browser
            // custody. Record only their durable identity and creation
            // revision; no GenerateArtifact effect may recreate or relabel
            // them through a document renderer.
            let creation_revision = self.task.revision().max(1);
            self.artifacts.insert(
                artifact_id.as_str().to_owned(),
                ArtifactRecord::browser(artifact_id.clone(), artifact_kind, creation_revision),
            );
            self.retained_tool_artifact_count += 1;
            self.retained_tool_artifact_bytes += outcome.output_bytes;
            events.push(
                TaskEvent::record(EventKind::ArtifactReady, kind)
                    .about(EventSubject::Artifact(artifact_id)),
            );
        }
        // A job that made something made progress whatever it repeated; one
        // that made nothing is held to the same rule as any other verified
        // call (decision 0233).
        if state == ActionState::Verified && produces_an_artifact {
            self.note_progress();
        } else {
            self.note_action_settled(action_id, None);
        }
        Ok(Outcome::recorded(events, Vec::new()))
    }
}
