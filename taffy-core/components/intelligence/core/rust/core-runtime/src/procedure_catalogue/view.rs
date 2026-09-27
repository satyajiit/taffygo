// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded review of stored definitions without transient page or field bytes.

use core_api_types::{SiteSkillProvenanceView, SiteSkillStatusView, SiteSkillView};
use procedure_engine::{ProcedureProvenance, ProcedureStatus};

use super::ProcedureCatalogue;

impl ProcedureCatalogue {
    /// Stable identity-ordered metadata and complete safe step descriptions.
    pub fn site_skill_views(&self) -> Vec<SiteSkillView> {
        self.procedures
            .iter()
            .filter_map(|entry| {
                let provenance = match entry.procedure.provenance {
                    ProcedureProvenance::Authored => SiteSkillProvenanceView::Authored,
                    ProcedureProvenance::RecordedFromTask => {
                        SiteSkillProvenanceView::RecordedFromTask
                    }
                    // Restore rejects pack rows and the recorder cannot create
                    // one while signer custody remains unresolved.
                    ProcedureProvenance::InstalledFromPack => return None,
                };
                Some(SiteSkillView {
                    skill_id: entry.procedure.id.as_str().to_owned(),
                    origin: entry.procedure.scope.origin().display(),
                    provenance,
                    status: status(entry.procedure.status),
                    active_version: entry.procedure.version.0,
                    step_count: u32::try_from(entry.procedure.steps.len()).ok()?,
                    installed_at_epoch_ms: entry.installed_at_utc_ms,
                    updated_at_epoch_ms: entry.updated_at_utc_ms,
                    recorded_from_task_id: entry.procedure.recorded_from_task_id.clone(),
                    reviewed_steps: review_steps(&entry.procedure).unwrap_or_default(),
                })
            })
            .collect()
    }
}

/// An entire review or none; a missing argument never becomes a shorter flow.
fn review_steps(
    procedure: &procedure_engine::Procedure,
) -> Option<Vec<core_api_types::SiteSkillObservedStep>> {
    use core_api_types::{
        SiteSkillArgumentKind as Kind, SiteSkillObservedArgument as Argument,
        SiteSkillObservedStep as Step, SiteSkillSemanticTarget,
    };
    use procedure_engine::{FieldPurpose, PhraseId, StepValue};
    use task_engine::tool::{ArgumentValue, Milestone};
    procedure
        .steps
        .iter()
        .enumerate()
        .map(|(step_index, step)| {
            let definition = task_engine::tool::resolve(&step.verb, Milestone::M8)
                .entry()?
                .definition();
            let arguments = step
                .arguments
                .iter()
                .map(|argument| {
                    let parameter = definition
                        .parameters
                        .iter()
                        .position(|parameter| parameter.name == argument.name)?;
                    let mut projected = Argument {
                        parameter: u32::try_from(parameter).ok()?,
                        kind: Kind::Count,
                        value: 0,
                        purpose: 0,
                        public_address: None,
                        semantic_target: None,
                    };
                    match &argument.value {
                        StepValue::FromEarlierStep { step } => {
                            projected.kind = Kind::FromEarlierStep;
                            projected.value = u64::try_from(*step).ok()?;
                        }
                        StepValue::FromPerson { purpose } => {
                            projected.kind = Kind::FromPerson;
                            projected.purpose = ordinal(FieldPurpose::ALL, purpose)?;
                        }
                        StepValue::SemanticTarget { role, phrase } => {
                            projected.kind = Kind::SemanticTarget;
                            projected.semantic_target = Some(SiteSkillSemanticTarget {
                                role: ordinal(bip_types::snapshot::SemanticRole::ALL, role)?,
                                phrase: ordinal(PhraseId::ALL, phrase)?,
                            });
                        }
                        StepValue::Literal(ArgumentValue::Address(address))
                            if step_index == 0
                                && step.verb == "browser.navigate"
                                && procedure_engine::recording::public_address_is_valid(
                                    address,
                                    procedure.scope.origin(),
                                ) =>
                        {
                            projected.kind = Kind::PublicAddress;
                            projected.public_address = Some(address.clone());
                        }
                        StepValue::Literal(ArgumentValue::Choice(choice)) => {
                            projected.kind = Kind::Choice;
                            projected.value = u64::try_from(
                                definition
                                    .parameters
                                    .get(parameter)?
                                    .value_type
                                    .choices()
                                    .iter()
                                    .position(|value| *value == choice)?,
                            )
                            .ok()?;
                        }
                        StepValue::Literal(ArgumentValue::Count(count)) => projected.value = *count,
                        StepValue::Literal(ArgumentValue::Flag(flag)) => {
                            projected.kind = Kind::Flag;
                            projected.value = u64::from(*flag);
                        }
                        StepValue::Literal(_) => return None,
                    }
                    Some(projected)
                })
                .collect::<Option<Vec<_>>>()?;
            Some(Step {
                verb: step.verb.clone(),
                arguments,
                postcondition: ordinal(
                    bip_types::action::PostconditionKind::ALL,
                    &step.postcondition,
                )?,
                has_fill: step.fills.is_some(),
                fill_purpose: step
                    .fills
                    .map_or(Some(0), |purpose| ordinal(FieldPurpose::ALL, &purpose))?,
            })
        })
        .collect()
}

fn ordinal<T: PartialEq>(values: &[T], value: &T) -> Option<u32> {
    u32::try_from(values.iter().position(|member| member == value)?).ok()
}

const fn status(value: ProcedureStatus) -> SiteSkillStatusView {
    match value {
        ProcedureStatus::Draft => SiteSkillStatusView::Draft,
        ProcedureStatus::Active => SiteSkillStatusView::Active,
        ProcedureStatus::Superseded => SiteSkillStatusView::Superseded,
        ProcedureStatus::Retired => SiteSkillStatusView::Retired,
        ProcedureStatus::Disabled => SiteSkillStatusView::Disabled,
    }
}
