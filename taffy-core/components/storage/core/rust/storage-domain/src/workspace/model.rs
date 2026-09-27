// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Validated, platform-neutral workspace state.

use crate::ids::{FactId, SourceId, WorkspaceId};

mod media;

use media::valid_media_provenance;
pub use media::{
    WorkspaceMediaEvidenceKind, WorkspaceMediaFactKind, WorkspaceMediaKind,
    WorkspaceMediaProvenance, WorkspacePageFactScope, MAX_CONFIDENCE_PPM, MAX_MEDIA_LOCATOR_BYTES,
};

pub const MAX_SOURCES: usize = 64;
pub const MAX_FACTS: usize = 256;
pub const MAX_FACT_SOURCES: usize = 16;
pub const MAX_GOAL_BYTES: usize = 8_192;
pub const MAX_DISPLAY_NAME_BYTES: usize = 256;
pub const MAX_TITLE_BYTES: usize = 1_024;
pub const MAX_HOST_BYTES: usize = 253;
/// Maximum bytes in one browser-owned canonical source locator.
pub const MAX_SOURCE_LOCATOR_BYTES: usize = 4_096;
pub const MAX_FIELD_BYTES: usize = 256;
pub const MAX_VALUE_BYTES: usize = 16_384;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspacePhase {
    Running,
    WaitingForUser,
    Paused,
    Done,
    PartlyDone,
    Stopped,
    Failed,
}

impl WorkspacePhase {
    pub(crate) const fn wire(self) -> u8 {
        match self {
            Self::Running => 0,
            Self::WaitingForUser => 1,
            Self::Paused => 2,
            Self::Done => 3,
            Self::PartlyDone => 4,
            Self::Stopped => 5,
            Self::Failed => 6,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Running),
            1 => Some(Self::WaitingForUser),
            2 => Some(Self::Paused),
            3 => Some(Self::Done),
            4 => Some(Self::PartlyDone),
            5 => Some(Self::Stopped),
            6 => Some(Self::Failed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspaceTemplate {
    CompareProducts,
    SummarizeEvidence,
    BuildSourceTable,
    WebErrand,
}

impl WorkspaceTemplate {
    pub(crate) const fn wire(self) -> u8 {
        match self {
            Self::CompareProducts => 0,
            Self::SummarizeEvidence => 1,
            Self::BuildSourceTable => 2,
            Self::WebErrand => 3,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::CompareProducts),
            1 => Some(Self::SummarizeEvidence),
            2 => Some(Self::BuildSourceTable),
            3 => Some(Self::WebErrand),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FactKind {
    FromPage,
    Summarized,
    TaffyInference,
    UserEntered,
}

impl FactKind {
    pub(crate) const fn wire(self) -> u8 {
        match self {
            Self::FromPage => 0,
            Self::Summarized => 1,
            Self::TaffyInference => 2,
            Self::UserEntered => 3,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::FromPage),
            1 => Some(Self::Summarized),
            2 => Some(Self::TaffyInference),
            3 => Some(Self::UserEntered),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceSource {
    pub source_id: SourceId,
    pub title: String,
    pub host: String,
    /// Last committed canonical HTTP(S) page address, when it was safe to
    /// retain. The browser strips fragments and withholds addresses carrying
    /// credentials or query material before this value enters the core.
    ///
    /// `None` is intentional and makes the source non-refreshable. In
    /// particular, snapshots written before schema v4 decode with no locator.
    pub canonical_locator: Option<String>,
    pub read_at_epoch_ms: u64,
    pub excluded: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceFact {
    pub fact_id: FactId,
    pub field: String,
    pub value: String,
    pub kind: FactKind,
    pub sources: Vec<SourceId>,
    pub correction: Option<String>,
    pub has_conflict: bool,
    /// Present only for media-derived page facts. DOM facts retain `None`.
    pub media_provenance: Option<WorkspaceMediaProvenance>,
}

impl WorkspaceFact {
    pub const fn page_scope(&self) -> Option<WorkspacePageFactScope> {
        if !matches!(self.kind, FactKind::FromPage) {
            return None;
        }
        Some(match &self.media_provenance {
            Some(provenance) => WorkspacePageFactScope::Media(provenance.media_kind),
            None => WorkspacePageFactScope::Dom,
        })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceSnapshot {
    pub workspace_id: WorkspaceId,
    pub revision: u64,
    /// Mutable user-facing name. The task goal remains immutable evidence.
    pub display_name: String,
    pub goal: String,
    pub phase: WorkspacePhase,
    /// Whether the person explicitly chose to keep this task result.
    ///
    /// False is temporary task-session retention. It may support crash
    /// recovery and the live task screen, but must never appear in the saved
    /// workspace list or Library until an explicit save mutation commits.
    pub saved: bool,
    pub last_updated_epoch_ms: u64,
    pub template: WorkspaceTemplate,
    pub sources: Vec<WorkspaceSource>,
    pub facts: Vec<WorkspaceFact>,
}

impl WorkspaceSnapshot {
    pub fn validate(&self) -> bool {
        if self.revision == 0
            || !valid_display_name(&self.display_name)
            || self.goal.is_empty()
            || self.goal.len() > MAX_GOAL_BYTES
            || self.sources.len() > MAX_SOURCES
            || self.facts.len() > MAX_FACTS
            || !strictly_ordered(self.sources.iter().map(|source| source.source_id))
            || !strictly_ordered(self.facts.iter().map(|fact| fact.fact_id))
        {
            return false;
        }
        self.sources.iter().all(valid_source)
            && self
                .facts
                .iter()
                .all(|fact| valid_fact(fact, &self.sources))
    }

    pub fn source(&self, id: SourceId) -> Option<&WorkspaceSource> {
        self.source_index(id)
            .and_then(|index| self.sources.get(index))
    }

    pub fn fact(&self, id: FactId) -> Option<&WorkspaceFact> {
        self.fact_index(id).and_then(|index| self.facts.get(index))
    }

    pub fn fact_needs_new_source(&self, fact: &WorkspaceFact) -> bool {
        !fact.sources.is_empty()
            && fact
                .sources
                .iter()
                .all(|source_id| self.source(*source_id).is_none_or(|source| source.excluded))
    }

    pub(crate) fn source_index(&self, id: SourceId) -> Option<usize> {
        self.sources
            .binary_search_by_key(&id, |source| source.source_id)
            .ok()
    }

    pub(crate) fn fact_index(&self, id: FactId) -> Option<usize> {
        self.facts
            .binary_search_by_key(&id, |fact| fact.fact_id)
            .ok()
    }
}

/// Derives the first bounded display name without changing the immutable goal.
pub fn initial_display_name(goal: &str) -> String {
    let trimmed = goal.trim();
    let source = if trimmed.is_empty() {
        "Workspace"
    } else {
        trimmed
    };
    let end = source
        .char_indices()
        .map(|(index, character)| index + character.len_utf8())
        .take_while(|end| *end <= MAX_DISPLAY_NAME_BYTES)
        .last()
        .unwrap_or(0);
    source.get(..end).unwrap_or_default().to_owned()
}

pub(crate) fn valid_display_name(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_DISPLAY_NAME_BYTES
        && value.trim() == value
        && !value.chars().any(char::is_control)
}

fn valid_source(source: &WorkspaceSource) -> bool {
    !source.title.is_empty()
        && source.title.len() <= MAX_TITLE_BYTES
        && valid_host(&source.host)
        && source
            .canonical_locator
            .as_deref()
            .is_none_or(|locator| valid_canonical_locator(locator, &source.host))
}

/// Verifies the narrow address shape admitted by the browser boundary.
///
/// This is deliberately not a general URL parser. Shipping callers must pass
/// a canonical `GURL`; portable storage independently rejects every shape
/// that could carry credentials, query data, fragments, whitespace, or a host
/// different from the display host.
fn valid_canonical_locator(locator: &str, expected_host: &str) -> bool {
    if locator.is_empty()
        || locator.len() > MAX_SOURCE_LOCATOR_BYTES
        || !locator.is_ascii()
        || locator
            .bytes()
            .any(|byte| byte.is_ascii_control() || byte == b' ')
        || locator.contains(['?', '#', '\\'])
    {
        return false;
    }
    let Some(after_scheme) = locator
        .strip_prefix("https://")
        .or_else(|| locator.strip_prefix("http://"))
    else {
        return false;
    };
    let (authority, path) = after_scheme.split_once('/').unwrap_or((after_scheme, ""));
    if authority.is_empty()
        || authority.contains('@')
        || (!path.is_empty() && locator.ends_with(':'))
    {
        return false;
    }
    let host = if let Some(ipv6) = authority.strip_prefix('[') {
        let Some((address, suffix)) = ipv6.split_once(']') else {
            return false;
        };
        if !suffix.is_empty()
            && (!suffix.starts_with(':')
                || suffix.get(1..).is_none_or(|port| {
                    port.is_empty() || !port.bytes().all(|byte| byte.is_ascii_digit())
                }))
        {
            return false;
        }
        address
    } else {
        let (host, port) = authority
            .rsplit_once(':')
            .map_or((authority, None), |(host, port)| (host, Some(port)));
        if port
            .is_some_and(|port| port.is_empty() || !port.bytes().all(|byte| byte.is_ascii_digit()))
        {
            return false;
        }
        host
    };
    host == expected_host
}

fn valid_host(host: &str) -> bool {
    if host.is_empty()
        || host.len() > MAX_HOST_BYTES
        || !host.is_ascii()
        || host.contains(['/', '?', '#', '@'])
    {
        return false;
    }
    if host.contains(':') {
        return host.parse::<std::net::Ipv6Addr>().is_ok();
    }
    host.bytes()
        .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-'))
}

fn valid_fact(fact: &WorkspaceFact, sources: &[WorkspaceSource]) -> bool {
    if fact.field.is_empty()
        || fact.field.len() > MAX_FIELD_BYTES
        || fact.value.is_empty()
        || fact.value.len() > MAX_VALUE_BYTES
        || fact.sources.len() > MAX_FACT_SOURCES
        || !strictly_ordered(fact.sources.iter().copied())
        || fact.sources.iter().any(|source_id| {
            sources
                .binary_search_by_key(source_id, |source| source.source_id)
                .is_err()
        })
        || fact
            .correction
            .as_ref()
            .is_some_and(|value| value.is_empty() || value.len() > MAX_VALUE_BYTES)
        || fact.media_provenance.as_ref().is_some_and(|provenance| {
            fact.kind != FactKind::FromPage || !valid_media_provenance(provenance)
        })
    {
        return false;
    }
    fact.kind == FactKind::UserEntered || !fact.sources.is_empty()
}

fn strictly_ordered<T: Ord + Copy>(values: impl Iterator<Item = T>) -> bool {
    let mut previous = None;
    for value in values {
        if previous.is_some_and(|prior| prior >= value) {
            return false;
        }
        previous = Some(value);
    }
    true
}
