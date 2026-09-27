// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Truthful bounded fallback when a complete `CoreStatus` exceeds transport.

use core_api_types::{
    CoreStatus, CoreStatusPayloadCodecError, CoreStatusProjectionFamily as Family,
    CoreStatusProjectionMode as Mode, CoreStatusProjectionOmission as Omission,
};

/// Encodes the complete projection, falling back only when its truthful wire
/// representation cannot fit the Core API transport budget.
pub(super) fn encode(
    status: &mut CoreStatus,
    provider_revision: u64,
) -> Result<Vec<u8>, CoreStatusPayloadCodecError> {
    match core_api_types::measure_core_status_payload(status) {
        Ok(_) => {}
        Err(CoreStatusPayloadCodecError::SizeLimit) => match fit_reviews(status) {
            Ok(()) => {}
            Err(CoreStatusPayloadCodecError::SizeLimit) => apply(status, provider_revision),
            Err(error) => return Err(error),
        },
        Err(error) => return Err(error),
    }
    core_api_types::encode_core_status_payload(status)
}

/// Reviews are optional display detail. Preserve all catalogue metadata and
/// the largest identity-ordered prefix of whole reviews the actual codec can
/// carry before falling back to recovery for the other durable state.
fn fit_reviews(status: &mut CoreStatus) -> Result<(), CoreStatusPayloadCodecError> {
    let mut withheld: Vec<_> = status
        .site_skills
        .iter_mut()
        .map(|skill| std::mem::take(&mut skill.reviewed_steps))
        .collect();
    core_api_types::measure_core_status_payload(status)?;
    let (mut fits, mut upper) = (0, withheld.len());
    while fits < upper {
        let candidate = fits + (upper - fits).div_ceil(2);
        set_review_prefix(status, &mut withheld, candidate);
        match core_api_types::measure_core_status_payload(status) {
            Ok(_) => fits = candidate,
            Err(CoreStatusPayloadCodecError::SizeLimit) => upper = candidate - 1,
            Err(error) => return Err(error),
        }
    }
    set_review_prefix(status, &mut withheld, fits);
    Ok(())
}

fn set_review_prefix(
    status: &mut CoreStatus,
    withheld: &mut [Vec<core_api_types::SiteSkillObservedStep>],
    count: usize,
) {
    for (index, (skill, saved)) in status.site_skills.iter_mut().zip(withheld).enumerate() {
        if (index < count) == skill.reviewed_steps.is_empty() {
            std::mem::swap(&mut skill.reviewed_steps, saved);
        }
    }
}

pub(super) fn apply(status: &mut CoreStatus, provider_revision: u64) {
    let omissions = describe_omissions(status, provider_revision);

    status.active_tasks.clear();
    status.workspaces.clear();
    status.workspace_export = None;
    status.asset_delivery = None;
    status.provider_roster.clear();
    status.provider_probes.clear();
    status.provider_models.clear();
    status.library = core_api_types::LibraryViewState {
        availability: core_api_types::LibraryAvailability::Unavailable,
        revision: 0,
        entries: Vec::new(),
        search: None,
        refresh_previews: Vec::new(),
        refresh_results: Vec::new(),
    };
    status.library_export = None;
    status.memory = core_api_types::MemoryViewState {
        availability: core_api_types::MemoryAvailability::Unavailable,
        revision: 0,
        records: Vec::new(),
        search: None,
    };
    status.saved_sign_ins = core_api_types::SavedSignInsView {
        availability: core_api_types::SavedDataAvailability::Unavailable,
        revision: 0,
        records: Vec::new(),
    };
    status.saved_details = core_api_types::SavedDetailsView {
        availability: core_api_types::SavedDataAvailability::Unavailable,
        revision: 0,
        people: Vec::new(),
    };
    status.site_skills.clear();
    status.projection_mode = Mode::RecoveryRequired;
    status.projection_omissions = omissions;
}

fn describe_omissions(status: &CoreStatus, provider_revision: u64) -> Vec<Omission> {
    vec![
        omission(
            Family::ActiveTasks,
            greatest(status.active_tasks.iter().map(|task| task.revision)),
            status.active_tasks.len(),
        ),
        omission(
            Family::Workspaces,
            greatest(status.workspaces.iter().map(|workspace| workspace.revision)),
            status.workspaces.len(),
        ),
        omission(
            Family::WorkspaceExport,
            status
                .workspace_export
                .as_ref()
                .map_or(0, |export| export.revision),
            usize::from(status.workspace_export.is_some()),
        ),
        omission(
            Family::AssetDelivery,
            0,
            status
                .asset_delivery
                .as_ref()
                .map_or(0, |delivery| delivery.assets.len()),
        ),
        omission(
            Family::ProviderRoster,
            provider_revision,
            status.provider_roster.len(),
        ),
        omission(Family::ProviderProbes, 0, status.provider_probes.len()),
        omission(
            Family::ProviderModels,
            provider_revision,
            status.provider_models.len(),
        ),
        omission(
            Family::Library,
            status.library.revision,
            status.library.entries.len(),
        ),
        omission(
            Family::LibraryExport,
            status
                .library_export
                .as_ref()
                .map_or(0, |export| export.library_revision),
            usize::from(status.library_export.is_some()),
        ),
        omission(
            Family::Memory,
            status.memory.revision,
            status.memory.records.len(),
        ),
        omission(
            Family::SavedSignIns,
            status.saved_sign_ins.revision,
            status.saved_sign_ins.records.len(),
        ),
        omission(
            Family::SavedDetails,
            status.saved_details.revision,
            status.saved_details.people.len(),
        ),
        omission(
            Family::SiteSkills,
            greatest(
                status
                    .site_skills
                    .iter()
                    .map(|skill| u64::from(skill.active_version)),
            ),
            status.site_skills.len(),
        ),
    ]
}

fn omission(family: Family, revision: u64, item_count: usize) -> Omission {
    Omission {
        family,
        revision,
        item_count: u32::try_from(item_count).unwrap_or(u32::MAX),
    }
}

fn greatest(values: impl Iterator<Item = u64>) -> u64 {
    values.max().unwrap_or(0)
}
