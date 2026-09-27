// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed decoding rules for a person-approved Library refresh.

use core_service_types as wire;
use task_engine::{
    ConsentedSource, LibraryRefreshContext, LibraryRefreshSource, SourceId, TaskTemplateId,
    WorkspaceId,
};

use super::StartTaskDecodeError;

pub(super) fn validate_start(
    command: &wire::StartTaskCommand,
    template_id: TaskTemplateId,
    consented_sources: &[ConsentedSource],
) -> Result<(), StartTaskDecodeError> {
    let Some(refresh) = command.library_refresh.as_ref() else {
        return Ok(());
    };
    let refresh_source_count = u32::try_from(refresh.sources.len())
        .map_err(|_| StartTaskDecodeError::InvalidConsentPreview)?;
    if template_id != TaskTemplateId::WebErrand
        || command.workspace_id.as_deref() == Some(refresh.collection_id.as_str())
        || command.consent_preview.provider_route != wire::TaskProviderRoute::NoModelRequired
        || !consented_sources.is_empty()
        || !command.consent_preview.source_discovery_enabled
        || command.consent_preview.new_source_cap != refresh_source_count
        || refresh_source_count == 0
    {
        return Err(StartTaskDecodeError::InvalidConsentPreview);
    }
    Ok(())
}

pub(super) fn decode(
    value: &wire::LibraryRefreshRequest,
) -> Result<LibraryRefreshContext, StartTaskDecodeError> {
    let context = LibraryRefreshContext {
        preview_id: value.preview_id.clone(),
        library_revision: value.library_revision,
        collection_id: WorkspaceId::parse(&value.collection_id)
            .map_err(|_| StartTaskDecodeError::InvalidWorkspaceId)?,
        source_workspace_revision: value.source_workspace_revision,
        sources: value
            .sources
            .iter()
            .map(|source| {
                Ok(LibraryRefreshSource {
                    source_id: SourceId::parse(&source.source_id)
                        .map_err(|_| StartTaskDecodeError::InvalidConsentPreview)?,
                    title: source.title.clone(),
                    host: source.host.clone(),
                    canonical_locator: source.canonical_locator.clone(),
                    original_content_digest: source.original_content_digest,
                })
            })
            .collect::<Result<Vec<_>, StartTaskDecodeError>>()?,
    };
    if !context.is_well_formed() {
        return Err(StartTaskDecodeError::InvalidConsentPreview);
    }
    Ok(context)
}
