// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic optimistic-concurrency workspace mutations.

use crate::ids::{FactId, SourceId};

use super::model::{
    valid_display_name, FactKind, WorkspaceFact, WorkspacePageFactScope, WorkspacePhase,
    WorkspaceSnapshot, WorkspaceSource, MAX_FACTS, MAX_VALUE_BYTES,
};

/// One exact-source page replacement committed with the owning task phase.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspacePageFactReplacement {
    pub source_id: SourceId,
    pub scope: WorkspacePageFactScope,
    pub facts: Vec<WorkspaceFact>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum WorkspaceMutation {
    /// Explicitly retain a completed or partly-done result.
    Save,
    Rename {
        display_name: String,
    },
    CorrectFact {
        fact_id: FactId,
        value: String,
    },
    ExcludeSource {
        source_id: SourceId,
    },
    ReplacePageFacts {
        source_id: SourceId,
        facts: Vec<WorkspaceFact>,
    },
    /// Synchronize one owning-task transition and its optional page capture.
    SyncTask {
        phase: WorkspacePhase,
        page_facts: Option<WorkspacePageFactReplacement>,
        /// Public metadata for one newly observed, durably consented task source.
        /// A verified empty capture may carry this without page facts; callers
        /// establish that observation before entering the storage mutation.
        source: Option<WorkspaceSource>,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MutationError {
    StaleRevision,
    UnknownFact,
    UnknownSource,
    AlreadyExcluded,
    SourceExcluded,
    EmptyPageFacts,
    TooManyFacts,
    InvalidCorrection,
    InvalidDisplayName,
    UnchangedDisplayName,
    UnchangedTask,
    AlreadySaved,
    NotSavable,
    RevisionOverflow,
    InvalidSnapshot,
}

impl WorkspaceSnapshot {
    pub fn apply(
        &self,
        expected_revision: u64,
        updated_at_epoch_ms: u64,
        mutation: WorkspaceMutation,
    ) -> Result<Self, MutationError> {
        if !self.validate() {
            return Err(MutationError::InvalidSnapshot);
        }
        if self.revision != expected_revision {
            return Err(MutationError::StaleRevision);
        }
        match mutation {
            WorkspaceMutation::Save => {
                if self.saved {
                    return Err(MutationError::AlreadySaved);
                }
                if !matches!(
                    self.phase,
                    WorkspacePhase::Done | WorkspacePhase::PartlyDone
                ) {
                    return Err(MutationError::NotSavable);
                }
                self.finish_mutation(updated_at_epoch_ms, |next| {
                    next.saved = true;
                    Ok(())
                })
            }
            WorkspaceMutation::Rename { display_name } => {
                if !valid_display_name(&display_name) {
                    return Err(MutationError::InvalidDisplayName);
                }
                if display_name == self.display_name {
                    return Err(MutationError::UnchangedDisplayName);
                }
                self.finish_mutation(updated_at_epoch_ms, |next| {
                    next.display_name = display_name;
                    Ok(())
                })
            }
            WorkspaceMutation::CorrectFact { fact_id, value } => {
                if value.is_empty() || value.len() > MAX_VALUE_BYTES {
                    return Err(MutationError::InvalidCorrection);
                }
                let fact_index = self.fact_index(fact_id).ok_or(MutationError::UnknownFact)?;
                self.finish_mutation(updated_at_epoch_ms, |next| {
                    next.facts
                        .get_mut(fact_index)
                        .ok_or(MutationError::InvalidSnapshot)?
                        .correction = Some(value);
                    Ok(())
                })
            }
            WorkspaceMutation::ExcludeSource { source_id } => {
                let source_index = self
                    .source_index(source_id)
                    .ok_or(MutationError::UnknownSource)?;
                if self
                    .sources
                    .get(source_index)
                    .ok_or(MutationError::InvalidSnapshot)?
                    .excluded
                {
                    return Err(MutationError::AlreadyExcluded);
                }
                self.finish_mutation(updated_at_epoch_ms, |next| {
                    next.sources
                        .get_mut(source_index)
                        .ok_or(MutationError::InvalidSnapshot)?
                        .excluded = true;
                    Ok(())
                })
            }
            WorkspaceMutation::ReplacePageFacts { source_id, facts } => {
                self.apply_page_fact_replacement(source_id, facts, updated_at_epoch_ms)
            }
            WorkspaceMutation::SyncTask {
                phase,
                page_facts,
                source,
            } => self.apply_task_sync(phase, page_facts, source, updated_at_epoch_ms),
        }
    }

    fn apply_task_sync(
        &self,
        phase: WorkspacePhase,
        page_facts: Option<WorkspacePageFactReplacement>,
        source: Option<WorkspaceSource>,
        updated_at_epoch_ms: u64,
    ) -> Result<Self, MutationError> {
        if self.phase == phase && page_facts.is_none() && source.is_none() {
            return Err(MutationError::UnchangedTask);
        }
        if source.as_ref().is_some_and(|source| {
            source.excluded
                || source.read_at_epoch_ms != 0
                || self.source(source.source_id).is_some()
                || page_facts
                    .as_ref()
                    .is_some_and(|facts| facts.source_id != source.source_id)
        }) {
            return Err(MutationError::InvalidSnapshot);
        }
        self.finish_mutation(updated_at_epoch_ms, |next| {
            next.phase = phase;
            if let Some(mut source) = source {
                source.read_at_epoch_ms = updated_at_epoch_ms;
                next.sources.push(source);
                next.sources.sort_by_key(|source| source.source_id);
            }
            if let Some(replacement) = page_facts {
                let source_index = next.validate_page_replacement(&replacement)?;
                next.replace_page_facts(replacement, source_index, updated_at_epoch_ms)?;
            }
            Ok(())
        })
    }

    fn apply_page_fact_replacement(
        &self,
        source_id: SourceId,
        facts: Vec<WorkspaceFact>,
        updated_at_epoch_ms: u64,
    ) -> Result<Self, MutationError> {
        if facts.is_empty() {
            return Err(MutationError::EmptyPageFacts);
        }
        let scope = facts
            .first()
            .and_then(WorkspaceFact::page_scope)
            .ok_or(MutationError::InvalidSnapshot)?;
        let replacement = WorkspacePageFactReplacement {
            source_id,
            scope,
            facts,
        };
        let source_index = self.validate_page_replacement(&replacement)?;
        self.finish_mutation(updated_at_epoch_ms, |next| {
            next.replace_page_facts(replacement, source_index, updated_at_epoch_ms)
        })
    }

    fn validate_page_replacement(
        &self,
        replacement: &WorkspacePageFactReplacement,
    ) -> Result<usize, MutationError> {
        if replacement.facts.is_empty() {
            return Err(MutationError::EmptyPageFacts);
        }
        let source_index = self
            .source_index(replacement.source_id)
            .ok_or(MutationError::UnknownSource)?;
        if self
            .sources
            .get(source_index)
            .ok_or(MutationError::InvalidSnapshot)?
            .excluded
        {
            return Err(MutationError::SourceExcluded);
        }
        if replacement.facts.iter().any(|fact| {
            fact.kind != FactKind::FromPage
                || fact.sources.as_slice() != [replacement.source_id]
                || fact.page_scope() != Some(replacement.scope)
                || fact.correction.is_some()
                || fact.has_conflict
        }) {
            return Err(MutationError::InvalidSnapshot);
        }
        let retained = self.facts.iter().filter(|fact| {
            fact.kind != FactKind::FromPage
                || fact.sources.as_slice() != [replacement.source_id]
                || fact.page_scope() != Some(replacement.scope)
        });
        if retained.count().saturating_add(replacement.facts.len()) > MAX_FACTS {
            return Err(MutationError::TooManyFacts);
        }
        Ok(source_index)
    }

    fn replace_page_facts(
        &mut self,
        replacement: WorkspacePageFactReplacement,
        source_index: usize,
        updated_at_epoch_ms: u64,
    ) -> Result<(), MutationError> {
        self.facts.retain(|fact| {
            fact.kind != FactKind::FromPage
                || fact.sources.as_slice() != [replacement.source_id]
                || fact.page_scope() != Some(replacement.scope)
        });
        self.facts.extend(replacement.facts);
        self.facts.sort_by_key(|fact| fact.fact_id);
        self.sources
            .get_mut(source_index)
            .ok_or(MutationError::InvalidSnapshot)?
            .read_at_epoch_ms = updated_at_epoch_ms;
        Ok(())
    }

    fn finish_mutation(
        &self,
        updated_at_epoch_ms: u64,
        mutate: impl FnOnce(&mut Self) -> Result<(), MutationError>,
    ) -> Result<Self, MutationError> {
        let revision = self
            .revision
            .checked_add(1)
            .ok_or(MutationError::RevisionOverflow)?;
        let mut next = self.clone();
        mutate(&mut next)?;
        next.revision = revision;
        next.last_updated_epoch_ms = updated_at_epoch_ms;
        next.validate()
            .then_some(next)
            .ok_or(MutationError::InvalidSnapshot)
    }
}
