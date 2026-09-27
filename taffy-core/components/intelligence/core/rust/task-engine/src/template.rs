// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a reviewed template says about itself in a person's words.
//!
//! # Why the plan is the template's and not the procedure's
//!
//! Decision 0055 section 4 makes a procedure step exactly three things — a
//! tool name from the compiled-in table, a bounded argument record, and a
//! `PostconditionKind` — so a stored procedure carries no prose at all. That
//! is deliberate: prose in a stored record is a second thing a person could be
//! persuaded to paste in from elsewhere, and it would be shown to them as the
//! product's own explanation of what it is about to do.
//!
//! A plan, meanwhile, is explanatory and authorises nothing (see
//! [`crate::plan`]). It is the *task's* account of itself, which is why it can
//! be prose and why it belongs here rather than on the record. Both the
//! hard-coded [`crate::workflow`] and a replayed procedure ask this module,
//! and that is what lets the two emit the same [`Command::SetPlan`] — decision
//! 0055 section 7's identical-command requirement — without either of them
//! inventing wording the other would have to match.
//!
//! [`Command::SetPlan`]: crate::command::Command::SetPlan
//!
//! # `None` is a fact, not a gap to be filled in
//!
//! [`plan_for`] answers `None` for a template that has no reviewed local
//! sequence today. Returning a plausible-looking plan for one would be this
//! module deciding what those tasks do, which is a reviewed product question
//! and not a default.

use crate::plan::{PlanDraft, StepDraft, StepKind};
use crate::task::{GapReason, TaskTemplateId, UnmetRequirement};

/// The explanatory plan a task of `template` works from, when the product has
/// a reviewed local sequence for it.
// The two `None` arms below are kept apart on purpose, and clippy is right
// that their bodies match. They answer the same value for opposite reasons:
// one is a template whose sequence nobody has reviewed yet, the other is a
// template that will never have one. Merging them would lose exactly the
// distinction the arms are here to record, and the next person to add a
// reviewed sequence would have to work out which half they were editing.
#[allow(clippy::match_same_arms)]
pub fn plan_for(template: TaskTemplateId) -> Option<PlanDraft> {
    match template {
        TaskTemplateId::BuildSourceTable => Some(PlanDraft {
            summary: "Build a table from the selected source".to_owned(),
            steps: vec![
                StepDraft {
                    kind: StepKind::Observe,
                    description: "Read the selected source".to_owned(),
                    dependencies: Vec::new(),
                },
                StepDraft {
                    kind: StepKind::Extract,
                    description: "Build the source table".to_owned(),
                    dependencies: vec![0],
                },
            ],
        }),
        // No reviewed local sequence exists for either yet. See the module
        // header: an invented plan here would be an invented product.
        TaskTemplateId::CompareProducts | TaskTemplateId::SummarizeEvidence => None,
        // An errand answers `None` for a different reason than the two above,
        // and the difference is decided rather than pending. Decision 0087
        // section 3: an errand is walked a step at a time from what the page
        // turns out to say, so there is no sequence to review. A plan composed
        // before the first observation would describe a page nobody has looked
        // at, and a plan composed from a model's reply would be model prose
        // rendered as the product's own account of itself.
        TaskTemplateId::WebErrand => None,
    }
}

/// The one gap a bounded local observation can leave behind.
///
/// An observation the protocol itself reported as incomplete produces a
/// `PARTIAL` result rather than a `COMPLETED` one, and this is the labelled
/// requirement that says so. It lives beside the plan for the same reason the
/// plan does: it is prose a person reads, both paths have to produce the same
/// one, and neither of them is a place to compose it.
pub fn incomplete_observation_gap() -> UnmetRequirement {
    UnmetRequirement {
        subject: "source observation completeness".to_owned(),
        reason: GapReason::BudgetReached,
    }
}

/// The stable gap used when an errand answered in prose with no outcome.
///
/// The subject is the outcome rather than the reading, because the reading is
/// exactly what was not missing: the errand this exists for read its pages and
/// then could not act on them. [`GapReason::NotFoundInScope`] is the honest
/// reason — nothing the task was allowed to reach carried the outcome — and it
/// is deliberately an existing reason rather than a new one, because
/// `PersistedGapReason` is a closed generated enumeration and a fourth word
/// for this would be a contract minor for one sentence.
pub fn errand_outcome_gap() -> UnmetRequirement {
    UnmetRequirement {
        subject: "errand outcome".to_owned(),
        reason: GapReason::NotFoundInScope,
    }
}

/// The stable gap used when research produced neither a fact nor an artifact.
pub fn empty_research_result_gap() -> UnmetRequirement {
    UnmetRequirement {
        subject: "research result".to_owned(),
        reason: GapReason::NotFoundInScope,
    }
}

#[cfg(test)]
mod tests {
    use super::{empty_research_result_gap, incomplete_observation_gap, plan_for};
    use crate::plan::StepKind;
    use crate::task::TaskTemplateId;

    #[test]
    fn exactly_one_template_has_a_reviewed_local_plan_today() {
        let named: Vec<&str> = TaskTemplateId::ALL
            .iter()
            .filter(|template| plan_for(**template).is_some())
            .map(|template| template.label())
            .collect();
        assert_eq!(named, vec!["build_a_source_table"]);
    }

    #[test]
    fn an_empty_research_result_has_one_stable_gap() {
        let gap = empty_research_result_gap();
        assert_eq!(gap.subject, "research result");
        assert_eq!(gap.reason, crate::task::GapReason::NotFoundInScope);
    }

    #[test]
    fn an_errand_has_no_plan_and_that_is_the_decision() {
        // Decision 0087 section 3. The other two templates answer `None`
        // because no sequence has been reviewed for them yet; an errand
        // answers `None` because there is nothing to review. Asserting it here
        // keeps a later "fill in the missing plans" change from quietly
        // deciding what an errand does.
        assert!(plan_for(TaskTemplateId::WebErrand).is_none());
        assert!(TaskTemplateId::WebErrand.admits_discovered_destination());
    }

    #[test]
    fn only_an_errand_admits_a_destination_it_has_not_been_given() {
        let named: Vec<&str> = TaskTemplateId::ALL
            .iter()
            .filter(|template| template.admits_discovered_destination())
            .map(|template| template.label())
            .collect();
        assert_eq!(named, vec!["web_errand"]);
    }

    #[test]
    fn the_source_table_plan_observes_and_then_extracts_from_what_it_observed() {
        let Some(plan) = plan_for(TaskTemplateId::BuildSourceTable) else {
            unreachable!("the reviewed template has a plan")
        };
        assert_eq!(plan.validate(), Ok(()));
        let kinds: Vec<StepKind> = plan.steps.iter().map(|step| step.kind).collect();
        assert_eq!(kinds, vec![StepKind::Observe, StepKind::Extract]);
        // The extract waits on the observation. A plan whose second step did
        // not would be a plan that could build a table from nothing.
        assert_eq!(
            plan.steps.get(1).map(|step| step.dependencies.as_slice()),
            Some([0].as_slice())
        );
    }

    #[test]
    fn the_gap_is_the_same_gap_every_time_it_is_asked_for() {
        assert_eq!(incomplete_observation_gap(), incomplete_observation_gap());
    }
}
