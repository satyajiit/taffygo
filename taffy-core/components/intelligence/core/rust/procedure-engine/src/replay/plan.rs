// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A learned errand's visible ordered plan, derived only from saved verbs.

use crate::Procedure;
use task_engine::plan::{PlanDraft, StepDraft, StepKind};

pub(super) fn errand_plan(procedure: &Procedure) -> PlanDraft {
    let mut steps: Vec<_> = procedure
        .steps
        .iter()
        .enumerate()
        .map(|(index, step)| {
            let (kind, description) = match step.verb.as_str() {
                "user.handover" => (StepKind::AskUser, "Wait for you to complete this step"),
                "browser.navigate" | "browser.link.open" => {
                    (StepKind::Navigate, "Open the saved page")
                }
                "browser.download.list" => (StepKind::Observe, "Check your download"),
                "browser.download.from_link" => (StepKind::Export, "Download the document"),
                "browser.dom.click" | "browser.dom.focus" => {
                    (StepKind::Navigate, "Use the matching page control")
                }
                _ => (StepKind::Observe, "Read the current page"),
            };
            StepDraft {
                kind,
                description: description.to_owned(),
                dependencies: index.checked_sub(1).into_iter().collect(),
            }
        })
        .collect();
    steps.push(StepDraft {
        kind: StepKind::Extract,
        description: "Check the completed result".to_owned(),
        dependencies: procedure.steps.len().checked_sub(1).into_iter().collect(),
    });
    PlanDraft {
        summary: "Follow your saved steps".to_owned(),
        steps,
    }
}
