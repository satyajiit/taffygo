// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded live recording of browser-verified steps, never a transcript.

use bip_types::action::PostconditionKind;
use policy_engine::origin::{normalize_serialization, NormalizedOrigin};
use procedure_engine::{
    LedgerEntry, MatchClause, Procedure, ProcedureId, Recording, StepDescriptor,
};
use task_engine::{ActionState, ConsentedSource, Milestone};

use crate::context::LivePage;
use crate::ports::ActionEffectFacts;

mod descriptor;
mod prelude;
#[cfg(test)]
mod tests;

const MAX_RECORDED_STEPS: usize = 64;

#[derive(Clone, Debug)]
struct PendingStep {
    identity: String,
    descriptor: StepDescriptor,
    verified: bool,
}

/// One generation's complete admitted sequence. Restoring a task leaves this
/// unarmed: a surviving journal cannot reconstruct the page facts it omitted.
#[derive(Clone, Debug, Default)]
pub struct FlowRecording {
    armed: bool,
    refused: bool,
    origin: Option<NormalizedOrigin>,
    clause: Option<MatchClause>,
    admitted: usize,
    steps: Vec<PendingStep>,
    prelude: prelude::Prelude,
}

impl FlowRecording {
    /// Called only after a fresh task's creation is durably committed.
    pub fn begin(&mut self) {
        self.armed = true;
    }

    /// Once recording starts, changing the consented origin refuses it.
    pub fn observe_origin(&mut self, origin: &str) {
        if !self.armed || self.refused || self.origin.is_none() {
            return;
        }
        let Ok(origin) = normalize_serialization(origin) else {
            self.refuse();
            return;
        };
        if self.origin.as_ref().is_some_and(|prior| prior != &origin) {
            self.refuse();
        } else {
            self.origin = Some(origin);
        }
    }

    /// Describes the exact committed dispatch while its observation is live.
    pub fn dispatched(&mut self, facts: &ActionEffectFacts, page: &LivePage) {
        if !self.armed || self.refused {
            return;
        }
        if self.origin.is_none() {
            if !self.prelude.dispatched(facts) {
                self.refuse();
            }
            return;
        }
        self.admitted = self.admitted.saturating_add(1);
        if let Some((origin, clause)) = page.recording_origin_clause() {
            self.observe_origin(&origin);
            if self.refused {
                return;
            }
            if self.clause.is_none() {
                self.clause = Some(clause);
            }
        }
        let Some((step, clause)) = descriptor::describe(&facts.proposal, page) else {
            self.refuse();
            return;
        };
        if self.steps.len() >= MAX_RECORDED_STEPS
            || self
                .steps
                .iter()
                .any(|step| step.identity == facts.action_id)
        {
            self.refuse();
            return;
        }
        if self.clause.is_none() {
            self.clause = clause;
        }
        self.steps.push(PendingStep {
            identity: facts.action_id.clone(),
            descriptor: step,
            verified: false,
        });
    }

    /// A refused, ambiguous or lost result cannot produce a saved success.
    pub fn settled(
        &mut self,
        facts: &ActionEffectFacts,
        accepted: Option<&ConsentedSource>,
        discovered: Option<&ConsentedSource>,
    ) {
        if !self.armed || self.refused {
            return;
        }
        if self.origin.is_none() {
            match self.prelude.settled(facts, accepted, discovered) {
                Ok(Some((origin, descriptor))) => {
                    self.origin = Some(origin);
                    self.clause = Some(MatchClause::RolePresent(
                        bip_types::snapshot::SemanticRole::Document,
                    ));
                    self.admitted = 1;
                    self.steps.push(PendingStep {
                        identity: facts.action_id.clone(),
                        descriptor,
                        verified: true,
                    });
                    self.prelude = prelude::Prelude::default();
                }
                Ok(None) => {}
                Err(()) => self.refuse(),
            }
            return;
        }
        let Some(step) = self
            .steps
            .iter_mut()
            .find(|step| step.identity == facts.action_id)
        else {
            self.refuse();
            return;
        };
        if facts.state == ActionState::Verified {
            step.verified = true;
        } else {
            self.refuse();
        }
    }

    /// A person step stores only its identity and never what they entered.
    pub fn handover(&mut self, identity: &str, completed: bool) {
        if !self.armed || self.refused {
            return;
        }
        if self.origin.is_none() {
            self.refuse();
            return;
        }
        let identity = format!("handover:{identity}");
        if completed {
            if let Some(step) = self.steps.iter_mut().find(|step| step.identity == identity) {
                step.verified = true;
            } else {
                self.refuse();
            }
        } else if self.steps.len() < MAX_RECORDED_STEPS
            && !self.steps.iter().any(|step| step.identity == identity)
        {
            self.admitted = self.admitted.saturating_add(1);
            self.steps.push(PendingStep {
                identity,
                descriptor: StepDescriptor::new("user.handover", PostconditionKind::NoMutation)
                    .taking(vec![procedure_engine::ArgumentDescriptor::new(
                        0,
                        procedure_engine::RecordedValue::Choice { index: 1 },
                    )]),
                verified: false,
            });
        } else {
            self.refuse();
        }
    }

    /// Returns the whole verified sequence only; unsupported steps never
    /// disappear into a shorter procedure that claims to be the same task.
    pub fn finish(&self, id: ProcedureId, task_id: &str) -> Option<Procedure> {
        if !self.armed
            || self.refused
            || self.steps.is_empty()
            || self.steps.iter().any(|step| !step.verified)
        {
            return None;
        }
        let entries = self
            .steps
            .iter()
            .map(|step| LedgerEntry::Described(step.descriptor.clone()))
            .collect();
        let recording = Recording::new(
            self.origin.clone()?,
            vec![self.clause?],
            entries,
            self.admitted,
        );
        procedure_engine::record_procedure(id, &recording, Milestone::M7)
            .ok()?
            .from_task(task_id)
            .ok()
    }

    /// Discards transient descriptions as soon as the sequence is unusable.
    pub fn refuse(&mut self) {
        self.refused = true;
        self.steps.clear();
        self.clause = None;
        self.origin = None;
        self.prelude = prelude::Prelude::default();
    }
}
