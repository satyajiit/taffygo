// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded model-visible projections of the live page aggregate.

use core::fmt::Write as _;

use bip_types::identity::TabId;
use task_engine::{CurrentDocument, HandleTable, PageReadability, PersonsPages};

use super::{
    LivePage, ObservedPage, ProjectedPage, StagedDomQuery, MAX_RESEARCH_PROJECTION_BYTES,
    PER_RESEARCH_SOURCE_BUDGET_BYTES, RESEARCH_EVIDENCE_INSTRUCTION, SOURCE_RENDER_OVERHEAD_BYTES,
};
use crate::context::arena::{DomQueryProjection, Readability};
use crate::context::page::media::append_media_projection;
use crate::context::render::{
    render, render_query, PageProjection, RenderBudget, PER_VIEW_BUDGET_BYTES,
};
use crate::context::vocabulary::{
    no_query_match_line, unreadable_page_line, EMPTY_PAGE_LINE, PERSONS_PAGE_LINE,
};

struct ProjectionAccumulator {
    text: String,
    handles: HandleTable,
    offered: u32,
    omitted: u32,
    unreadable_nodes: u32,
    unreadable_text_bytes: u64,
    carries_page_content: bool,
    saw_unreadable: bool,
    /// A readable page that a staged query narrowed to nothing.
    ///
    /// This exists because `carries_page_content` is not free to answer it.
    /// That flag answers one question — do page-authored bytes leave in this
    /// request — and decision 0070 routes on the answer: it picks the
    /// disclosure class and the sensitivity classes a provider must be
    /// allowed for. A query that matched nothing sends no page bytes, so
    /// `false` is the correct answer there and must stay `false`.
    ///
    /// The readability verdict was being derived from that same flag, and
    /// these are the one case where the two answers differ: no bytes leave,
    /// and the page is emphatically there. So a model that narrowed a
    /// 350-node page to nothing was told its page was blank, stopped
    /// trusting the handles it had just been given, and reached back to a
    /// page its tab had left — four refused calls in one errand, on a phone,
    /// before it handed the errand to the person (decision 0210).
    ///
    /// Only ever widens: this can turn an `Empty` verdict into a `Readable`
    /// one and never the reverse, so no page that reads as blank today can
    /// start reading as blank tomorrow because of it.
    query_hid_a_readable_page: bool,
}

impl ProjectionAccumulator {
    fn new(handles: HandleTable, carries_page_content: bool, capacity_hint: usize) -> Self {
        let mut text = String::with_capacity(capacity_hint);
        text.push_str(RESEARCH_EVIDENCE_INSTRUCTION);
        Self {
            text,
            handles,
            offered: 0,
            omitted: 0,
            unreadable_nodes: 0,
            unreadable_text_bytes: 0,
            carries_page_content,
            saw_unreadable: false,
            query_hid_a_readable_page: false,
        }
    }

    /// Records that a query left nothing, and what the page behind it said.
    ///
    /// The page is asked, not the match set. Both callers reach the same
    /// state by different routes — an empty position list never reaches the
    /// renderer, and a match set of silent nodes reaches it and comes back
    /// `Empty` — and a rule stated once cannot disagree with itself.
    fn note_query_matched_nothing(&mut self, page: &ObservedPage) {
        let readable = matches!(page.arena.readability(), Readability::Readable);
        self.query_hid_a_readable_page |= readable;
        self.text.push_str(no_query_match_line(readable));
        self.text.push('\n');
    }

    fn append_source(
        &mut self,
        page: &ObservedPage,
        staged_query: Option<&StagedDomQuery>,
        source_number: usize,
        remaining_sources: usize,
        persons_pages: &PersonsPages,
    ) {
        let remaining = MAX_RESEARCH_PROJECTION_BYTES.saturating_sub(self.text.len());
        let share = remaining / remaining_sources.max(1);
        let mut heading = format!("Source {source_number}:\nEvidence status: ");
        let complete = staged_query.map_or_else(
            || page.evidence.supports_complete_result(),
            |query| query.projection.complete,
        );
        heading.push_str(if complete {
            "complete observation"
        } else {
            "incomplete observation; evidence is missing or may be missing"
        });
        heading.push_str(".\n");
        // The tab every number this source issues will carry, which is the
        // tab `call_target` refuses on, so the heading and the refusal cannot
        // name different pages. Part of the heading, so the budget below pays
        // for it out of this source's own share (decision 0237).
        if persons_pages.holds(&page.identity.tab_id) {
            heading.push_str(PERSONS_PAGE_LINE);
            heading.push('\n');
        }
        if let Some(query) = staged_query {
            append_query_status(&mut heading, &query.projection);
        }
        self.text.push_str(&heading);

        let budget = share
            .saturating_sub(heading.len())
            .saturating_sub(SOURCE_RENDER_OVERHEAD_BYTES)
            .min(PER_RESEARCH_SOURCE_BUDGET_BYTES)
            .min(PER_VIEW_BUDGET_BYTES);
        if staged_query.is_some_and(|query| query.projection.is_empty()) {
            self.note_query_matched_nothing(page);
            return;
        }
        if let Some(query) = staged_query {
            self.omitted = self.omitted.saturating_add(
                u32::try_from(
                    query
                        .projection
                        .matched
                        .saturating_sub(query.projection.retained()),
                )
                .unwrap_or(u32::MAX),
            );
        }
        self.append_rendered_page(page, staged_query, budget);
        if let Some(media) = &page.media {
            self.carries_page_content |= append_media_projection(
                &mut self.text,
                media,
                source_number,
                MAX_RESEARCH_PROJECTION_BYTES,
            );
        }
    }

    fn append_rendered_page(
        &mut self,
        page: &ObservedPage,
        staged_query: Option<&StagedDomQuery>,
        budget: usize,
    ) {
        let projected = if let Some(query) = staged_query {
            render_query(
                &page.arena,
                &query.projection,
                &page.identity,
                RenderBudget::hard(budget),
                &mut self.handles,
            )
        } else {
            render(
                &page.arena,
                &page.identity,
                RenderBudget::hard(budget),
                &mut self.handles,
            )
        };
        match projected {
            PageProjection::Rendered(rendered) => {
                self.carries_page_content = true;
                self.offered = self
                    .offered
                    .saturating_add(u32::try_from(rendered.offered).unwrap_or(u32::MAX));
                self.omitted = self
                    .omitted
                    .saturating_add(u32::try_from(rendered.omitted).unwrap_or(u32::MAX));
                self.text.push_str(&rendered.text);
            }
            PageProjection::Empty => {
                // `render_query` reaches this arm for a match set whose nodes
                // all say nothing — the position list was not empty, so the
                // guard in `append_source` did not fire, and
                // `readability_for` answered `Empty` about the handful of
                // nodes it was given rather than about the document. The
                // blank-page sentence is only ever about a document.
                if staged_query.is_some() {
                    self.note_query_matched_nothing(page);
                } else {
                    self.text.push_str(EMPTY_PAGE_LINE);
                    self.text.push('\n');
                }
            }
            PageProjection::Unreadable {
                node_count,
                text_bytes,
            } => {
                self.saw_unreadable = true;
                self.unreadable_nodes = self
                    .unreadable_nodes
                    .saturating_add(u32::try_from(node_count).unwrap_or(u32::MAX));
                self.unreadable_text_bytes = self.unreadable_text_bytes.saturating_add(text_bytes);
                let _ = writeln!(
                    self.text,
                    "{}",
                    unreadable_page_line(node_count, text_bytes)
                );
            }
        }
    }
}

const fn projection_capacity_hint(source_count: usize, text_bytes: usize) -> usize {
    let requested = RESEARCH_EVIDENCE_INSTRUCTION
        .len()
        .saturating_add(source_count.saturating_mul(SOURCE_RENDER_OVERHEAD_BYTES))
        .saturating_add(text_bytes);
    if requested < MAX_RESEARCH_PROJECTION_BYTES {
        requested
    } else {
        MAX_RESEARCH_PROJECTION_BYTES
    }
}

fn append_query_status(heading: &mut String, projection: &DomQueryProjection) {
    if projection.complete {
        let _ = writeln!(
            heading,
            "Query result: {} matching nodes.",
            projection.matched
        );
    } else {
        let _ = writeln!(
            heading,
            "Query result: {} visible matching nodes; withheld content, an observation gap, or the result limit may hide more.",
            projection.matched
        );
    }
}

impl LivePage {
    /// Renders the current pages into a clone of the table, issuing nothing
    /// into `self`.
    ///
    /// The caller commits with [`Self::commit_handles`] only after the body
    /// that used this preview is accepted, so a refused composition does not
    /// spend numbers on a request that never left.
    pub fn preview(&self) -> ProjectedPage {
        self.preview_for_empty_tab(None, &PersonsPages::default())
    }

    /// Renders the current pages, using one exact task-owned tab only when no
    /// page is current. The fallback is identity, not content or authority;
    /// it lets a proposal made from no page still name the tab the browser
    /// already committed. A turn reaches that state twice over: before the
    /// first read, and after a settled navigation retires the page bytes the
    /// last read left. The caller owes a tab in both (decision 0178).
    ///
    /// A source standing in one of `persons_pages` says in its heading that it
    /// is the page the person asked from and may only be read. An empty set —
    /// a zero-source errand, a task with no tab of its own — renders exactly
    /// as [`Self::preview`] does (decision 0237).
    pub fn preview_for_empty_tab(
        &self,
        empty_tab: Option<&TabId>,
        persons_pages: &PersonsPages,
    ) -> ProjectedPage {
        let Some(first) = self.pages.first() else {
            return self.empty_projection(empty_tab);
        };
        let media_attachment = self.sole_media_attachment();
        let text_bytes = self.pages.iter().fold(0_usize, |total, page| {
            total.saturating_add(page.arena.text_bytes())
        });
        let mut projection = ProjectionAccumulator::new(
            self.handles.clone(),
            media_attachment.is_some(),
            projection_capacity_hint(self.pages.len(), text_bytes),
        );

        for (index, page) in self.pages.iter().enumerate() {
            let remaining_sources = self.pages.len().saturating_sub(index).max(1);
            let staged_query = self
                .staged_query
                .as_ref()
                .filter(|query| query.source_id == page.source_id);
            projection.append_source(
                page,
                staged_query,
                index.saturating_add(1),
                remaining_sources,
                persons_pages,
            );
        }
        debug_assert!(projection.text.len() <= MAX_RESEARCH_PROJECTION_BYTES);
        ProjectedPage {
            text: Some(projection.text),
            tab: first.identity.tab_id.clone(),
            handles: projection.handles,
            carries_page_content: projection.carries_page_content,
            media_attachment,
            documents: self
                .pages
                .iter()
                .map(|page| CurrentDocument {
                    tab: page.evidence.tab_id.clone(),
                    frame: page.evidence.frame_id.clone(),
                    page_epoch: page.evidence.page_epoch.clone(),
                })
                .collect(),
            // Two questions, and `carries_page_content` only answers the
            // other one. It says whether page-authored bytes leave in this
            // request, which is the disclosure fact decision 0070 routes on;
            // this says whether there is a page. They agree everywhere
            // except behind a query that matched nothing (decision 0210).
            readability: if projection.carries_page_content || projection.query_hid_a_readable_page
            {
                PageReadability::Readable
            } else if projection.saw_unreadable {
                PageReadability::Unreadable
            } else {
                PageReadability::Empty
            },
            offered: projection.offered,
            omitted: projection.omitted,
            unreadable_nodes: projection.unreadable_nodes,
            unreadable_text_bytes: projection.unreadable_text_bytes,
        }
    }

    fn empty_projection(&self, tab: Option<&TabId>) -> ProjectedPage {
        ProjectedPage {
            text: None,
            tab: tab.cloned().unwrap_or_else(|| TabId::new(String::new())),
            handles: self.handles.clone(),
            carries_page_content: false,
            media_attachment: None,
            documents: Vec::new(),
            readability: PageReadability::Empty,
            offered: 0,
            omitted: 0,
            unreadable_nodes: 0,
            unreadable_text_bytes: 0,
        }
    }

    fn sole_media_attachment(&self) -> Option<super::MediaAttachment> {
        let mut attachments = self
            .pages
            .iter()
            .filter_map(|page| page.media.as_ref()?.attachment.as_ref());
        attachments
            .next()
            .filter(|_| attachments.next().is_none())
            .cloned()
    }

    /// Keeps the table a preview issued into, after that preview's body was
    /// accepted.
    pub fn commit_handles(&mut self, handles: HandleTable) {
        self.handles = handles;
        self.staged_query = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn small_page_projection_does_not_reserve_the_global_maximum() {
        let hint = projection_capacity_hint(1, 64);

        assert_eq!(
            hint,
            RESEARCH_EVIDENCE_INSTRUCTION.len() + SOURCE_RENDER_OVERHEAD_BYTES + 64
        );
        assert!(hint < MAX_RESEARCH_PROJECTION_BYTES);
    }

    #[test]
    fn page_projection_capacity_hint_is_saturating_and_bounded() {
        assert_eq!(
            projection_capacity_hint(usize::MAX, usize::MAX),
            MAX_RESEARCH_PROJECTION_BYTES
        );
    }
}
