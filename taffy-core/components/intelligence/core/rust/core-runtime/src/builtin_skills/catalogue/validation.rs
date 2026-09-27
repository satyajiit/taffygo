// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Construction-time proof for the compiled built-in catalogue.

use std::collections::BTreeSet;

use task_engine::{Milestone, ToolDispatch, REGISTRY};

use super::{BuiltinSkillCatalogue, BuiltinSkillCatalogueError};
use crate::builtin_skills::{
    BuiltinSkillDefinition as Definition, BuiltinSkillId as Id, BuiltinSkillSourcePolicy as Source,
    RequiredTool,
};

pub(super) fn validate_definitions(
    definitions: &[Definition],
) -> Result<(), BuiltinSkillCatalogueError> {
    if definitions.len() != Id::ALL.len()
        || definitions
            .iter()
            .zip(Id::ALL)
            .any(|(row, id)| row.id != *id)
    {
        return Err(BuiltinSkillCatalogueError::DefinitionSet);
    }
    let mut planned_seen = [false; 4];
    let milestones = [
        Milestone::M3,
        Milestone::M3,
        Milestone::M3,
        Milestone::M3,
        Milestone::M3,
        Milestone::M4,
        Milestone::M4,
        Milestone::M5,
        Milestone::M5,
        Milestone::M5,
        Milestone::M6,
        Milestone::M6,
        Milestone::M6,
        Milestone::M6,
        Milestone::M7,
        Milestone::M7,
    ];
    for (row, expected_milestone) in definitions.iter().zip(milestones) {
        if row.version != 1 {
            return Err(BuiltinSkillCatalogueError::DefinitionVersion);
        }
        if row.milestone != expected_milestone {
            return Err(BuiltinSkillCatalogueError::DefinitionMilestone);
        }
        validate_source_policy(row.source_policy)?;
        if row.required_tools.is_empty() {
            return Err(BuiltinSkillCatalogueError::MissingRequiredTool);
        }
        let mut tool_names = BTreeSet::new();
        for required in row.required_tools {
            let name = match required {
                RequiredTool::Registered(name) => {
                    if !BuiltinSkillCatalogue::registered_tool_is_available(name, Milestone::M8) {
                        return Err(BuiltinSkillCatalogueError::RegisteredToolUnavailable(name));
                    }
                    *name
                }
                RequiredTool::Planned(tool) => {
                    if REGISTRY.iter().any(|entry| entry.name == tool.name()) {
                        return Err(BuiltinSkillCatalogueError::PlannedToolRegistered);
                    }
                    let Some(seen) = planned_seen.get_mut(tool.index()) else {
                        return Err(BuiltinSkillCatalogueError::PlannedToolSet);
                    };
                    *seen = true;
                    tool.name()
                }
            };
            if !tool_names.insert(name) {
                return Err(BuiltinSkillCatalogueError::DuplicateRequiredTool);
            }
        }
        let mut assets = BTreeSet::new();
        for asset in row.required_assets {
            if !assets.insert((asset.asset_id, asset.asset_revision)) {
                return Err(BuiltinSkillCatalogueError::DuplicateRequiredAsset);
            }
        }
    }
    if !planned_seen.iter().all(|seen| *seen) {
        return Err(BuiltinSkillCatalogueError::PlannedToolSet);
    }
    Ok(())
}

fn validate_source_policy(policy: Source) -> Result<(), BuiltinSkillCatalogueError> {
    let valid = match policy {
        Source::SelectedPage
        | Source::CurrentPageAndDiscoveredDestinations
        | Source::DownloadHistoryAndDiscoveredPage => true,
        Source::SelectedPages { minimum, maximum } => {
            (minimum == 1 || minimum == 2) && maximum == 16 && minimum <= maximum
        }
        Source::DiscoverWeb {
            maximum_named_sources,
            maximum_new_sources,
        } => {
            maximum_named_sources == 1
                && maximum_new_sources == task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP
        }
    };
    if valid {
        Ok(())
    } else {
        Err(BuiltinSkillCatalogueError::InvalidSourcePolicy)
    }
}

pub(super) fn registered_entry(
    name: &str,
    milestone: Milestone,
) -> Option<&'static task_engine::ToolEntry> {
    let mut matching = REGISTRY.iter().filter(|entry| entry.name == name);
    let entry = matching.next()?;
    if matching.next().is_some()
        || !entry.is_available_at(milestone)
        || matches!(entry.dispatch, ToolDispatch::Unserved)
    {
        return None;
    }
    let callable_names: Vec<_> = entry.callable_names().collect();
    if callable_names.is_empty()
        || callable_names.iter().any(|callable| {
            let lookup = task_engine::resolve(callable, milestone);
            !lookup.is_available()
                || lookup.entry().is_none_or(|resolved| resolved != entry)
                || lookup
                    .entry()
                    .is_some_and(|resolved| matches!(resolved.dispatch, ToolDispatch::Unserved))
        })
    {
        return None;
    }
    Some(entry)
}
