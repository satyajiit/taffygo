// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Records only committed browser facts from a fresh Web errand.

use task_engine::{EventKind, EventSubject, TaskEvent, TaskTemplateId};

use super::CoreRuntime;
use bip_types::identity::TaskId;

impl CoreRuntime {
    pub(super) fn begin_flow_recording(&mut self, task_id: &TaskId) {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return;
        };
        if session
            .task
            .view_facts()
            .is_ok_and(|view| view.template_id == TaskTemplateId::WebErrand)
            && crate::builtin_skills::saved_procedure_version(
                session.task.builtin_skill_reference(),
                session.task.skill_version_id(),
            )
            .is_none()
        {
            self.loop_state_mut(task_id.as_str()).recording.begin();
        }
    }

    pub(super) fn record_committed_steps(&mut self, task_id: &TaskId, events: &[TaskEvent]) {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return;
        };
        let Ok(view) = session.task.view_facts() else {
            return;
        };
        if view.template_id != TaskTemplateId::WebErrand
            || crate::builtin_skills::saved_procedure_version(
                session.task.builtin_skill_reference(),
                session.task.skill_version_id(),
            )
            .is_some()
        {
            return;
        }
        let state = self
            .loop_states
            .entry(task_id.as_str().to_owned())
            .or_default();
        if let Some(consent) = &view.accepted_consent {
            if consent.sources.len() > 1 {
                state.recording.refuse();
            }
            for source in &consent.sources {
                state.recording.observe_origin(&source.normalized_origin);
            }
        }
        for event in events {
            match (&event.kind, &event.subject) {
                (EventKind::ActionDispatchStarted, Some(EventSubject::Action(id))) => {
                    if let Some(facts) = session.task.action_effect_facts(id) {
                        state.recording.dispatched(&facts, &state.page);
                    } else {
                        state.recording.refuse();
                    }
                }
                (EventKind::ActionVerificationCompleted, Some(EventSubject::Action(id))) => {
                    if let Some(facts) = session.task.action_effect_facts(id) {
                        let accepted = view.accepted_consent.as_ref().and_then(|consent| {
                            consent
                                .sources
                                .iter()
                                .find(|source| &source.tab_id == facts.proposal.tab_id())
                        });
                        let discovered = session.task.journal().commands().last().and_then(
                            |record| match &record.envelope.command {
                                task_engine::Command::RecordActionOutcome {
                                    action_id,
                                    outcome,
                                } if action_id == id => outcome.discovered_source.as_ref(),
                                _ => None,
                            },
                        );
                        state.recording.settled(&facts, accepted, discovered);
                    } else {
                        state.recording.refuse();
                    }
                }
                (EventKind::HandoverRequested, Some(EventSubject::Handover(id))) => {
                    state.recording.handover(id.as_str(), false);
                }
                (EventKind::HandoverCompleted, Some(EventSubject::ActorLease(lease))) => {
                    // The audit event names the fresh lease. The exact
                    // committed command owns which person step it completed.
                    let completed = session.task.journal().commands().last().and_then(|record| {
                        match &record.envelope.command {
                            task_engine::Command::CompleteHandover(completion)
                                if completion.resumed_with() == lease =>
                            {
                                Some(completion.handover_id().clone())
                            }
                            _ => None,
                        }
                    });
                    if let Some(id) = completed {
                        state.recording.handover(id.as_str(), true);
                    } else {
                        state.recording.refuse();
                    }
                }
                (
                    EventKind::TaskFailed
                    | EventKind::TaskCancelled
                    | EventKind::TaskPartial
                    | EventKind::HandoverExpired,
                    _,
                ) => state.recording.refuse(),
                _ => {}
            }
        }
    }
}
