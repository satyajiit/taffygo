// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Transient exact-repeat retrieval over retained tasks and complete reviews.

use bip_types::identity::TaskId;
use core_api_types::{SiteSkillProvenanceView, SiteSkillStatusView, SiteSkillView};
use core_service_types::{SavedFlowQueryCommand, SavedFlowQueryKind, SavedFlowQueryStatus};
use task_engine::{TaskState, TaskTemplateId};

use super::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    /// Reads existing data only. The submitted goal is neither echoed nor
    /// copied into the catalogue, journal, index, model context or logs.
    pub fn query_saved_flows(
        &self,
        query: &SavedFlowQueryCommand,
    ) -> (
        SavedFlowQueryStatus,
        Vec<core_service_types::SavedFlowReview>,
    ) {
        if self.private_profile {
            return (SavedFlowQueryStatus::PrivateProfile, Vec::new());
        }
        let goal = query.goal.trim();
        let valid = match query.kind {
            SavedFlowQueryKind::ExactGoal => {
                !goal.is_empty()
                    && query.goal.len() <= core_api_types::MAX_TASK_GOAL_BYTES
                    && query.skill_id.is_empty()
                    && query.expected_version == 0
            }
            SavedFlowQueryKind::Review | SavedFlowQueryKind::PublicStart => {
                query.goal.is_empty()
                    && !query.skill_id.is_empty()
                    && query.skill_id.len() <= core_api_types::MAX_SKILL_ID_BYTES
                    && query.expected_version > 0
                    && usize::try_from(query.expected_version).is_ok_and(|version| {
                        version <= core_service_types::MAX_SKILL_VERSIONS_PER_SKILL
                    })
            }
        };
        if !valid {
            return (SavedFlowQueryStatus::InvalidRequest, Vec::new());
        }
        let flows = self
            .procedures
            .site_skill_views()
            .into_iter()
            .filter(complete_review)
            .filter(|flow| match query.kind {
                SavedFlowQueryKind::Review => {
                    flow.skill_id == query.skill_id && flow.active_version == query.expected_version
                }
                SavedFlowQueryKind::PublicStart => {
                    flow.skill_id == query.skill_id
                        && flow.active_version == query.expected_version
                        && active_recorded(flow)
                        && public_start(flow).is_some()
                }
                SavedFlowQueryKind::ExactGoal => {
                    active_recorded(flow)
                        && public_start(flow).is_some()
                        && flow.recorded_from_task_id.as_ref().is_some_and(|id| {
                            self.core.task(&TaskId::new(id)).is_some_and(|task| {
                                task.view_facts().is_ok_and(|facts| {
                                    facts.state == TaskState::Completed
                                        && facts.template_id == TaskTemplateId::WebErrand
                                        && facts.goal.trim() == goal
                                })
                            })
                        })
                }
            })
            .take(core_api_types::MAX_SAVED_FLOW_QUERY_RESULTS)
            .map(wire_review)
            .collect();
        (SavedFlowQueryStatus::Available, flows)
    }
}

fn complete_review(flow: &SiteSkillView) -> bool {
    !flow.reviewed_steps.is_empty() && flow.reviewed_steps.len() == flow.step_count as usize
}

fn active_recorded(flow: &SiteSkillView) -> bool {
    flow.status == SiteSkillStatusView::Active
        && flow.provenance == SiteSkillProvenanceView::RecordedFromTask
}

/// The definition projector already validated the address against its exact
/// procedure scope. Require that reviewed first navigation again here.
fn public_start(flow: &SiteSkillView) -> Option<&str> {
    let first = flow.reviewed_steps.first()?;
    if first.verb != "browser.navigate" {
        return None;
    }
    first.arguments.iter().find_map(|argument| {
        (argument.kind == core_api_types::SiteSkillArgumentKind::PublicAddress)
            .then_some(argument.public_address.as_deref())
            .flatten()
    })
}

fn wire_review(flow: SiteSkillView) -> core_service_types::SavedFlowReview {
    use core_service_types as wire;
    wire::SavedFlowReview {
        skill_id: flow.skill_id,
        origin: flow.origin,
        provenance: match flow.provenance {
            SiteSkillProvenanceView::Authored => wire::SkillProvenance::Authored,
            SiteSkillProvenanceView::RecordedFromTask => wire::SkillProvenance::RecordedFromTask,
        },
        status: match flow.status {
            SiteSkillStatusView::Draft => wire::SkillStatus::Draft,
            SiteSkillStatusView::Active => wire::SkillStatus::Active,
            SiteSkillStatusView::Superseded => wire::SkillStatus::Superseded,
            SiteSkillStatusView::Retired => wire::SkillStatus::Retired,
            SiteSkillStatusView::Disabled => wire::SkillStatus::Disabled,
        },
        active_version: flow.active_version,
        step_count: flow.step_count,
        installed_at_epoch_ms: flow.installed_at_epoch_ms,
        updated_at_epoch_ms: flow.updated_at_epoch_ms,
        recorded_from_task_id: flow.recorded_from_task_id,
        reviewed_steps: flow
            .reviewed_steps
            .into_iter()
            .map(|step| wire::SkillObservedStep {
                verb: step.verb,
                postcondition: step.postcondition,
                has_fill: step.has_fill,
                fill_purpose: step.fill_purpose,
                arguments: step
                    .arguments
                    .into_iter()
                    .map(|arg| wire::SkillObservedArgument {
                        parameter: arg.parameter,
                        kind: match arg.kind {
                            core_api_types::SiteSkillArgumentKind::FromEarlierStep => {
                                wire::SkillArgumentKind::FromEarlierStep
                            }
                            core_api_types::SiteSkillArgumentKind::FromPerson => {
                                wire::SkillArgumentKind::FromPerson
                            }
                            core_api_types::SiteSkillArgumentKind::Choice => {
                                wire::SkillArgumentKind::Choice
                            }
                            core_api_types::SiteSkillArgumentKind::Count => {
                                wire::SkillArgumentKind::Count
                            }
                            core_api_types::SiteSkillArgumentKind::Flag => {
                                wire::SkillArgumentKind::Flag
                            }
                            core_api_types::SiteSkillArgumentKind::PublicAddress => {
                                wire::SkillArgumentKind::PublicAddress
                            }
                            core_api_types::SiteSkillArgumentKind::SemanticTarget => {
                                wire::SkillArgumentKind::SemanticTarget
                            }
                        },
                        value: arg.value,
                        purpose: arg.purpose,
                        public_address: arg.public_address,
                        semantic_target: arg.semantic_target.map(|target| {
                            wire::SkillSemanticTarget {
                                role: target.role,
                                phrase: target.phrase,
                            }
                        }),
                    })
                    .collect(),
            })
            .collect(),
    }
}
