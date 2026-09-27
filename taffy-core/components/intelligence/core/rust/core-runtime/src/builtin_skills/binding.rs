// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One admission boundary for compiled built-in task bindings.

use core_service_types as wire;
use task_engine::{EffectiveToolSet, Milestone, TaskTemplateId};

use crate::assistant_configuration::AssistantConfiguration;
use crate::ports::{AssetInstallationView, BuiltinSkillBindingFacts};

use super::{
    builtin_id_from_wire, BuiltinSkillCatalogue, BuiltinSkillDefinition, BuiltinSkillReference,
    BuiltinSkillSourcePolicy, RequiredAsset, RequiredTool,
};

/// Why a compiled built-in could not be bound or restored exactly.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BuiltinSkillBindingError {
    ConflictingSavedProcedure,
    UnknownDefinition,
    WrongVersion,
    WrongTemplate,
    MilestoneNotReached,
    AbilityDisabled,
    SourcePolicyMismatch,
    RequiredAssetMissing,
    RequiredToolUnavailable,
    ToolBindingMismatch,
}

/// Returns a saved procedure only for a saved-procedure task. A compiled
/// built-in always walks the ordinary Taffy loop and never enters saved-skill
/// resolution or run-ledger bookkeeping, even if malformed test input carries
/// both identities.
pub fn saved_procedure_version(
    builtin: Option<BuiltinSkillReference>,
    saved: Option<&str>,
) -> Option<&str> {
    if builtin.is_some() {
        None
    } else {
        saved
    }
}

impl BuiltinSkillCatalogue {
    /// Binds one generated start in place. `Ok(false)` means it is not a
    /// built-in start and leaves saved-procedure/ordinary handling to its
    /// existing boundary.
    pub(crate) fn prepare_start(
        &self,
        configuration: &AssistantConfiguration,
        installations: &[AssetInstallationView],
        start: &mut wire::StartTaskCommand,
    ) -> Result<bool, BuiltinSkillBindingError> {
        let Some(reference) = start.builtin_skill.as_ref() else {
            return Ok(false);
        };
        if start.skill_version_id.is_some() {
            return Err(BuiltinSkillBindingError::ConflictingSavedProcedure);
        }
        let reference = BuiltinSkillReference {
            skill_id: builtin_id_from_wire(reference.skill_id),
            version: reference.version,
        };
        let definition = self.resolve(reference)?;
        let milestone = milestone(start.milestone);
        Self::validate_static(
            definition,
            reference,
            template(start.template_id),
            milestone,
        )?;
        if !configuration.ability_is_enabled(definition.required_ability) {
            return Err(BuiltinSkillBindingError::AbilityDisabled);
        }
        if !source_matches(
            definition.source_policy,
            start.consent_preview.sources.len(),
            start.consent_preview.source_discovery_enabled,
            start.consent_preview.new_source_cap,
            false,
        ) {
            return Err(BuiltinSkillBindingError::SourcePolicyMismatch);
        }
        if definition
            .required_assets
            .iter()
            .any(|required| !asset_is_installed(required, installations))
        {
            return Err(BuiltinSkillBindingError::RequiredAssetMissing);
        }
        let configured = configuration.effective_tool_allowlist();
        start.tool_allowlist = Self::canonical_tools(
            definition,
            milestone,
            &start.tool_allowlist,
            Some(&configured),
        )?;
        Ok(true)
    }

    /// Revalidates a replayed binding only against immutable compiled and
    /// durable facts. Current settings and installations never rewrite task
    /// history.
    pub(crate) fn validate_restore(
        &self,
        facts: &BuiltinSkillBindingFacts,
    ) -> Result<(), BuiltinSkillBindingError> {
        let definition = self.resolve(facts.reference)?;
        Self::validate_static(
            definition,
            facts.reference,
            facts.template_id,
            facts.milestone,
        )?;
        if !source_matches(
            definition.source_policy,
            facts.initial_source_count,
            facts.source_discovery_enabled,
            facts.remaining_new_source_cap,
            true,
        ) {
            return Err(BuiltinSkillBindingError::SourcePolicyMismatch);
        }
        let canonical =
            Self::canonical_tools(definition, facts.milestone, &facts.tool_allowlist, None)?;
        if canonical != facts.tool_allowlist {
            return Err(BuiltinSkillBindingError::ToolBindingMismatch);
        }
        Ok(())
    }

    fn resolve(
        &self,
        reference: BuiltinSkillReference,
    ) -> Result<&'static BuiltinSkillDefinition, BuiltinSkillBindingError> {
        let definition = self
            .definitions()
            .iter()
            .find(|definition| definition.id == reference.skill_id)
            .ok_or(BuiltinSkillBindingError::UnknownDefinition)?;
        if definition.version != reference.version || reference.version == 0 {
            return Err(BuiltinSkillBindingError::WrongVersion);
        }
        Ok(definition)
    }

    fn validate_static(
        definition: &BuiltinSkillDefinition,
        reference: BuiltinSkillReference,
        template_id: TaskTemplateId,
        milestone: Milestone,
    ) -> Result<(), BuiltinSkillBindingError> {
        if definition.id != reference.skill_id {
            return Err(BuiltinSkillBindingError::UnknownDefinition);
        }
        if task_template(definition.template) != template_id {
            return Err(BuiltinSkillBindingError::WrongTemplate);
        }
        if milestone < definition.milestone {
            return Err(BuiltinSkillBindingError::MilestoneNotReached);
        }
        Ok(())
    }

    fn canonical_tools(
        definition: &BuiltinSkillDefinition,
        milestone: Milestone,
        requested: &[String],
        configured: Option<&[String]>,
    ) -> Result<Vec<String>, BuiltinSkillBindingError> {
        let required = definition
            .required_tools
            .iter()
            .map(|required| match required {
                RequiredTool::Registered(name) => {
                    Ok(task_engine::tool::allowlist_name(name).to_owned())
                }
                RequiredTool::Planned(_) => Err(BuiltinSkillBindingError::RequiredToolUnavailable),
            })
            .collect::<Result<Vec<_>, _>>()?;
        let mut effective = EffectiveToolSet::for_task(milestone, requested);
        if let Some(configured) = configured {
            effective = effective.narrow_by(configured);
        }
        let narrowed = effective.narrow_by(&required);
        if definition
            .required_tools
            .iter()
            .any(|required| match required {
                RequiredTool::Registered(name) => {
                    !Self::registered_tool_is_available(name, milestone)
                        || !narrowed.entries().iter().any(|entry| entry.name == *name)
                }
                RequiredTool::Planned(_) => true,
            })
        {
            return Err(BuiltinSkillBindingError::RequiredToolUnavailable);
        }
        let mut canonical = Vec::new();
        for entry in narrowed.entries() {
            let name = task_engine::tool::allowlist_name(entry.name);
            if !canonical.iter().any(|held| held == name) {
                canonical.push(name.to_owned());
            }
        }
        Ok(canonical)
    }
}

fn asset_is_installed(required: &RequiredAsset, installed: &[AssetInstallationView]) -> bool {
    installed.iter().any(|candidate| {
        candidate.asset_id == required.asset_id
            && candidate.asset_revision == required.asset_revision
            && candidate.presence == wire::AssetPresence::Installed
    })
}

fn source_matches(
    policy: BuiltinSkillSourcePolicy,
    source_count: usize,
    discovery_enabled: bool,
    new_source_cap: u32,
    restoring: bool,
) -> bool {
    let discovery_cap = if restoring {
        new_source_cap <= task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP
    } else {
        (1..=task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP).contains(&new_source_cap)
    };
    match policy {
        BuiltinSkillSourcePolicy::SelectedPage => {
            source_count == 1 && !discovery_enabled && new_source_cap == 0
        }
        BuiltinSkillSourcePolicy::SelectedPages { minimum, maximum } => {
            u32::try_from(source_count).is_ok_and(|count| (minimum..=maximum).contains(&count))
                && !discovery_enabled
                && new_source_cap == 0
        }
        BuiltinSkillSourcePolicy::DiscoverWeb {
            maximum_named_sources,
            maximum_new_sources,
        } => {
            u32::try_from(source_count).is_ok_and(|count| count <= maximum_named_sources)
                && discovery_enabled
                && if restoring {
                    new_source_cap <= maximum_new_sources
                } else {
                    (1..=maximum_new_sources).contains(&new_source_cap)
                }
        }
        BuiltinSkillSourcePolicy::CurrentPageAndDiscoveredDestinations => {
            source_count == 1 && discovery_enabled && discovery_cap
        }
        BuiltinSkillSourcePolicy::DownloadHistoryAndDiscoveredPage => {
            source_count <= 1 && discovery_enabled && discovery_cap
        }
    }
}

const fn milestone(value: wire::TaskMilestone) -> Milestone {
    match value {
        wire::TaskMilestone::M0 => Milestone::M0,
        wire::TaskMilestone::M1 => Milestone::M1,
        wire::TaskMilestone::M2 => Milestone::M2,
        wire::TaskMilestone::M3 => Milestone::M3,
        wire::TaskMilestone::M4 => Milestone::M4,
        wire::TaskMilestone::M5 => Milestone::M5,
        wire::TaskMilestone::M6 => Milestone::M6,
        wire::TaskMilestone::M7 => Milestone::M7,
        wire::TaskMilestone::M8 => Milestone::M8,
    }
}

const fn template(value: wire::TaskTemplateId) -> TaskTemplateId {
    task_template(value)
}

const fn task_template(value: wire::TaskTemplateId) -> TaskTemplateId {
    match value {
        wire::TaskTemplateId::CompareProducts => TaskTemplateId::CompareProducts,
        wire::TaskTemplateId::SummarizeEvidence => TaskTemplateId::SummarizeEvidence,
        wire::TaskTemplateId::BuildSourceTable => TaskTemplateId::BuildSourceTable,
        wire::TaskTemplateId::WebErrand => TaskTemplateId::WebErrand,
    }
}

#[cfg(test)]
mod tests;
