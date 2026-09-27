// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one observed page looks like while the core is still holding it.
//!
//! This is the arena decision 0052 section 5 requires: the one place page text
//! lives inside the core, owned, bounded, and dropped when the turn that asked
//! for it ends. Nothing here is durable. The journal beneath it retains counts
//! and closed enumerations by construction, and a page's words are not among
//! them — replay reconstructs the *shape* of what happened, never what the page
//! said.
//!
//! The distinction the arena exists to make possible is between a page that
//! holds nothing and a page this build could not read. A projection with no
//! content over a graph with fifty nodes is not an empty page: it is an
//! unreadable one, and the difference matters because an empty result otherwise
//! travels on as evidence that the page held nothing.
//!
//! Beside the page sits the *conversation*: [`transcript`] holds the ordered
//! turns of one task, bounded by the same argument and dropped by the same
//! rule. It is here rather than beside the composer because it is context in
//! exactly the sense this module means — what a model is shown, held for as
//! long as a turn needs it, and never durable.

pub mod arena;
pub mod opening;
pub mod page;
pub mod refusal;
pub mod render;
pub mod transcript;
pub mod vocabulary;

pub use crate::context::arena::{
    ArenaNode, ArenaTextRun, DestinationClass, DomQueryProjection, PageArena, PageIdentity,
    PageIdentityError, Readability, MAX_ARENA_CONTAINS_EDGES, MAX_ARENA_NODES,
    MAX_ARENA_TEXT_BYTES,
};
pub use crate::context::page::{
    DomQueryFilter, LivePage, LivePageObservationState, MediaAttachment, MediaCaptureProvenance,
    MediaEvidenceKind, MediaFact, MediaFactKind, MediaObservation, MediaObservationKind,
    ProjectedPage, RedactedPageContent, MAX_RESEARCH_PROJECTION_BYTES, MAX_WORKSPACE_PAGE_FACTS,
    MAX_WORKSPACE_PAGE_FACT_BYTES, PER_RESEARCH_SOURCE_BUDGET_BYTES,
};
pub use crate::context::render::{
    render, PageProjection, RenderBudget, RenderedPage, FINISH_OVERSHOOT, PER_VIEW_BUDGET_BYTES,
};
pub use crate::context::transcript::{
    RecentTurns, RecordedCall, TaskTranscript, TranscriptBudget, TranscriptViews, TurnExchange,
    MAX_RECENT_SAID_BYTES, MAX_RECENT_TURNS, MAX_TRANSCRIPT_BYTES,
};
