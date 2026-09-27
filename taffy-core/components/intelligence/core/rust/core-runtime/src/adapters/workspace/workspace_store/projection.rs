// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated Core API projection of committed workspace state.

use std::collections::BTreeMap;

use core_api_types::{
    TaskTemplateId, WorkspaceFactKind, WorkspaceFactView, WorkspacePhase, WorkspaceSourceView,
    WorkspaceViewState,
};
use taffy_storage::ids::SourceId;
use taffy_storage::workspace::{WorkspacePhase as Phase, WorkspaceSnapshot, WorkspaceTemplate};

use super::WorkspaceStore;
use crate::ports::WorkspaceStoreError;

impl WorkspaceStore {
    pub fn project_core_api(&self) -> Result<Vec<WorkspaceViewState>, WorkspaceStoreError> {
        let mut snapshots = self.snapshots.values().collect::<Vec<_>>();
        snapshots.sort_by(|left, right| {
            right
                .last_updated_epoch_ms
                .cmp(&left.last_updated_epoch_ms)
                .then_with(|| left.workspace_id.cmp(&right.workspace_id))
        });
        snapshots.into_iter().map(project_snapshot).collect()
    }
}

fn project_snapshot(
    snapshot: &WorkspaceSnapshot,
) -> Result<WorkspaceViewState, WorkspaceStoreError> {
    let source_index = SourceProjectionIndex::build(snapshot)?;
    Ok(WorkspaceViewState {
        workspace_id: snapshot.workspace_id.to_text(),
        revision: snapshot.revision,
        // Core API does not yet carry display_name. Keep the immutable goal in
        // its existing field so rename never rewrites task history.
        goal: snapshot.goal.clone(),
        phase: project_phase(snapshot.phase),
        last_updated_epoch_ms: snapshot.last_updated_epoch_ms,
        template_id: project_template(snapshot.template),
        sources: snapshot
            .sources
            .iter()
            .map(|source| {
                let facts = source_index
                    .by_id
                    .get(&source.source_id)
                    .ok_or(WorkspaceStoreError::InvalidSnapshot)?;
                Ok(WorkspaceSourceView {
                    source_id: source.source_id.to_text(),
                    title: source.title.clone(),
                    host: source.host.clone(),
                    read_at_epoch_ms: source.read_at_epoch_ms,
                    fact_count: facts.fact_count,
                    excluded: source.excluded,
                })
            })
            .collect::<Result<Vec<_>, WorkspaceStoreError>>()?,
        facts: snapshot
            .facts
            .iter()
            .map(|fact| WorkspaceFactView {
                fact_id: fact.fact_id.to_text(),
                field: fact.field.clone(),
                value: fact.value.clone(),
                kind: match fact.kind {
                    taffy_storage::workspace::FactKind::FromPage => WorkspaceFactKind::FromPage,
                    taffy_storage::workspace::FactKind::Summarized => WorkspaceFactKind::Summarized,
                    taffy_storage::workspace::FactKind::TaffyInference => {
                        WorkspaceFactKind::TaffyInference
                    }
                    taffy_storage::workspace::FactKind::UserEntered => {
                        WorkspaceFactKind::UserEntered
                    }
                },
                sources: fact.sources.iter().map(|source| source.to_text()).collect(),
                correction: fact.correction.clone(),
                has_conflict: fact.has_conflict,
                needs_new_source: !fact.sources.is_empty()
                    && fact.sources.iter().all(|source_id| {
                        source_index
                            .by_id
                            .get(source_id)
                            .is_some_and(|facts| facts.excluded)
                    }),
            })
            .collect(),
        saved: snapshot.saved,
        display_name: snapshot.display_name.clone(),
        // The runtime attaches a digest-backed challenge after projection,
        // because the digest port belongs to the profile composition rather
        // than this ordered in-memory store.
        deletion_preview: None,
    })
}

#[derive(Clone, Copy, Debug, Default)]
struct SourceProjectionFacts {
    fact_count: u32,
    excluded: bool,
}

/// One status-publication index over source attribution.
///
/// Workspace facts are already validated as strictly ordered, duplicate-free
/// source lists. Filing each attribution once therefore answers both questions
/// the Core API projection asks without rescanning the whole fact set for every
/// source or the whole source set for every fact.
struct SourceProjectionIndex {
    by_id: BTreeMap<SourceId, SourceProjectionFacts>,
    #[cfg(test)]
    association_visits: usize,
}

impl SourceProjectionIndex {
    fn build(snapshot: &WorkspaceSnapshot) -> Result<Self, WorkspaceStoreError> {
        let mut by_id = snapshot
            .sources
            .iter()
            .map(|source| {
                (
                    source.source_id,
                    SourceProjectionFacts {
                        fact_count: 0,
                        excluded: source.excluded,
                    },
                )
            })
            .collect::<BTreeMap<_, _>>();
        #[cfg(test)]
        let mut association_visits = 0usize;
        for fact in &snapshot.facts {
            for source_id in &fact.sources {
                #[cfg(test)]
                {
                    association_visits = association_visits.saturating_add(1);
                }
                let source = by_id
                    .get_mut(source_id)
                    .ok_or(WorkspaceStoreError::InvalidSnapshot)?;
                source.fact_count = source
                    .fact_count
                    .checked_add(1)
                    .ok_or(WorkspaceStoreError::ProjectionOverflow)?;
            }
        }
        Ok(Self {
            by_id,
            #[cfg(test)]
            association_visits,
        })
    }
}

const fn project_phase(phase: Phase) -> WorkspacePhase {
    match phase {
        Phase::Running => WorkspacePhase::Running,
        Phase::WaitingForUser => WorkspacePhase::WaitingForUser,
        Phase::Paused => WorkspacePhase::Paused,
        Phase::Done => WorkspacePhase::Done,
        Phase::PartlyDone => WorkspacePhase::PartlyDone,
        Phase::Stopped => WorkspacePhase::Stopped,
        Phase::Failed => WorkspacePhase::Failed,
    }
}

const fn project_template(template: WorkspaceTemplate) -> TaskTemplateId {
    match template {
        WorkspaceTemplate::CompareProducts => TaskTemplateId::CompareProducts,
        WorkspaceTemplate::SummarizeEvidence => TaskTemplateId::SummarizeEvidence,
        WorkspaceTemplate::BuildSourceTable => TaskTemplateId::BuildSourceTable,
        WorkspaceTemplate::WebErrand => TaskTemplateId::WebErrand,
    }
}

#[cfg(test)]
mod tests {
    use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
    use taffy_storage::workspace::{
        FactKind, WorkspaceFact, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource,
        WorkspaceTemplate, MAX_FACTS, MAX_FACT_SOURCES, MAX_SOURCES,
    };

    use super::SourceProjectionIndex;

    #[test]
    fn source_index_visits_each_recorded_attribution_once_at_the_bounds() {
        let sources = (0..MAX_SOURCES)
            .map(|index| {
                let identity = u8::try_from(index)
                    .unwrap_or_else(|_| unreachable!("source bound fits in one byte"));
                WorkspaceSource {
                    source_id: SourceId::from_bytes([identity; 16]),
                    title: format!("Source {index}"),
                    host: format!("source-{index}.example"),
                    canonical_locator: None,
                    read_at_epoch_ms: 1,
                    excluded: index % 2 == 0,
                }
            })
            .collect::<Vec<_>>();
        let fact_sources = sources
            .iter()
            .take(MAX_FACT_SOURCES)
            .map(|source| source.source_id)
            .collect::<Vec<_>>();
        let snapshot = WorkspaceSnapshot {
            workspace_id: WorkspaceId::from_bytes([1; 16]),
            revision: 1,
            display_name: "Bounded workspace".to_owned(),
            goal: "Exercise the projection bound".to_owned(),
            phase: WorkspacePhase::Running,
            saved: true,
            last_updated_epoch_ms: 1,
            template: WorkspaceTemplate::CompareProducts,
            sources,
            facts: (0..MAX_FACTS)
                .map(|index| {
                    let identity = u8::try_from(index)
                        .unwrap_or_else(|_| unreachable!("fact bound fits in one byte"));
                    WorkspaceFact {
                        fact_id: FactId::from_bytes([identity; 16]),
                        field: "field".to_owned(),
                        value: "value".to_owned(),
                        kind: FactKind::FromPage,
                        sources: fact_sources.clone(),
                        correction: None,
                        has_conflict: false,
                        media_provenance: None,
                    }
                })
                .collect(),
        };

        let index = SourceProjectionIndex::build(&snapshot)
            .unwrap_or_else(|_| unreachable!("the bounded snapshot has valid attributions"));
        assert_eq!(
            index.association_visits,
            MAX_FACTS.saturating_mul(MAX_FACT_SOURCES)
        );
        assert_eq!(
            index
                .by_id
                .get(&fact_sources[0])
                .map(|facts| facts.fact_count),
            Some(
                u32::try_from(MAX_FACTS)
                    .unwrap_or_else(|_| unreachable!("fact bound fits in a counter"))
            )
        );
        assert_eq!(
            index
                .by_id
                .get(&snapshot.sources[MAX_FACT_SOURCES].source_id)
                .map(|facts| facts.fact_count),
            Some(0)
        );
    }
}
