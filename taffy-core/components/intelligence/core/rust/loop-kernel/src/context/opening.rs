// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Compiled-in guidance for the task's opening turn.
//!
//! An errand's model is told the game rather than left to infer it from the
//! tool schemas: how a site is reached, what the page looks like when it
//! arrives, who supplies a value only the person knows, and what finishing
//! means (decision 0136). Every sentence here is compiled in. The only values
//! that vary are two counts the core already holds — sites so far and sites
//! still allowed — and a flag saying whether the task has a tab of its own.
//! Comparison guidance explains how to distinguish offers without inventing
//! missing facts. No page text, model text or goal text passes through this module.

use task_engine::{FieldValueAskOutcome, HeldValuesPlaced};

/// A comparison answers from the offers observed, within their comparability.
pub const COMPARISON_PREAMBLE: &str = concat!(
    "Answer the requested comparison directly, using observed evidence. Different sellers' ",
    "offers and prices are differences, not by themselves conflicting evidence; reserve ",
    "conflicts for incompatible claims about the same offer and attribute. Cite each offer's ",
    "Source label and observed title. For a price comparison, name the lowest observed offer ",
    "and calculate the price difference only when observed currency, product/variant and price ",
    "basis are comparable. Distinguish listed item prices from delivered totals; mention missing ",
    "costs or variant differences only when material. If comparison is unsupported, state what ",
    "prevents it. Never invent exchange rates or missing terms, or claim an observed offer is ",
    "cheapest beyond the sources read.",
);

/// What an errand is, and how it is walked.
///
/// The request-values sentence names the field as well as the form because
/// most sites draw their inputs with no form element around them. The browser
/// already takes one such field as the whole request, but the model was told
/// to name a form, found none in the snapshot, and on 2026-09-18 left the
/// eAadhaar page it had reached rather than ask for the number.
///
/// A CAPTCHA is asked for, not handed over. The sentence used to send it to
/// `user.handover`, which is what decision 0088 replaced: the request sheet
/// shows the person the picture and the browser holds their answer. On
/// 2026-09-18 a model on the eAadhaar form planned to ask for the number,
/// read "hand the page over for a CAPTCHA", and handed over the whole form
/// instead (decision 0188).
///
/// The first two sentences used to read "if you already know the official
/// https address, use browser.navigate directly", and that is what the model
/// did: on three device runs it typed a host it remembered, the host did not
/// resolve, and the errand ended on Chromium's error page. A results page is
/// what the web says today and an address a model recalls is not, so the
/// order is now the other way round (decision 0176).
pub const ERRAND_PREAMBLE: &str = concat!(
    "This task is an errand on a website. Find the official site with browser.search and follow ",
    "a numbered result with browser.link.open; a result is what the web says now, and an ",
    "address you recall may no longer exist. Use browser.navigate to a typed https address when ",
    "no result leads where you need to go, and if the browser answers that it did not land ",
    "there, that address is wrong — search again rather than typing another guess. ",
    "Read the page snapshot before acting, and act one step ",
    "at a time. Use semantic page, form, document and text tools first; use image inspection ",
    "or a screenshot only when those tools cannot provide the needed information. Search and ",
    "activate deferred tools when needed. Ask the person for values only they ",
    "know with user.request_values, naming the form, or the field itself when the snapshot ",
    "shows it in no form; a field marked private holds such a value, and so does a one-time ",
    "code, or the answer to a CAPTCHA, whose picture TaffyGo shows them. They type them in ",
    "TaffyGo and you never see them. Hand the page to the person with user.handover for a ",
    "sign-in, or when the browser will not take the request. When the person hands ",
    "back, read the fresh page and continue the original task; completing a handover or filling ",
    "a field does not finish it. Download an observed link with browser.download.from_link; ",
    "the browser resolves its address. Use browser.download.list to check that it ",
    "completed; a started download is still in progress. Finish when the requested outcome is ",
    "verified, then say in one sentence what was done and ask whether to open a downloaded PDF.",
);

/// How a page reaches the model.
///
/// The second sentence is here because of what a search results page looks
/// like from inside a snapshot. A result's visible title is a heading, and the
/// link that carries the address is a different node beside it; a model that
/// picks the one it can read picks the wrong one, and the browser can only
/// answer that the handle names nothing it may open. On three device runs that
/// was every `browser.link.open` the errand made (decision 0177).
pub const SNAPSHOT_LINE: &str = concat!(
    "Page snapshots show numbered handles and never addresses; act on a handle from the latest ",
    "snapshot. browser.link.open takes only a handle the snapshot shows as link and that says ",
    "\"this site\" or \"another site\" after it: that is the node carrying the address. A heading, ",
    "a button or the text of a result is not one, and neither is a link with no such words.",
);

/// What the model reads after a reply that did nothing.
///
/// The last clause is load-bearing and was, until recently, a trap: the nudge
/// invited the model to say plainly what could not be done, and the reducer
/// then counted that obedience as another unproductive reply. Three of them
/// ended the task. The nudge and the counter now agree — saying so is an
/// ending, and the model is told it is one, so it spends it deliberately
/// rather than as a way of being helpful.
pub const ERRAND_NUDGE: &str = concat!(
    "Nothing has been done yet. Use a tool or ask the person. If the work genuinely cannot be ",
    "done from here, say plainly what is blocking it — that ends the task as partly done, so do ",
    "not say it while a move is still worth trying.",
);

/// What the model reads after arriving where the task had already been.
pub const ERRAND_REPEAT_NUDGE: &str = concat!(
    "You have arrived back at a site this task already holds; searching again lands here again. ",
    "What leaves it is browser.navigate to an https address, or browser.link.open on a numbered ",
    "link from the latest snapshot. If neither can work, hand the page over with user.handover ",
    "or ask with user.request_values rather than looking again.",
);

/// What the model reads when its asks for values keep coming back empty.
///
/// Said before the bound rather than at it (decision 0216). The model has
/// already been given a move for the way each ask came back; this is the fact
/// none of those sentences carries, because each of them is about one ask and
/// this is about the run of them. A model that is told only "scroll it into
/// view, then ask again" three times has no way to know the third time is
/// different from the first.
pub const UNANSWERED_VALUES_NUDGE: &str = concat!(
    "Two asks for values in a row have come back with nothing. A third that comes back empty ",
    "ends this task, so do not ask the same way again: either do the move the last answer named ",
    "before asking, name a different field or the form itself, or hand the page over with ",
    "user.handover so the person can fill it in themselves.",
);

/// What the model reads when its turns have stopped changing anything.
///
/// Said two turns before the bound (decision 0233). Each of the other nudges
/// is about one shape of turn; this one is about the run, whatever shapes it
/// is made of, which is why it names what counts rather than what went wrong:
/// a model alternating a reading with a refused click has been told about the
/// click every time and has no way to see that the reading is the other half
/// of the same loop.
pub const STALLED_TURNS_NUDGE: &str = concat!(
    "Your last few turns have changed nothing: no new page was reached, nothing new was pressed, ",
    "filled or opened, and no answer came from the person. Reading the same page again does not ",
    "change that. One or two more turns like them end this task, so act on the latest snapshot ",
    "now. If it shows fields the task needs values for, ask the person with user.request_values ",
    "naming the form; press a button with browser.dom.click; or hand the page over with ",
    "user.handover if it cannot be done from here.",
);

/// What came of the task's last `user.request_values`, from the two facts the
/// core holds (decisions 0192 and 0215).
///
/// The call is not an action, so the conversation rebuilt from actions has no
/// exchange for it. On a phone an errand named the same FAQ heading four times,
/// and every answer of nothing reached the model as silence; an answer of two
/// values would have been as silent, with nothing saying they could be filled.
///
/// The fill sentence states the browser's own rule. It holds the values under
/// one approval for the fields the sheet showed, in the order it showed them,
/// on the page as it was when the person answered, and a fill that breaks the
/// order or names another field ends that approval.
///
/// A zero count takes its sentence from the outcome, and the sentence this
/// replaced is the reason why. It said the person closed the sheet *or* the
/// browser could not ask, and then told the model not to name that line
/// again — true, ambiguous, and on the myAadhaar CAPTCHA the exact opposite of
/// what was needed, since the picture was below the fold and naming it again
/// after a scroll was the whole fix. Its wording survives only for
/// [`FieldValueAskOutcome`] `None`, which is a command rebuilt from a journal
/// that does not record the outcome; there the "or" is honest.
pub fn values_answer(supplied: u32, outcome: Option<FieldValueAskOutcome>) -> String {
    if supplied == 0 {
        return unanswered(outcome).to_owned();
    }
    let mut line = String::with_capacity(512);
    line.push_str("The person answered your last user.request_values; the browser holds ");
    line.push_str(&supplied.to_string());
    line.push_str(if supplied == 1 { " value" } else { " values" });
    line.push_str(concat!(
        " you will not see. Fill them with browser.form.fill, one field per call, in the order ",
        "the fields appear on the page: value_from 0 into the first field the sheet asked about, ",
        "then 1 into the next. Name each field by its number in the latest snapshot. A fill out ",
        "of that order, or into another field, is refused. Then press the page's own button to ",
        "continue.",
    ));
    line
}

/// What came of the task's last `user.request_values`, once the task has had
/// the chance to put the person's values into their fields itself (decision
/// 0238).
///
/// The model is not asked between the answer and the fills, so by the time it
/// reads this the values are in the page or the first fill that did not go in
/// has stopped the rest. It is told which: that the fields are filled and must
/// not be filled again, and what the page wants next — its own button, or the
/// next field only the person can supply — or which positions are still held
/// and how to fill them, in the words [`values_answer`] has always used. When
/// the task did not place anything, because the answer was rebuilt from the
/// journal or the task may not fill a field, the sentence is
/// [`values_answer`]'s.
pub fn values_after_placing(
    placed: HeldValuesPlaced,
    supplied: u32,
    outcome: Option<FieldValueAskOutcome>,
) -> String {
    match placed {
        HeldValuesPlaced::NotPlaced => values_answer(supplied, outcome),
        HeldValuesPlaced::Placed { count } => values_placed(count),
        HeldValuesPlaced::Partly { placed, count } => values_partly_placed(placed, count),
    }
}

fn values_placed(count: u32) -> String {
    let mut line = String::with_capacity(384);
    line.push_str(
        "The person answered your last user.request_values and the browser has put their ",
    );
    if count == 1 {
        line.push_str("value into the field the sheet showed");
    } else {
        line.push_str(&count.to_string());
        line.push_str(" values into the fields the sheet showed");
    }
    line.push_str(concat!(
        "; you will not see them. Do not fill those fields again. Press the page's own button to ",
        "continue, or, when the page shows another field only the person can supply, ask for it ",
        "with user.request_values.",
    ));
    line
}

fn values_partly_placed(placed: u32, count: u32) -> String {
    let mut line = String::with_capacity(640);
    line.push_str("The person answered your last user.request_values; the browser holds ");
    line.push_str(&count.to_string());
    line.push_str(if count == 1 { " value" } else { " values" });
    line.push_str(" you will not see. ");
    if placed == 0 {
        line.push_str("None of them could be put into its field for you. Fill them");
    } else {
        line.push_str("It put the first ");
        line.push_str(&placed.to_string());
        line.push_str(concat!(
            " into the fields the sheet showed, and the rest could not be put in for you. Do not ",
            "fill those again. Fill the rest",
        ));
    }
    line.push_str(" with browser.form.fill, one field per call, in the order the fields appear ");
    line.push_str("on the page: value_from ");
    line.push_str(&placed.to_string());
    line.push_str(if placed == 0 {
        " into the first field the sheet asked about"
    } else {
        " into the next field the sheet asked about"
    });
    if count.saturating_sub(placed) > 1 {
        line.push_str(", then ");
        line.push_str(&placed.saturating_add(1).to_string());
        line.push_str(" into the one after it");
    }
    line.push_str(concat!(
        ". Name each field by its number in the latest snapshot. A fill out of that order, or ",
        "into another field, is refused. Then press the page's own button to continue.",
    ));
    line
}

/// The sentence for an ask that brought nothing back, one per outcome.
///
/// Each one ends in a move, because the reason a model is told anything here
/// is to choose the next call. `ChallengeOffScreen` is the member this
/// function exists for: the target was right, the sheet failed on geometry
/// alone, and the move is a scroll the task is already allowed to make —
/// which is why no browser-side scroll was added (decision 0215).
const fn unanswered(outcome: Option<FieldValueAskOutcome>) -> &'static str {
    match outcome {
        Some(FieldValueAskOutcome::Answered) => concat!(
            "Your last user.request_values was answered and held nothing: the person supplied no ",
            "values. Do not ask the same way again; say what is blocking the task, or hand the ",
            "page over with user.handover.",
        ),
        Some(FieldValueAskOutcome::Dismissed) => concat!(
            "The person saw your last user.request_values and closed it without filling anything ",
            "in. Asking the same way again asks them to refuse twice. Say what you need and why ",
            "in the reply, or hand the page over with user.handover so they can do it ",
            "themselves.",
        ),
        Some(FieldValueAskOutcome::NotAField) => concat!(
            "The line you named in user.request_values takes no value, so there was nothing to ",
            "ask about. Do not name that line again. Name the form the fields are in, or one ",
            "number the latest snapshot says takes a value.",
        ),
        Some(FieldValueAskOutcome::ChallengeOffScreen) => concat!(
            "Your last user.request_values named the right line: it is a challenge whose picture ",
            "is on this page but not in the part of it on screen, so the browser had nothing to ",
            "show the person. Scroll it into view with browser.dom.scroll on that same number, ",
            "then call user.request_values on it again.",
        ),
        Some(FieldValueAskOutcome::CannotBeShown) => concat!(
            "The browser could not build a sheet for the line you named in user.request_values — ",
            "no picture it could copy, no outline to draw, or no label to put on it. Asking ",
            "again the same way reaches the same place. Name a different field from the latest ",
            "snapshot, or hand the page over with user.handover.",
        ),
        Some(FieldValueAskOutcome::PageMoved) => concat!(
            "The page changed while your last user.request_values was being prepared, so the ",
            "line you named no longer described anything. Read the page again, then name the ",
            "field from the new snapshot.",
        ),
        Some(FieldValueAskOutcome::NoSurface) => concat!(
            "Nothing could draw a sheet for your last user.request_values, so the person was ",
            "never asked. Asking again cannot help. Say plainly what the task needs from them ",
            "and stop.",
        ),
        // Restored from the journal, which does not record the outcome. The
        // old ambiguous wording, kept for the one case where the ambiguity is
        // the truth rather than a gap.
        None => concat!(
            "Your last user.request_values brought back nothing, and this task was restored ",
            "since, so the reason is no longer held: the person closed the sheet, or the browser ",
            "could not ask about the line you named. Name the form the fields are in, or one ",
            "field that takes text, from the latest snapshot, or say what is blocking the task.",
        ),
    }
}

/// The errand's position, from counts the core holds.
///
/// `has_own_tab` says whether the task holds a blank tab of its own to open a
/// site in. Every errand whose consent granted discovery gets one, whether or
/// not it also started holding a page; a task restored without one does not.
/// An errand that holds both needs to be told, because otherwise the only tab
/// it knows about is the one it is reading and a search reads as a move that
/// would take that page away (decision 0224).
pub fn errand_situation(
    source_count: usize,
    remaining_new_source_cap: u32,
    has_own_tab: bool,
) -> String {
    let mut line = String::with_capacity(192);
    match (source_count, has_own_tab) {
        (0, true) => line.push_str("You have a blank tab of your own and no site yet"),
        (0, false) => line.push_str("You have no site yet"),
        (count, own_tab) => {
            line.push_str("You have ");
            line.push_str(&count.to_string());
            line.push_str(if count == 1 { " site" } else { " sites" });
            line.push_str(" so far");
            if own_tab {
                line.push_str(", and a blank tab of your own");
            }
        }
    }
    match remaining_new_source_cap {
        0 => line.push_str(
            ". The sites budget is spent: stay on the sites you have, or ask the person.",
        ),
        remaining => {
            if source_count == 0 {
                line.push_str(
                    "; browser.search or browser.navigate to an https address opens the first",
                );
            } else if has_own_tab {
                line.push_str(
                    "; browser.search opens the next in your blank tab, not in a tab you hold",
                );
            }
            line.push_str(". You may open ");
            line.push_str(&remaining.to_string());
            line.push_str(if remaining == 1 {
                " more new site."
            } else {
                " more new sites."
            });
        }
    }
    line
}

#[cfg(test)]
mod tests {
    use bip_types::ActionResultCode;
    use task_engine::tool::{Milestone, ToolLookup};

    use super::{
        errand_situation, values_after_placing, values_answer, FieldValueAskOutcome,
        HeldValuesPlaced, COMPARISON_PREAMBLE, ERRAND_NUDGE, ERRAND_PREAMBLE, ERRAND_REPEAT_NUDGE,
        SNAPSHOT_LINE, STALLED_TURNS_NUDGE, UNANSWERED_VALUES_NUDGE,
    };
    use crate::context::refusal::{refusal_sentence, refusal_sentence_for};
    use crate::context::vocabulary::{not_attempted_word, PERSONS_PAGE_LINE};

    /// Every member, so a new one cannot be added without a sentence.
    const EVERY_OUTCOME: [FieldValueAskOutcome; 7] = [
        FieldValueAskOutcome::Answered,
        FieldValueAskOutcome::Dismissed,
        FieldValueAskOutcome::NotAField,
        FieldValueAskOutcome::ChallengeOffScreen,
        FieldValueAskOutcome::CannotBeShown,
        FieldValueAskOutcome::PageMoved,
        FieldValueAskOutcome::NoSurface,
    ];

    /// The dotted words a sentence names as tools: a lowercase namespace, a
    /// dot, and a lowercase name, as every registered tool is spelt.
    fn tool_names_in(sentence: &str) -> Vec<&str> {
        sentence
            .split(|c: char| !(c.is_ascii_lowercase() || c == '.' || c == '_'))
            .map(|word| word.trim_matches('.'))
            .filter(|word| {
                let mut parts = word.split('.');
                parts.next().is_some_and(|head| !head.is_empty())
                    && parts.next().is_some_and(|tail| !tail.is_empty())
            })
            .collect()
    }

    /// A sentence that names a tool the registry does not hold sends the model
    /// looking for it. The off-screen CAPTCHA sentence named
    /// `action.scroll_into_view`; on a phone the model spent the errand's last
    /// allowed turn on `tool.search` for it, one scroll short of the sheet
    /// (decision 0234).
    #[test]
    fn every_tool_a_sentence_names_is_one_the_model_can_call() {
        let mut sentences: Vec<String> = [
            COMPARISON_PREAMBLE,
            ERRAND_PREAMBLE,
            SNAPSHOT_LINE,
            ERRAND_NUDGE,
            ERRAND_REPEAT_NUDGE,
            UNANSWERED_VALUES_NUDGE,
            STALLED_TURNS_NUDGE,
        ]
        .iter()
        .map(|sentence| (*sentence).to_owned())
        .collect();
        sentences.push(values_answer(0, None));
        sentences.push(values_answer(2, Some(FieldValueAskOutcome::Answered)));
        for placed in [
            HeldValuesPlaced::Placed { count: 1 },
            HeldValuesPlaced::Placed { count: 2 },
            HeldValuesPlaced::Partly {
                placed: 0,
                count: 2,
            },
            HeldValuesPlaced::Partly {
                placed: 1,
                count: 3,
            },
        ] {
            sentences.push(values_after_placing(placed, 2, None));
        }
        sentences.extend(
            EVERY_OUTCOME
                .iter()
                .map(|outcome| values_answer(0, Some(*outcome))),
        );
        for code in ActionResultCode::ALL {
            sentences.push(refusal_sentence(*code).to_owned());
            sentences.push(refusal_sentence_for("browser.dom.read", *code).to_owned());
        }
        // A call refused before it is attempted is answered in these words,
        // and a source heading can carry the last one (decision 0237).
        sentences.extend(
            task_engine::NotAttempted::ALL
                .iter()
                .map(|reason| not_attempted_word(*reason).to_owned()),
        );
        sentences.push(PERSONS_PAGE_LINE.to_owned());
        let mut named = 0_usize;
        for sentence in &sentences {
            for name in tool_names_in(sentence) {
                named += 1;
                assert!(
                    !matches!(
                        task_engine::tool::resolve(name, Milestone::M7),
                        ToolLookup::Unknown
                    ),
                    "{name} is named to the model and is not a registered tool: {sentence}"
                );
            }
        }
        // The sweep found something to check; a tokenizer that matched
        // nothing would pass every sentence.
        assert!(named > 20, "only {named} tool names were found");
    }

    #[test]
    fn a_restored_task_says_the_reason_did_not_survive_rather_than_inventing_one() {
        let line = values_answer(0, None);
        assert!(line.contains("brought back nothing"));
        assert!(line.contains("this task was restored"));
        assert!(!line.contains("value_from"));
    }

    /// The one this function was written for: the picture was merely below the
    /// fold, so the move is a scroll and then the same ask again.
    #[test]
    fn a_challenge_below_the_fold_is_told_to_scroll_and_ask_again() {
        let line = values_answer(0, Some(FieldValueAskOutcome::ChallengeOffScreen));
        assert!(line.contains("browser.dom.scroll on that same number"));
        assert!(line.contains("user.request_values on it again"));
        assert!(
            !line.contains("Do not name that line again"),
            "the target was right; this is the sentence that used to say otherwise: {line}"
        );
    }

    #[test]
    fn every_outcome_says_something_different_and_names_a_move() {
        let mut seen = Vec::with_capacity(EVERY_OUTCOME.len() + 1);
        for outcome in EVERY_OUTCOME {
            let line = values_answer(0, Some(outcome));
            assert!(
                !line.is_empty() && !line.contains("value_from"),
                "{outcome:?} reads as an answer that holds values: {line}"
            );
            assert!(
                !seen.contains(&line),
                "{outcome:?} repeats another outcome's sentence, so the distinction it exists \
                 for does not reach the model: {line}"
            );
            seen.push(line);
        }
        seen.push(values_answer(0, None));
        assert_eq!(seen.len(), EVERY_OUTCOME.len() + 1);
    }

    #[test]
    fn an_answer_says_how_many_values_and_how_they_are_filled() {
        let line = values_answer(2, Some(FieldValueAskOutcome::Answered));
        assert!(line.contains("the browser holds 2 values you will not see"));
        assert!(line.contains("browser.form.fill"));
        assert!(line.contains("value_from 0 into the first field"));
        assert!(
            values_answer(1, Some(FieldValueAskOutcome::Answered)).contains("holds 1 value you")
        );
    }

    /// A count that is not zero is the fill sentence whatever the outcome says.
    ///
    /// The two facts are independent on the wire, and a browser that sent a
    /// count with a mismatched member must not make the held values
    /// unreachable — the references exist either way.
    #[test]
    fn a_held_value_is_still_filled_when_the_outcome_disagrees() {
        for outcome in EVERY_OUTCOME {
            assert!(
                values_answer(1, Some(outcome)).contains("browser.form.fill"),
                "{outcome:?} hid a value the browser is holding"
            );
        }
    }

    /// The fills happened with no turn between the answer and them, so the
    /// model is told they are done and what the page wants next, and is not
    /// told to fill anything (decision 0238).
    #[test]
    fn values_the_task_placed_are_not_the_models_to_fill_again() {
        let line = values_after_placing(HeldValuesPlaced::Placed { count: 2 }, 2, None);
        assert!(line.contains("has put their 2 values into the fields the sheet showed"));
        assert!(line.contains("Do not fill those fields again"));
        assert!(line.contains("Press the page's own button"));
        assert!(line.contains("ask for it with user.request_values"));
        assert!(!line.contains("value_from"), "{line}");
        let one = values_after_placing(HeldValuesPlaced::Placed { count: 1 }, 1, None);
        assert!(
            one.contains("their value into the field the sheet showed"),
            "{one}"
        );
    }

    /// A fill that did not go in stops the rest, and the model is handed the
    /// positions still held in the words it has always been given.
    #[test]
    fn a_stopped_placement_hands_the_rest_to_the_model_by_position() {
        let line = values_after_placing(
            HeldValuesPlaced::Partly {
                placed: 1,
                count: 3,
            },
            3,
            None,
        );
        assert!(
            line.contains("It put the first 1 into the fields"),
            "{line}"
        );
        assert!(line.contains("value_from 1 into the next field"), "{line}");
        assert!(line.contains("then 2 into the one after it"), "{line}");
        assert!(!line.contains("value_from 0"), "{line}");
        let none = values_after_placing(
            HeldValuesPlaced::Partly {
                placed: 0,
                count: 1,
            },
            1,
            None,
        );
        assert!(
            none.contains("None of them could be put into its field"),
            "{none}"
        );
        assert!(none.contains("value_from 0 into the first field"), "{none}");
        assert!(!none.contains("then 1"), "{none}");
    }

    /// Nothing placed is the sentence that existed before, word for word.
    #[test]
    fn nothing_placed_reads_as_it_always_has() {
        for supplied in [0, 2] {
            let outcome = Some(FieldValueAskOutcome::Answered);
            assert_eq!(
                values_after_placing(HeldValuesPlaced::NotPlaced, supplied, outcome),
                values_answer(supplied, outcome)
            );
        }
    }

    #[test]
    fn a_fresh_errand_is_told_it_has_a_blank_tab_and_how_the_first_site_opens() {
        let line = errand_situation(0, 3, true);
        assert!(line.starts_with("You have a blank tab of your own and no site yet;"));
        assert!(line.contains("browser.search or browser.navigate"));
        assert!(line.ends_with("You may open 3 more new sites."));
    }

    #[test]
    fn a_task_with_sites_reads_its_counts_and_a_spent_budget_says_so() {
        assert_eq!(
            errand_situation(1, 1, false),
            "You have 1 site so far. You may open 1 more new site."
        );
        assert_eq!(
            errand_situation(2, 0, false),
            "You have 2 sites so far. The sites budget is spent: stay on the sites you have, \
             or ask the person."
        );
    }

    /// The sentence the ordinary errand reads: one asked from a page, holding
    /// that page and a blank tab at the same time. Before decision 0224 this
    /// shape could not exist, and the line said only how many sites it had —
    /// so the one move that could reach a new site went unmentioned, and the
    /// model spent its turns navigating the tab it was reading.
    #[test]
    fn an_errand_that_holds_a_page_is_still_told_about_its_blank_tab() {
        let line = errand_situation(1, 8, true);
        assert_eq!(
            line,
            "You have 1 site so far, and a blank tab of your own; browser.search opens the \
             next in your blank tab, not in a tab you hold. You may open 8 more new sites."
        );
    }

    /// A spent budget outranks the tab: there is nothing to open, so naming
    /// the move would be an offer the browser refuses.
    #[test]
    fn a_spent_budget_names_no_move_even_with_a_blank_tab_held() {
        let line = errand_situation(1, 0, true);
        assert!(line.contains("and a blank tab of your own"), "{line}");
        assert!(!line.contains("browser.search"), "{line}");
    }

    #[test]
    fn no_sentence_carries_anything_but_compiled_words_and_two_counts() {
        // The whole of what varies: digits. Anything else in the line was
        // written here, which is what keeps a page or a goal out of it.
        for (sources, cap, tab) in [(0, 0, false), (0, 8, true), (9, 1, true)] {
            let line = errand_situation(sources, cap, tab);
            let stripped: String = line.chars().filter(|c| !c.is_ascii_digit()).collect();
            assert!(!stripped.contains("://"), "{line}");
        }
    }
}
