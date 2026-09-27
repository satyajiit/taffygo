// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact, content-free Library refresh status projection.

use std::collections::{BTreeMap, BTreeSet};

use core_api_types::{
    LibraryRefreshDisposition as ViewDisposition, LibraryRefreshPreviewView,
    LibraryRefreshResultItemView, LibraryRefreshResultView, LibraryRefreshSourceView,
    LibraryViewState, TaskProviderRoute, MAX_LIBRARY_REFRESH_RESULTS,
};
use taffy_storage::library::{
    LibraryRefreshDisposition, LibraryRefreshPreview, LibraryRefreshResult,
};
use taffy_storage::workspace::WorkspacePhase;
use task_engine::LibraryRefreshContext;

use super::super::CoreRuntime;

pub(super) fn project_library_view(runtime: &CoreRuntime) -> LibraryViewState {
    let mut library = runtime.components.library.project_core_api();
    if library.availability != core_api_types::LibraryAvailability::Available {
        return library;
    }

    let workspaces = runtime
        .components
        .workspaces
        .list_workspaces()
        .into_iter()
        .map(|entry| (entry.workspace_id.clone(), entry))
        .collect::<BTreeMap<_, _>>();
    let collection_ids = library
        .entries
        .iter()
        .map(|entry| entry.collection_id.clone())
        .collect::<BTreeSet<_>>();
    let mut plans = BTreeMap::new();

    for collection_id in collection_ids {
        let Some(workspace_entry) = workspaces.get(&collection_id) else {
            continue;
        };
        let Ok(workspace) = runtime
            .components
            .workspaces
            .reopen_workspace(&collection_id, workspace_entry.revision)
        else {
            continue;
        };
        let Ok(preview) = runtime.components.library.preview_refresh(
            &collection_id,
            library.revision,
            &workspace,
            workspace_entry.revision,
            runtime.components.digest.as_ref(),
        ) else {
            continue;
        };
        library.refresh_previews.push(project_preview(&preview));
        plans.insert(preview.preview_id.clone(), preview);
    }

    let mut projected_ids = BTreeSet::new();
    for session in runtime.tasks.values() {
        if library.refresh_results.len() == MAX_LIBRARY_REFRESH_RESULTS {
            break;
        }
        let Some(context) = session.task.library_refresh_context() else {
            continue;
        };
        let Some(preview) = plans.get(&context.preview_id) else {
            continue;
        };
        if !context_matches_preview(&context, preview)
            || projected_ids.contains(&context.preview_id)
        {
            continue;
        }
        let Some(refreshed_workspace_id) = session.task.workspace_id().map(|id| id.to_text())
        else {
            continue;
        };
        let Some(refreshed_entry) = workspaces.get(&refreshed_workspace_id) else {
            continue;
        };
        if !terminal_phase(refreshed_entry.phase) {
            continue;
        }
        let Ok(original) = runtime.components.workspaces.reopen_workspace(
            &context.collection_id.to_text(),
            context.source_workspace_revision,
        ) else {
            continue;
        };
        let Ok(refreshed) = runtime
            .components
            .workspaces
            .reopen_workspace(&refreshed_workspace_id, refreshed_entry.revision)
        else {
            continue;
        };
        let Ok(result) = runtime.components.library.compare_refresh(
            preview,
            &original,
            &refreshed,
            runtime.components.digest.as_ref(),
        ) else {
            continue;
        };
        projected_ids.insert(context.preview_id);
        library.refresh_results.push(project_result(result));
    }
    library
}

fn project_preview(preview: &LibraryRefreshPreview) -> LibraryRefreshPreviewView {
    LibraryRefreshPreviewView {
        preview_id: preview.preview_id.clone(),
        collection_id: preview.collection_id.to_text(),
        library_revision: preview.library_revision,
        source_workspace_revision: preview.source_workspace_revision,
        provider_route: TaskProviderRoute::NoModelRequired,
        navigation_count: preview.estimated_navigation_count(),
        observation_count: preview.estimated_observation_count(),
        sources: preview
            .sources
            .iter()
            .map(|source| LibraryRefreshSourceView {
                source_id: source.source_id.to_text(),
                title: source.title.clone(),
                host: source.host.clone(),
            })
            .collect(),
    }
}

fn project_result(result: LibraryRefreshResult) -> LibraryRefreshResultView {
    LibraryRefreshResultView {
        preview_id: result.preview_id,
        collection_id: result.collection_id.to_text(),
        items: result
            .sources
            .into_iter()
            .map(|source| LibraryRefreshResultItemView {
                source_id: source.source_id.to_text(),
                disposition: match source.disposition {
                    LibraryRefreshDisposition::Unchanged => ViewDisposition::Unchanged,
                    LibraryRefreshDisposition::Changed => ViewDisposition::Changed,
                    LibraryRefreshDisposition::Missing => ViewDisposition::Missing,
                },
            })
            .collect(),
    }
}

fn context_matches_preview(
    context: &LibraryRefreshContext,
    preview: &LibraryRefreshPreview,
) -> bool {
    context.preview_id == preview.preview_id
        && context.library_revision == preview.library_revision
        && context.collection_id.to_text() == preview.collection_id.to_text()
        && context.source_workspace_revision == preview.source_workspace_revision
        && context.sources.len() == preview.sources.len()
        && context
            .sources
            .iter()
            .zip(&preview.sources)
            .all(|(approved, current)| {
                approved.source_id.to_text() == current.source_id.to_text()
                    && approved.title == current.title
                    && approved.host == current.host
                    && approved.canonical_locator == current.canonical_locator
                    && approved.original_content_digest == current.original_content_digest
            })
}

const fn terminal_phase(phase: WorkspacePhase) -> bool {
    matches!(
        phase,
        WorkspacePhase::Done
            | WorkspacePhase::PartlyDone
            | WorkspacePhase::Stopped
            | WorkspacePhase::Failed
    )
}

#[cfg(test)]
mod tests {
    use taffy_storage::ids::{SourceId as StoredSourceId, WorkspaceId as StoredWorkspaceId};
    use taffy_storage::library::{LibraryRefreshPreview, LibraryRefreshSource};
    use task_engine::{
        LibraryRefreshContext, LibraryRefreshSource as TaskRefreshSource, SourceId, WorkspaceId,
    };

    use super::{context_matches_preview, terminal_phase};
    use taffy_storage::workspace::WorkspacePhase;

    fn preview() -> LibraryRefreshPreview {
        LibraryRefreshPreview {
            preview_id: "ab".repeat(32),
            library_revision: 7,
            collection_id: StoredWorkspaceId::from_bytes([1; 16]),
            collection_name: "Saved evidence".to_owned(),
            source_workspace_revision: 3,
            sources: vec![LibraryRefreshSource {
                source_id: StoredSourceId::from_bytes([2; 16]),
                title: "Evidence".to_owned(),
                host: "example.test".to_owned(),
                canonical_locator: "https://example.test/evidence".to_owned(),
                original_content_digest: [4; 32],
            }],
        }
    }

    fn context() -> LibraryRefreshContext {
        LibraryRefreshContext {
            preview_id: "ab".repeat(32),
            library_revision: 7,
            collection_id: WorkspaceId::from_bytes([1; 16]),
            source_workspace_revision: 3,
            sources: vec![TaskRefreshSource {
                source_id: SourceId::from_bytes([2; 16]),
                title: "Evidence".to_owned(),
                host: "example.test".to_owned(),
                canonical_locator: "https://example.test/evidence".to_owned(),
                original_content_digest: [4; 32],
            }],
        }
    }

    #[test]
    fn only_the_exact_approved_manifest_can_publish_a_result() {
        let preview = preview();
        let approved = context();
        assert!(context_matches_preview(&approved, &preview));

        let mut stale = approved.clone();
        stale.source_workspace_revision += 1;
        assert!(!context_matches_preview(&stale, &preview));

        let mut substituted = approved;
        if let Some(source) = substituted.sources.first_mut() {
            source.canonical_locator = "https://example.test/other".to_owned();
        }
        assert!(!context_matches_preview(&substituted, &preview));
    }

    #[test]
    fn results_wait_for_a_closed_terminal_workspace() {
        for phase in [
            WorkspacePhase::Done,
            WorkspacePhase::PartlyDone,
            WorkspacePhase::Stopped,
            WorkspacePhase::Failed,
        ] {
            assert!(terminal_phase(phase));
        }
        for phase in [
            WorkspacePhase::Running,
            WorkspacePhase::WaitingForUser,
            WorkspacePhase::Paused,
        ] {
            assert!(!terminal_phase(phase));
        }
    }
}
