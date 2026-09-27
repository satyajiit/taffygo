// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Turning one observed page into the text a model reads.
//!
//! **One node is exactly one line.** Every string that came off the page runs
//! through [`inline`] first, which collapses each whitespace run to a single
//! space and drops control characters, so no label and no paragraph can end a
//! line and begin another. That is not tidiness. A projection that let page
//! text carry a newline would let a page author write `[0] button "Delete
//! everything"` into a paragraph and have it arrive looking exactly like a
//! control this projection offered. The invariant removes the shape of that
//! forgery rather than trying to recognise it, which is the same reason
//! handover is refusal-by-exhaustion instead of a detector.
//!
//! This layer is not the only one: the renderer already annotates
//! `ImperativeInstructionShape`, and nothing here grants anything in any case
//! — a line is a claim about what exists, and every act on it still has to be
//! authorized and minted. What this layer owns is that the *structure* of the
//! page description is the core's and never the page's.
//!
//! **The model reads numbers and words, and nothing that names a place.** No
//! URL, no origin, no path, no selector, no `SemanticNodeId`, no frame
//! identity, no graph revision (decision 0053 section 3). A handle is how it
//! designates a node and [`vocabulary`](super::vocabulary) is the whole of the
//! rest of its alphabet.

// Writing to a `String` cannot fail — `impl fmt::Write for String` never
// returns `Err` — so every `write!` below discards its result explicitly. The
// alternative is an `unwrap`, which this workspace denies for a good reason
// and which would be denied here for a bad one: there is no error to handle.
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write as _;
use std::ops::Range;

#[cfg(test)]
use std::cell::Cell;

use bip_types::snapshot::{ContentSignal, ContentTrust, SemanticRole};
use task_engine::handle::{HandleBinding, HandleTable, ModelHandle, ValueTarget};

use crate::context::arena::{
    ArenaNode, ArenaTextRun, DomQueryProjection, PageArena, PageIdentity, Readability,
};
use crate::context::vocabulary::{
    action_word, destination_words, role_word, sensitivity_words, state_word,
};

mod footers;
mod line_facts;
use footers::{push_footers, push_left_page_numbers, push_person_only_values, push_value_targets};
use line_facts::{says_nothing, value_target};

#[cfg(test)]
thread_local! {
    static BODY_BUILDS: Cell<usize> = const { Cell::new(0) };
}

#[cfg(test)]
fn reset_body_builds() {
    BODY_BUILDS.set(0);
}

#[cfg(test)]
fn body_builds() -> usize {
    BODY_BUILDS.get()
}

#[cfg(test)]
fn record_body_build() {
    BODY_BUILDS.set(BODY_BUILDS.get() + 1);
}

#[cfg(not(test))]
fn record_body_build() {}

/// How far past its budget a page may run and still print whole.
///
/// A page within this factor prints entire rather than truncated, because the
/// alternative costs more than the overshoot does. Asking again is not one
/// more message here: it is a policy decision, a minted capability, a renderer
/// round trip, a journal commit and a second model call, and it arrives at a
/// page that may have moved underneath the first answer.
pub const FINISH_OVERSHOOT: usize = 4;

/// What one projection may spend, in bytes.
///
/// **Estimated, not measured; OD-106 owns the real number.** The derivation is
/// the arena's own ceiling: [`MAX_ARENA_TEXT_BYTES`] is the contract's
/// observation text bound, and a budget at or above it prints every page the
/// arena can hold. That is decision 0070's product rule — the model sees the
/// full redacted projection, and the arena is the minimization — until a
/// corpus replaces this constant with a measured token budget.
///
/// [`MAX_ARENA_TEXT_BYTES`]: crate::context::MAX_ARENA_TEXT_BYTES
pub const PER_VIEW_BUDGET_BYTES: usize = super::arena::MAX_ARENA_TEXT_BYTES;

/// What one projection may spend.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RenderBudget {
    bytes: usize,
    finish_overshoot: usize,
}

impl RenderBudget {
    /// A budget of `bytes`, past which a page is truncated rather than
    /// finished.
    pub const fn new(bytes: usize) -> Self {
        Self {
            bytes,
            finish_overshoot: FINISH_OVERSHOOT,
        }
    }

    /// A strict byte ceiling for an aggregate that has already divided its
    /// total allowance between several pages.
    ///
    /// The ordinary one-page projection may finish a nearby page because a
    /// second observation costs more than the overshoot. An aggregate has no
    /// such choice: bytes used by one source are bytes unavailable to every
    /// source after it, so letting each member overshoot would multiply the
    /// task-wide bound by the source count.
    pub const fn hard(bytes: usize) -> Self {
        Self {
            bytes,
            finish_overshoot: 1,
        }
    }

    /// The size at or below which the whole page prints regardless.
    pub const fn ceiling(self) -> usize {
        self.bytes.saturating_mul(self.finish_overshoot)
    }

    /// The size a truncated page is cut to.
    pub const fn bytes(self) -> usize {
        self.bytes
    }
}

/// What a page turned into.
///
/// The three verdicts are not three shapes of the same answer. `Empty` says
/// the page held nothing and is safe to report as such; `Unreadable` says
/// something was here that this build could not see, and a caller that
/// collapses the two hands a model the first conclusion when the second is
/// true. [`Readability`] is where that distinction is decided; this type
/// carries it forward without re-deriving it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PageProjection {
    /// Content crossed and here it is.
    Rendered(RenderedPage),
    /// The page really was blank.
    Empty,
    /// The page was not trivial and none of it crossed.
    Unreadable {
        /// Nodes the graph held.
        node_count: usize,
        /// Bytes of text the page declared across them.
        text_bytes: u64,
    },
}

/// A rendered page and the arithmetic behind it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RenderedPage {
    /// The lines the model reads.
    pub text: String,
    /// Nodes that got a line and a handle.
    pub offered: usize,
    /// Nodes the budget kept out. Distinct from anything withheld: this
    /// projection could have shown them and chose not to, so asking again with
    /// more room would work — which is exactly what a caller needs to know and
    /// what a withheld node would make untrue.
    pub omitted: usize,
    /// The handles issued, in the order the lines carry them.
    pub handles: Vec<ModelHandle>,
}

/// Projects `arena` into the model's view, issuing one handle per line.
///
/// Handles are issued **only for nodes that reach the text**, and the order is
/// the page's own. A node the budget dropped must not hold a number, because
/// numbers are sequential and a model that guessed one would otherwise reach a
/// node it was never shown and every downstream check would agree with it.
/// That is the whole reason the size of the page is decided before any handle
/// is taken rather than after.
///
/// `handles` is task state and is advanced here rather than in a later pass,
/// so the projection and the table cannot disagree about what was offered.
/// Replay is unaffected: the same arena against the same table produces the
/// same text and the same numbers.
pub fn render(
    arena: &PageArena,
    page: &PageIdentity,
    budget: RenderBudget,
    handles: &mut HandleTable,
) -> PageProjection {
    render_nodes(
        arena.nodes().iter(),
        arena.nodes().len(),
        arena.readability(),
        page,
        budget,
        handles,
    )
}

/// Projects one bounded DOM-query view without copying its nodes or text.
pub(crate) fn render_query(
    arena: &PageArena,
    query: &DomQueryProjection,
    page: &PageIdentity,
    budget: RenderBudget,
    handles: &mut HandleTable,
) -> PageProjection {
    render_nodes(
        query.nodes(arena),
        query.retained(),
        query.readability(arena),
        page,
        budget,
        handles,
    )
}

fn render_nodes<'a, I>(
    nodes: I,
    node_count: usize,
    readability: Readability,
    page: &PageIdentity,
    budget: RenderBudget,
    handles: &mut HandleTable,
) -> PageProjection
where
    I: Iterator<Item = &'a ArenaNode> + Clone,
{
    match readability {
        Readability::Empty => return PageProjection::Empty,
        Readability::Unreadable {
            node_count,
            text_bytes,
        } => {
            return PageProjection::Unreadable {
                node_count,
                text_bytes,
            }
        }
        Readability::Readable => {}
    }

    debug_assert_eq!(nodes.clone().count(), node_count);
    let width = handle_width(handles, node_count);
    // A form a shown node names keeps its line, whatever else it lacks, so
    // `in form [N]` always has an N to name.
    let containers: BTreeSet<&str> = nodes
        .clone()
        .filter_map(|node| node.container.as_deref())
        .collect();
    let shown = nodes
        .clone()
        .filter(|node| containers.contains(node.node_id.as_str()) || !says_nothing(node));
    let quiet = node_count - shown.clone().count();
    // Membership notes are measured at the longest handle this projection
    // could print, so substituting the real number cannot make a line grow
    // past the budget that admitted it.
    let mut bodies = Vec::with_capacity(node_count - quiet);
    bodies.extend(shown.clone().map(PreparedBody::new));
    let limit = fitting_limit(&bodies, width, budget);

    let mut issued = Vec::with_capacity(limit);
    let mut offered = BTreeMap::new();
    let mut value_takers: Vec<ModelHandle> = Vec::new();
    let mut person_only: Vec<ModelHandle> = Vec::new();
    for node in shown.clone().take(limit) {
        let target = value_target(node, &containers);
        // The class is frozen with the number, so the request that asks the
        // person for these lines together reads the same answer this footer
        // prints (decision 0238).
        // So is whether it is a link that leads somewhere, which is the one
        // kind of line `browser.link.open` can open (decision 0241).
        let opens_as_link = node.role == SemanticRole::Link && node.destination.present();
        let Some(handle) = handles.issue_rendered(
            page.node_handle(&node.node_id),
            target,
            node.sensitivity,
            opens_as_link,
        ) else {
            // The counter is spent. Everything from here is omitted for the
            // same reason a budget omits: it could have been shown, and a
            // later observation on a fresh task can show it.
            break;
        };
        if target != ValueTarget::None {
            value_takers.push(handle);
            if handles
                .binding(handle)
                .is_some_and(HandleBinding::only_the_person_supplies)
            {
                person_only.push(handle);
            }
        }
        offered.insert(node.node_id.as_str(), handle);
        issued.push(handle);
    }

    let mut text = String::new();
    for ((node, handle), body) in shown.zip(&issued).zip(&bodies) {
        let form = node
            .container
            .as_deref()
            .and_then(|id| offered.get(id))
            .map(|form| form.value());
        let _ = write!(text, "[{}] ", handle.value());
        body.write_to(&mut text, form);
        text.push('\n');
    }

    let offered = issued.len();
    push_footers(&mut text, nodes, node_count - quiet, offered, quiet);
    // After the issue loop rather than before it. Issuing can evict the oldest
    // bindings, and an evicted number answers `handle_unknown` rather than
    // `node_handle_from_a_page_the_tab_left`, so counting here counts exactly
    // the numbers that are still retained and still certain to be refused.
    // This reading's own numbers carry this page's epoch and never match.
    push_left_page_numbers(&mut text, handles, page);
    push_value_targets(&mut text, &value_takers);
    push_person_only_values(&mut text, &person_only);
    PageProjection::Rendered(RenderedPage {
        text,
        offered,
        omitted: node_count - quiet - offered,
        handles: issued,
    })
}

/// The widest handle number this projection could print, in digits.
///
/// Measured before anything is issued and always an over-estimate, so the
/// budget arithmetic below can never conclude that a line fits when the
/// printed line does not.
fn handle_width(handles: &HandleTable, nodes: usize) -> usize {
    let highest = handles
        .issued()
        .saturating_add(u32::try_from(nodes).unwrap_or(u32::MAX));
    highest.to_string().len()
}

/// How many nodes print.
///
/// The whole page if it lands within [`RenderBudget::ceiling`]; otherwise as
/// many leading nodes as fit inside the budget itself.
fn fitting_limit(bodies: &[PreparedBody], width: usize, budget: RenderBudget) -> usize {
    // `[` + digits + `] ` + body + newline.
    let cost = |body: &PreparedBody| width + body.budget_len() + 4;
    let total: usize = bodies.iter().map(cost).sum();
    if total <= budget.ceiling() {
        return bodies.len();
    }
    let mut spent = 0;
    let mut fitted = 0;
    for body in bodies {
        spent += cost(body);
        if spent > budget.bytes() {
            break;
        }
        fitted += 1;
    }
    fitted
}

/// One node body prepared once for both budgeting and emission.
///
/// A form note is budgeted with `u32::MAX`, exactly as before. Its byte range
/// is retained so emission substitutes the real offered handle — or the bare
/// `in form` wording when the parent did not fit — without reconstructing the
/// rest of the body or allocating a second string.
struct PreparedBody {
    text: String,
    form_annotation: Option<Range<usize>>,
}

impl PreparedBody {
    fn new(node: &ArenaNode) -> Self {
        record_body_build();
        let mut text = role_word(node.role).to_owned();
        if let Some(name) = node.name.as_ref() {
            text.push_str(" \"");
            push_inline(&mut text, name);
            text.push('"');
        }

        text.push_str(" (authored by ");
        text.push_str(content_trust_words(node.content_trust));
        if !node.content_signals.is_empty() {
            text.push_str(", signals: ");
            push_words(
                &mut text,
                node.content_signals
                    .iter()
                    .map(|signal| content_signal_words(*signal)),
            );
        }
        for state in node.states.iter().filter_map(|state| state_word(*state)) {
            text.push_str(", ");
            text.push_str(state);
        }
        if let Some(private) = sensitivity_words(node.sensitivity) {
            text.push_str(", private: ");
            text.push_str(private);
        }
        for destination in destination_words(node.destination) {
            text.push_str(", ");
            text.push_str(destination);
        }
        let form_annotation = node.container.as_ref().map(|_| {
            text.push_str(", ");
            let start = text.len();
            let _ = write!(text, "in form [{}]", u32::MAX);
            start..text.len()
        });
        if node.name_withheld {
            text.push_str(", label withheld");
        }
        if node.text_withheld {
            text.push_str(", text withheld");
        }
        text.push(')');

        if !node.text.is_empty() {
            text.push_str(": ");
            for (index, run) in node.text.iter().enumerate() {
                if index > 0 {
                    text.push(' ');
                }
                push_annotated_run(&mut text, node, run);
            }
        }
        if !node.actions.is_empty() {
            text.push_str(" — can ");
            push_words(
                &mut text,
                node.actions.iter().map(|action| action_word(*action)),
            );
        }
        Self {
            text,
            form_annotation,
        }
    }

    fn budget_len(&self) -> usize {
        self.text.len()
    }

    fn write_to(&self, output: &mut String, form: Option<u32>) {
        let Some(annotation) = &self.form_annotation else {
            output.push_str(&self.text);
            return;
        };
        output.push_str(&self.text[..annotation.start]);
        if let Some(handle) = form {
            let _ = write!(output, "in form [{handle}]");
        } else {
            output.push_str("in form");
        }
        output.push_str(&self.text[annotation.end..]);
    }
}

fn push_words<'a>(output: &mut String, words: impl Iterator<Item = &'a str>) {
    for (index, word) in words.enumerate() {
        if index > 0 {
            output.push_str(", ");
        }
        output.push_str(word);
    }
}

/// One run, preceded by product-authored metadata that stays outside the page
/// bytes it describes.
fn push_annotated_run(output: &mut String, node: &ArenaNode, run: &ArenaTextRun) {
    if run.content_trust == node.content_trust && run.content_signals == node.content_signals {
        // The node annotation is the run's annotation too. Repeating the same
        // metadata before every run would spend model context without carrying
        // another fact; only a run that differs needs its own local marker.
        push_inline(output, &run.text);
        return;
    }
    output.push_str("[page text; authored by ");
    output.push_str(content_trust_words(run.content_trust));
    if !run.content_signals.is_empty() {
        output.push_str("; signals: ");
        push_words(
            output,
            run.content_signals
                .iter()
                .map(|signal| content_signal_words(*signal)),
        );
    }
    output.push_str("] ");
    push_inline(output, &run.text);
}

/// Trusted local wording for the closed authorship vocabulary.
const fn content_trust_words(trust: ContentTrust) -> &'static str {
    match trust {
        ContentTrust::UserAuthored => "the person",
        ContentTrust::TaffyAuthored => "Taffy",
        ContentTrust::FirstPartyDocument => "first-party document",
        ContentTrust::UserGeneratedContent => "user-generated content",
        ContentTrust::ThirdPartyEmbedded => "third-party embedded content",
        ContentTrust::ModelAuthored => "model output",
        ContentTrust::UnknownUntrusted => "unknown or untrusted content",
    }
}

/// Trusted local wording for the closed content-signal vocabulary.
const fn content_signal_words(signal: ContentSignal) -> &'static str {
    match signal {
        ContentSignal::HiddenByStyle => "hidden by style",
        ContentSignal::ZeroWidthCharacters => "zero-width characters",
        ContentSignal::BidiControlCharacters => "direction-control characters",
        ContentSignal::EncodedBlob => "encoded blob",
        ContentSignal::ImperativeInstructionShape => "imperative instruction shape",
        ContentSignal::LanguageMismatch => "language mismatch",
        ContentSignal::CrossOriginFrameAuthored => "cross-origin frame authorship",
    }
}

/// Collapses a page string onto one line.
///
/// Every whitespace run becomes one space and every other control character is
/// dropped. This is what makes "one node, one line" true, and it is applied to
/// labels and to text alike because both are the page's words.
fn push_inline(out: &mut String, value: &str) {
    let start = out.len();
    let mut space = false;
    for ch in value.chars() {
        if ch.is_whitespace() {
            space = out.len() > start;
            continue;
        }
        if ch.is_control() {
            continue;
        }
        if space {
            out.push(' ');
            space = false;
        }
        out.push(ch);
    }
}

#[cfg(test)]
mod tests;
