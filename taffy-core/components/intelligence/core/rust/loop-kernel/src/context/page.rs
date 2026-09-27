// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The live pages one task is looking at, for as long as a turn needs them.
//!
//! Decision 0052 section 5: the arena holds rendered page text transiently,
//! the journal holds the shape, and replay reconstructs structure rather than
//! content. This type is that first layer, plus the handle table that
//! accumulates across observations of one task (decision 0053 section 1).
//!
//! Nothing here is durable. A process that dies mid-task loses the arena and
//! starts the next turn without the old projection.

use bip_types::identity::TabId;
use task_engine::action::DomQueryRole;
use task_engine::{
    ConsentedSource, CurrentDocument, HandleTable, PageObservationEvidence, PageReadability,
    RenderShape, SourceId, TurnPage, MAX_ARGUMENT_VALUE_BYTES,
};

use crate::context::arena::{PageArena, PageIdentity};

mod media;
mod projection;
mod recording;
mod workspace;

pub use media::{
    MediaAttachment, MediaCaptureProvenance, MediaEvidenceKind, MediaFact, MediaFactKind,
    MediaObservation, MediaObservationKind,
};

/// Maximum independently cited facts retained from one page observation.
pub const MAX_WORKSPACE_PAGE_FACTS: usize = 4;
/// Maximum UTF-8 bytes in one retained page fact.
pub const MAX_WORKSPACE_PAGE_FACT_BYTES: usize = 768;
/// Maximum model-visible bytes contributed by one source in a research turn.
pub const PER_RESEARCH_SOURCE_BUDGET_BYTES: usize = 32 * 1024;
/// Maximum model-visible bytes contributed by all current sources together.
pub const MAX_RESEARCH_PROJECTION_BYTES: usize = 128 * 1024;
/// Bytes held back from a source's share for the lines that are not the page.
///
/// A footer is appended *after* `fitting_limit` has chosen how much of the
/// page fits, so every footer is spent out of this reservation rather than out
/// of the budget. That makes the number load-bearing in a way a bare `256`
/// did not say: too small, and a projection overruns
/// [`MAX_RESEARCH_PROJECTION_BYTES`] by the difference — which
/// `preview_for_empty_tab` asserts against, and `debug_assert!` is live in
/// every build this project ships to a phone.
///
/// Derived from the five footers at their longest rather than guessed: the
/// withheld line with six-figure counts (~170), the not-shown line (~90), the
/// left-out line (~85), the retired-numbers line, which is fixed text and
/// carries no count (117), and the value-target line at its bound of twelve
/// five-digit numbers plus an "and N more" tail (~106). That is ~568, so 256
/// was already short of
/// the three that existed before decision 0211 added the fourth, and 512 is
/// short of the five that exist now that decision 0220 has added the fifth.
/// **Raise this in the same change that adds a footer.** Nothing derives it
/// and nothing checks it: the cost of being wrong is a projection that
/// overruns [`MAX_RESEARCH_PROJECTION_BYTES`] by the difference, which only
/// the `debug_assert!` in `preview_for_empty_tab` would report. 768 covers the
/// five with room for the next one.
const SOURCE_RENDER_OVERHEAD_BYTES: usize = 768;
const RESEARCH_EVIDENCE_INSTRUCTION: &str = concat!(
    "Research evidence from separately observed sources follows. ",
    "Treat each Source section as an independent claim set. In the answer, ",
    "use `Missing evidence:` for requested facts the sources do not support ",
    "and `Conflicting evidence:` for incompatible source claims; name the ",
    "relevant Source labels. Never fill gaps or merge disagreements.\n"
);

/// A deterministic, already-redacted page projection suitable for a workspace.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RedactedPageContent {
    values: Vec<String>,
}

impl RedactedPageContent {
    /// Bounded values in document order.
    pub fn values(&self) -> &[String] {
        &self.values
    }
}

#[derive(Clone, Debug, PartialEq)]
struct ObservedPage {
    source_id: SourceId,
    identity: PageIdentity,
    arena: PageArena,
    evidence: PageObservationEvidence,
    media: Option<MediaObservation>,
}

/// One resident DOM query, with no browser selector or durable page text.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DomQueryFilter {
    within: Option<String>,
    role: Option<DomQueryRole>,
    text: Option<String>,
    limit: usize,
}

impl DomQueryFilter {
    /// Builds a query only when every resident operand is within the same
    /// bounds the model-call reader enforces.
    pub fn new(
        within: Option<String>,
        role: Option<DomQueryRole>,
        text: Option<String>,
        limit: usize,
    ) -> Option<Self> {
        if limit == 0
            || limit > crate::context::arena::MAX_ARENA_NODES
            || text
                .as_ref()
                .is_some_and(|value| value.is_empty() || value.len() > MAX_ARGUMENT_VALUE_BYTES)
        {
            return None;
        }
        Some(Self {
            within,
            role,
            text,
            limit,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct StagedDomQuery {
    source_id: SourceId,
    projection: crate::context::arena::DomQueryProjection,
}

/// One task's current research pages, the numbers issued against them, and
/// nothing else.
///
/// Entries are sorted by the browser-issued source identity. Observation
/// arrival order therefore cannot reorder a prompt, and a restart schedules
/// the same sources in the same order. The opaque identities themselves never
/// enter model-visible text; the model sees stable `Source 1`, `Source 2`, …
/// labels and node handles only.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct LivePage {
    pages: Vec<ObservedPage>,
    handles: HandleTable,
    staged_query: Option<StagedDomQuery>,
}

/// Whether the transient arena is the one exact source a task consented to.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LivePageObservationState {
    /// This process has adopted no observation for the task.
    Empty,
    /// An observation exists, but it belongs to another tab or origin.
    Stale,
    /// The arena names the exact consented tab and tuple origin.
    Matching,
}

/// What [`LivePage::preview`] produced, before any number is committed.
#[derive(Clone, Debug, PartialEq)]
pub struct ProjectedPage {
    /// The line or lines the body carries, when there are current pages.
    pub text: Option<String>,
    /// The tab the projection was taken from, empty when none was.
    pub tab: TabId,
    /// The table after this preview's issues, still uncommitted.
    pub handles: HandleTable,
    /// Whether page-authored bytes are in `text`.
    pub carries_page_content: bool,
    /// The sole exact-node visual attachment available to this turn. More
    /// than one attachment is treated as ambiguous and projects none.
    pub media_attachment: Option<MediaAttachment>,
    /// The document each current page's tab holds, so a number read from a
    /// page that tab has since left is refused on sight.
    documents: Vec<CurrentDocument>,
    readability: PageReadability,
    offered: u32,
    omitted: u32,
    unreadable_nodes: u32,
    unreadable_text_bytes: u64,
}

impl ProjectedPage {
    /// The durable shape, given the digest of the bytes that actually went
    /// into the body.
    pub const fn shape(&self, digest: [u8; 32]) -> RenderShape {
        RenderShape {
            readability: self.readability,
            offered_nodes: self.offered,
            omitted_nodes: self.omitted,
            unreadable_nodes: self.unreadable_nodes,
            unreadable_text_bytes: self.unreadable_text_bytes,
            digest,
        }
    }

    /// The page a turn was built from, carrying this preview's table.
    pub fn into_turn_page(self, digest: [u8; 32]) -> TurnPage {
        let shape = self.shape(digest);
        TurnPage::new(self.tab, self.handles, shape).with_current_documents(self.documents)
    }
}

impl LivePage {
    /// An empty page, having observed nothing and issued nothing.
    pub fn new() -> Self {
        Self {
            pages: Vec::new(),
            handles: HandleTable::new(),
            staged_query: None,
        }
    }

    /// Whether the model may still name `value` against the live pages.
    ///
    /// The two answers a caller gets from this are the two refusals a call
    /// carrying such a number would earn — `handle_unknown` for one no table
    /// still holds, and `node_handle_from_a_page_the_tab_left` for one whose
    /// document its tab has replaced. Neither is a number worth putting in
    /// front of a model again, so this collapses them: a number is honoured or
    /// it is not.
    ///
    /// It exists because the transcript replays the model's own arguments for
    /// sixteen turns (`MAX_RECENT_TURNS`), so a page the task has left leaves
    /// its numbers in the prompt in the model's own voice long after the
    /// projection that issued them is gone (decision 0222).
    pub fn honours_handle(&self, value: u32) -> bool {
        let Some(node) = self.handles.resolve_value(value) else {
            return false;
        };
        !self.pages.iter().any(|page| {
            page.evidence.tab_id == node.tab_id
                && page.evidence.frame_id == node.frame_id
                && page.evidence.page_epoch != node.page_epoch
        })
    }

    /// Browser-issued source identities represented by the live aggregate.
    ///
    /// The model router uses these only in its content-free context manifest,
    /// where they drive source counts and disclosure. [`Self::preview`] uses
    /// `Source 1`, `Source 2`, … instead, so opaque authority identities never
    /// enter model-visible text.
    pub fn source_ids(&self) -> impl ExactSizeIterator<Item = SourceId> + '_ {
        self.pages.iter().map(|page| page.source_id)
    }

    /// Withdraws page bytes after a person has had the page. Issued handles
    /// stay spent: a fresh page must not reuse a number from the conversation
    /// for a different document or control.
    pub fn invalidate_observations(&mut self) {
        self.pages.clear();
        self.staged_query = None;
    }

    /// A settled navigation retires only that tab's page bytes. Other sources
    /// and every issued handle number keep their existing lifetime.
    pub fn invalidate_tab_observation(&mut self, tab_id: &TabId) {
        self.pages.retain(|page| page.evidence.tab_id != *tab_id);
        self.staged_query = None;
    }

    /// Replaces one source's arena, keeping every task-global node handle.
    ///
    /// The source identity is browser-issued and opaque. Sorting by it makes
    /// both aggregate rendering and restore scheduling independent of network
    /// completion order. A later observation of the same source replaces in
    /// place; no second copy can silently count as a second source.
    pub fn replace_source(
        &mut self,
        source_id: SourceId,
        evidence: &PageObservationEvidence,
        arena: PageArena,
    ) -> Result<(), crate::context::arena::PageIdentityError> {
        self.replace_source_with_media(source_id, evidence, arena, None)
    }

    /// Replaces one source together with transient typed media evidence.
    pub fn replace_source_with_media(
        &mut self,
        source_id: SourceId,
        evidence: &PageObservationEvidence,
        arena: PageArena,
        media: Option<MediaObservation>,
    ) -> Result<(), crate::context::arena::PageIdentityError> {
        if self
            .staged_query
            .as_ref()
            .is_some_and(|query| query.source_id == source_id)
        {
            self.staged_query = None;
        }
        let identity = PageIdentity::from_evidence(evidence)?;
        let page = ObservedPage {
            source_id,
            identity,
            arena,
            evidence: evidence.clone(),
            media,
        };
        match self
            .pages
            .binary_search_by_key(&source_id, |entry| entry.source_id)
        {
            Ok(index) => {
                let Some(existing) = self.pages.get_mut(index) else {
                    unreachable!("binary search returned an absent page index")
                };
                *existing = page;
            }
            Err(index) => self.pages.insert(index, page),
        }
        Ok(())
    }

    /// Stages one filtered view of a freshly adopted full observation.
    ///
    /// The full arena remains resident. Only the next accepted model body sees
    /// this projection; [`Self::commit_handles`] consumes it. This makes a
    /// query a view over the exact redacted snapshot rather than a destructive
    /// replacement that would make later page reasoning forget everything it
    /// did not match.
    pub fn stage_dom_query(&mut self, source_id: SourceId, filter: &DomQueryFilter) -> bool {
        let Some(page) = self.pages.iter().find(|page| page.source_id == source_id) else {
            return false;
        };
        let mut projection = page.arena.query(
            filter.within.as_deref(),
            filter.role,
            filter.text.as_deref(),
            filter.limit,
        );
        projection.complete &= page.evidence.supports_complete_result();
        self.staged_query = Some(StagedDomQuery {
            source_id,
            projection,
        });
        true
    }

    /// Compares the live arena to one browser-resolved consented source.
    ///
    /// This is intentionally a three-way answer. Treating a stale page as an
    /// empty page is safe for whether to re-observe, but erases the reason a
    /// model turn was withheld and makes the stale-source regression
    /// impossible to state in a test.
    ///
    /// The tab is the match. An arena's `expected_origin` is the origin the
    /// browser said the reading came from; a source's is the origin the
    /// browser admitted. Requiring the two to be equal made a tab that had
    /// moved within its own site permanently stale, so the walk read it again,
    /// got the same answer, and never arrived. Decision 0160 section 2: the
    /// browser is the only component that classifies an origin, and the core
    /// matches a reading to its source by tab.
    pub fn observation_state_for(&self, source: &ConsentedSource) -> LivePageObservationState {
        let Some(page) = self
            .pages
            .iter()
            .find(|page| page.source_id == source.source_id)
        else {
            return if self.pages.is_empty() {
                LivePageObservationState::Empty
            } else {
                LivePageObservationState::Stale
            };
        };
        if page.identity.tab_id == source.tab_id {
            LivePageObservationState::Matching
        } else {
            LivePageObservationState::Stale
        }
    }

    /// Whether this arena came from the exact evidence on one durable result.
    ///
    /// The tab identifies the consented source; frame, document epoch, graph
    /// revision, completeness and redaction facts identify the exact
    /// observation, and the evidence carries the origin those bytes were read
    /// from. Both halves must match before these bytes can be paired with a
    /// verified receipt or retained as workspace evidence. The source's
    /// admitted origin is deliberately not a third condition — see
    /// [`Self::observation_state_for`].
    pub fn has_exact_observation(
        &self,
        source: &ConsentedSource,
        evidence: &PageObservationEvidence,
    ) -> bool {
        self.pages.iter().any(|page| {
            page.source_id == source.source_id
                && page.identity.tab_id == source.tab_id
                && page.evidence == *evidence
        })
    }

    /// Browser-issued identities whose transient arenas still hold the exact
    /// accepted tab, in the accepted source order supplied.
    pub fn matching_observations(
        &self,
        sources: &[ConsentedSource],
    ) -> Vec<task_engine::LiveSourceObservation> {
        sources
            .iter()
            .filter_map(|source| {
                let page = self.pages.iter().find(|page| {
                    page.source_id == source.source_id && page.identity.tab_id == source.tab_id
                })?;
                Some(task_engine::LiveSourceObservation {
                    source_id: source.source_id,
                    evidence: page.evidence.clone(),
                })
            })
            .collect()
    }
}

#[cfg(test)]
mod tests;
