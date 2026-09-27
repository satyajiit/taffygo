// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A complete live recording becomes an inactive, reviewable saved flow.

use std::fmt::Write;

use bip_types::identity::TaskId;
use procedure_engine::ProcedureId;
use task_engine::{TaskState, TaskTemplateId};

use super::ProfileServiceRuntime;
use crate::procedure_catalogue::PreparedSkillMutation;

impl ProfileServiceRuntime {
    /// Prepares one complete recording for the existing durable skill writer.
    /// No private profile, partial result, restored task or replay can mint it.
    pub fn prepare_completed_flow(
        &self,
        task_id: &TaskId,
        now_utc_millis: u64,
    ) -> Option<PreparedSkillMutation> {
        if self.private_profile
            || self.core.commit_in_flight(task_id) != Some(false)
            || self.core.recovery_required(task_id) != Some(false)
        {
            return None;
        }
        let task = self.core.task(task_id)?;
        let view = task.view_facts().ok()?;
        if view.state != TaskState::Completed
            || view.template_id != TaskTemplateId::WebErrand
            || crate::builtin_skills::saved_procedure_version(
                task.builtin_skill_reference(),
                task.skill_version_id(),
            )
            .is_some()
        {
            return None;
        }
        let digest = self.digest.sha256(task_id.as_str().as_bytes()).ok()?;
        let mut name = String::from("flow.");
        for byte in digest {
            write!(&mut name, "{byte:02x}").ok()?;
        }
        let procedure = self
            .core
            .loop_state(task_id.as_str())?
            .recording
            .finish(ProcedureId::new(name).ok()?, task_id.as_str())?;
        self.prepare_recorded_procedure(procedure, now_utc_millis)
            .ok()
    }
}
