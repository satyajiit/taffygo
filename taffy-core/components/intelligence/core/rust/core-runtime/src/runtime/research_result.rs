// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Research completion counts only current, durably retained cited page facts.

use bip_types::identity::TaskId;
use loop_kernel::walk::{next_pre_model_observation_envelope, PreModelObservationEnvelope};
use taffy_storage::workspace::FactKind;
use task_engine::{TaskResult, TaskState, TaskTemplateId};

use super::workspace_commit::derive_page_facts;
use super::CoreRuntime;

impl CoreRuntime {
    /// Refines only the agent's empty-result candidate. The reducer never sees
    /// page bytes, so it conservatively reports zero facts; this composition
    /// owner can prove the actual retained rows before submitting a result.
    pub(crate) fn validated_research_result(
        &self,
        task_id: &TaskId,
        candidate: &TaskResult,
    ) -> Option<(u64, TaskResult)> {
        if candidate.fact_count != 0
            || !candidate.artifact_ids.is_empty()
            || candidate.unmet != [task_engine::template::empty_research_result_gap()]
            || self.commit_in_flight(task_id) != Some(false)
            || self.recovery_required(task_id) != Some(false)
        {
            return None;
        }
        let session = self.tasks.get(task_id.as_str())?;
        let task = session.task.as_ref();
        let view = task.view_facts().ok()?;
        if view.state != TaskState::Completing || view.template_id == TaskTemplateId::WebErrand {
            return None;
        }
        let consent = view.accepted_consent.as_ref()?;
        let page = &self.loop_state(task_id.as_str())?.page;
        let observations = page.matching_observations(&consent.sources);
        if observations.len() != consent.sources.len()
            || observations.is_empty()
            || !matches!(
                next_pre_model_observation_envelope(
                    task,
                    &observations,
                    self.components.digest.as_ref(),
                ),
                Ok(PreModelObservationEnvelope::Ready)
            )
        {
            return None;
        }
        let workspace_id = view.workspace_id.as_deref()?;
        self.components
            .workspaces
            .preflight_task_update(workspace_id)
            .ok()?;
        let workspace = self.components.workspaces.retained_snapshot(workspace_id)?;
        let workspace_id = workspace.workspace_id.to_text();
        let mut fact_count = 0_u64;
        for source in &consent.sources {
            let source_id = source.source_id.to_text();
            let stored_source = workspace
                .sources
                .iter()
                .find(|stored| stored.source_id.to_text() == source_id && !stored.excluded)?;
            let observation = observations
                .iter()
                .find(|observation| observation.source_id == source.source_id)?;
            let redacted_page = page.redacted_workspace_content(&observation.evidence)?;
            let expected = derive_page_facts(
                &redacted_page,
                &session.id_entropy,
                &workspace_id,
                &source_id,
                observation.evidence.page_epoch.0.as_str(),
                observation.evidence.graph_revision,
                self.components.digest.as_ref(),
            )
            .ok()?;
            if expected.is_empty() {
                return None;
            }
            for fact in expected {
                workspace.facts.iter().find(|stored| {
                    stored.fact_id.to_text() == fact.fact_id
                        && stored.kind == FactKind::FromPage
                        && stored.sources == [stored_source.source_id]
                        && stored.field == fact.field
                        && stored.value == fact.value
                        && stored.correction.is_none()
                        && !stored.has_conflict
                        && stored.media_provenance.is_none()
                })?;
                fact_count = fact_count.checked_add(1)?;
            }
        }
        Some((
            workspace.revision,
            TaskResult {
                artifact_ids: Vec::new(),
                unmet: Vec::new(),
                fact_count,
                source_count: u64::try_from(consent.sources.len()).ok()?,
            },
        ))
    }
}
