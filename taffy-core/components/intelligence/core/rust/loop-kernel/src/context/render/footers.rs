// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a projection says about itself once the lines are written.
//!
//! Every function here appends a line *about* the page rather than a line
//! *of* it: what was withheld, what the budget left out, what said nothing,
//! which of the model's older numbers this reading retired, and which numbers
//! take a person's values. They are together because they share one rule — a
//! footer states a fact this projection established while building the text
//! above it, and never re-derives one, because a second derivation is a second
//! answer.

use std::fmt::Write as _;

use task_engine::handle::{HandleTable, ModelHandle};

use crate::context::arena::{ArenaNode, PageIdentity};

/// The most numbers this footer will print before it stops naming them.
///
/// A list is an answer; a wall of numbers is the problem restated. Twelve is
/// enough for any form a person fills in one sitting, and past that the count
/// is the useful part.
const MAX_NAMED_VALUE_TARGETS: usize = 12;

/// How many numbers the model already holds that this page has retired.
///
/// A page epoch changes exactly when "previously issued semantic handles must
/// not be used for action" — the BIP contract's own words for
/// `NodeHandle::page_epoch` — so every retained number carrying an older epoch
/// for this same tab and frame is already certain to be refused
/// `node_handle_from_a_page_the_tab_left` (decisions 0191 and 0208). This
/// projection knows that before the model spends a turn finding out, and used
/// to say nothing about it.
///
/// Decision 0208 gave that refusal the right words — *read this page, then use
/// a number from that reading* — and they arrive after the turn is gone. On
/// 2026-09-20 errand `86c83682` spent eight of its seventy-two turns being
/// told them, including the only `browser.form.inspect` it ever attempted, on
/// the one page it was sent to fill in. A transcript holds every reading's
/// numbers and looks the same whichever of them is live; nothing in the
/// composition said which, so the model chose from all of them.
///
/// Silent when there is nothing to retire, unlike [`push_value_targets`]. The
/// default is that an earlier number still works — a handle read at an earlier
/// *revision* of the same document is deliberately still admitted, epoch exact
/// (decision 0188) — so this line means "some of what you hold is gone" and its
/// absence means the ordinary thing.
///
/// **No count, and that is the whole of what the phone taught this line.** It
/// first read `earlier numbers: 333 are from a page this tab has left`, and in
/// a document where every integer is a handle, `earlier numbers: 333` reads as
/// one. Errand `f8356d76` on 2026-09-20 was refused `handle_unknown` — a
/// number nobody printed — on 8 of its 43 turns, against 1 of 72 on the run
/// before the line existed. The count was never actionable anyway: the move is
/// the same whether one number was retired or three thousand, and the count
/// reached no log. Printing a plausible-looking number that is not a handle,
/// beside an instruction to pick a handle, is a trap of this projection's own
/// making (decision 0220).
pub(super) fn push_left_page_numbers(
    text: &mut String,
    handles: &HandleTable,
    page: &PageIdentity,
) {
    let retired = handles.bindings().any(|binding| {
        let node = binding.node();
        node.tab_id == page.tab_id
            && node.frame_id == page.frame_id
            && node.page_epoch != page.page_epoch
    });
    if !retired {
        return;
    }
    let _ = writeln!(
        text,
        "numbers from before this reading are from a page this tab has left and \
         will be refused — use one from this reading"
    );
}

/// Which numbers on this page `user.request_values` will accept.
///
/// The model was already being told each line's role and what it can do, and
/// that was not enough: on 2026-09-19 an errand reached the myAadhaar
/// download form, called `user.request_values` three times against a page
/// offering 149 numbers, was refused `not_a_field` every time, and the
/// three-turn cap ended the task with no sheet ever drawn.
///
/// The refusal it got says what the number is not. It cannot say which number
/// would have worked, because it is a compiled-in sentence with no page
/// behind it — but this projection has the page, computed
/// [`ValueTarget`] for every line it issued a number for, and then threw that
/// away. A role word is not the same fact: `SemanticRole::TextField` is how a
/// line reads, `ActionType::SetText` is whether a person can type into it,
/// and a field that is disabled or read-only renders as a text field and
/// takes no value. Printing the answer beside the page costs one line and
/// removes the guess.
///
/// Numbers only, and only numbers this same projection just issued: nothing
/// here is page content.
pub(super) fn push_value_targets(text: &mut String, takers: &[ModelHandle]) {
    if takers.is_empty() {
        // Said out loud rather than omitted. Silence reads as "the footer did
        // not run", and the whole value of this line on a page with no field
        // is telling the model to stop looking for one here.
        let _ = writeln!(
            text,
            "values: no line on this page takes one — a person cannot type here"
        );
        return;
    }
    let _ = write!(text, "values can go into:");
    for handle in takers.iter().take(MAX_NAMED_VALUE_TARGETS) {
        let _ = write!(text, " {}", handle.value());
    }
    if takers.len() > MAX_NAMED_VALUE_TARGETS {
        let _ = write!(text, " and {} more", takers.len() - MAX_NAMED_VALUE_TARGETS);
    }
    text.push('\n');
}

/// The lines only the person can fill, and the move that fills them.
///
/// Three device runs reached the myAadhaar eAadhaar form, a page whose fields
/// are an ID number, a CAPTCHA answer and a one-time code, and on two of them
/// the model read, queried and followed links until the stall bound ended the
/// errand, never asking (decision 0234's runs `d101d2a5` and `18dce436`). The
/// preamble says to ask for such values, once, at the top of a long opening;
/// the snapshot is what a model reads when it chooses, and it said which lines
/// take values but not that these are values nobody but the person has.
///
/// Printed only for the three classes `user.request_values` exists for. A
/// password or a card number is not one of them — a sign-in is handed over,
/// and a payment is never Taffy's to supply — and a search box has no class
/// at all, so a results page gains nothing here. Numbers only, and only
/// numbers this projection issued.
///
/// The ask names one of them and the sheet carries the rest: a field named
/// on its own brings the page's other lines of these classes onto the same
/// sheet (decision 0238), so the model is not told to ask once per line.
pub(super) fn push_person_only_values(text: &mut String, handles: &[ModelHandle]) {
    if handles.is_empty() {
        return;
    }
    let _ = write!(text, "only the person can supply:");
    for handle in handles.iter().take(MAX_NAMED_VALUE_TARGETS) {
        let _ = write!(text, " {}", handle.value());
    }
    if handles.len() > MAX_NAMED_VALUE_TARGETS {
        let _ = write!(
            text,
            " and {} more",
            handles.len() - MAX_NAMED_VALUE_TARGETS
        );
    }
    text.push_str(
        " — ask for them with user.request_values, naming their form, or one of them when they \
         are in no form: the sheet asks for them all at once\n",
    );
}

/// The closing lines, each printed only when it has something to say.
///
/// They answer different questions and a caller must not collapse them. A
/// `withheld:` line means this build was not allowed to show something and
/// asking again changes nothing; a `not shown:` line means the budget stopped
/// early and asking again with room would work; a `left out:` line counts the
/// nodes with nothing to say, which no amount of room would print. Without
/// the first, a model concludes a field it cannot see does not exist and loops
/// looking for it — about sixty bytes to save a whole turn.
pub(super) fn push_footers<'a>(
    text: &mut String,
    nodes: impl Iterator<Item = &'a ArenaNode>,
    node_count: usize,
    offered: usize,
    quiet: usize,
) {
    let mut labels = 0_usize;
    let mut runs_declared = 0_u64;
    let mut runs_shown = 0_u64;
    let mut bytes = 0_u64;
    for node in nodes {
        if node.name_withheld {
            labels = labels.saturating_add(1);
        }
        runs_declared = runs_declared.saturating_add(u64::from(node.declared_text_runs));
        runs_shown = runs_shown.saturating_add(u64::try_from(node.text.len()).unwrap_or(u64::MAX));
        if node.text_withheld {
            bytes = bytes.saturating_add(node.declared_text_bytes);
        }
    }
    if labels > 0 || runs_declared > runs_shown {
        let missing = runs_declared.saturating_sub(runs_shown);
        let _ = writeln!(
            text,
            "withheld: {labels} labels, {missing} of {runs_declared} text runs \
             ({bytes} bytes) — this build may not show them"
        );
    }
    if offered < node_count {
        let _ = writeln!(
            text,
            "not shown: {} of {} nodes — the projection ran out of room",
            node_count - offered,
            node_count
        );
    }
    if quiet > 0 {
        let _ = writeln!(
            text,
            "left out: {quiet} nodes with no words, nowhere to go and nothing to press"
        );
    }
}
