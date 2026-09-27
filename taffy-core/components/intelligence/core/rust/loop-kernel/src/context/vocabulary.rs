// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The words a projection is allowed to use for a closed enumeration.
//!
//! Every function here is total over a closed enumeration, and that is the
//! point of the module. A projection that formatted a role with `{:?}` would
//! silently start printing a new member's Rust identifier the day one is added
//! — a vocabulary change nobody reviewed, reaching a model, in a build whose
//! only record of it is a derive. An exhaustive match makes the same day a
//! compile error with the reviewer's name on it.
//!
//! Most of the enumerations are the BIP contract's. Three are not: an action's
//! [`ActionState`] is the reducer's own, [`NotAttempted`] is why a call in a
//! reply was never proposed, and [`Elision`] is this layer's. They are here
//! rather than beside their owners because the rule the module exists for is
//! about the *reader* — every word a model is shown is compiled in and chosen
//! deliberately — and that rule does not care which crate declared the
//! enumeration it is speaking about.
//!
//! The words are deliberately ordinary English and deliberately not the
//! contract's identifiers. What the model reads is a description of a page,
//! and `UnknownInteractive` is a wire member where "control" is a description.

use bip_types::action::ActionType;
use bip_types::snapshot::{NodeState, SemanticRole, Sensitivity};
use task_engine::{ActionState, NotAttempted};

use crate::context::arena::DestinationClass;

/// A blank page, in the words the model reads.
///
/// Compiled in, and used only when the arena's verdict is empty: the page
/// held nothing, and reporting so is correct. An unreadable page must never
/// reuse this sentence, and neither must a page a **query** narrowed to
/// nothing — that is [`no_query_match_line`], and the difference is the
/// whole of decision 0210.
pub const EMPTY_PAGE_LINE: &str = "the page is blank";

/// The line a source heading carries when that source is a page the person
/// attached and the task has a tab of its own (decision 0237).
///
/// Every turn shows every current source, the person's page beside the
/// task's own, and nothing used to say which one a call may act on. So on a
/// phone the model, working a site in its own tab, picked a link from the
/// person's page for a `browser.link.open`. The call is refused now; this
/// line is what keeps the model from picking it. The verbs are the kinds of
/// call the refusal covers — every one that names a number and is not a read.
pub const PERSONS_PAGE_LINE: &str = "This is the page the person asked from: read it, but open, \
     press, scroll, fill and ask for values only with numbers from a page in your own tab.";

/// A query that matched nothing, in the words the model reads.
///
/// Two sentences rather than one, because "nothing matched" leaves the model
/// with a question this layer can already answer: is there anything here at
/// all? A page that is readable gets told so and gets the one move that
/// helps, for the same reason decision 0208 gives each refusal its own
/// advice — a model that is only told what failed repeats what it just did.
///
/// The verdict is the **page's**, never the match set's.
/// `readability_for` over an empty iterator answers `Empty`, which is a true
/// statement about the nodes it was handed and a false one about the
/// document they came from (decision 0210).
pub const fn no_query_match_line(page_is_readable: bool) -> &'static str {
    if page_is_readable {
        return "nothing on this page matched that query — the page itself is not blank, \
                so read this page to see what is on it";
    }
    "nothing on this page matched that query"
}

/// What kind of thing a node is, in one word or two.
pub const fn role_word(role: SemanticRole) -> &'static str {
    match role {
        SemanticRole::Document => "document",
        SemanticRole::Region => "region",
        SemanticRole::Heading => "heading",
        SemanticRole::Paragraph => "paragraph",
        SemanticRole::List => "list",
        SemanticRole::ListItem => "list item",
        SemanticRole::Table => "table",
        SemanticRole::TableRow => "row",
        SemanticRole::TableCell => "cell",
        SemanticRole::Link => "link",
        SemanticRole::Button => "button",
        SemanticRole::SearchField => "search box",
        SemanticRole::TextField => "text field",
        SemanticRole::Checkbox => "checkbox",
        SemanticRole::Radio => "radio button",
        SemanticRole::Select => "dropdown",
        SemanticRole::Option => "option",
        SemanticRole::Image => "image",
        SemanticRole::Media => "media",
        SemanticRole::Price => "price",
        SemanticRole::Rating => "rating",
        SemanticRole::Availability => "availability",
        SemanticRole::LabelValuePair => "field",
        SemanticRole::Citation => "citation",
        // Two members that say the renderer could not classify the element.
        // Saying so plainly is better than either guessing a role or leaving
        // the node out: a control nobody can name is still a control a person
        // can be asked about, and a node absent from the projection is a node
        // the model concludes does not exist.
        SemanticRole::UnknownInteractive => "control",
        SemanticRole::UnknownContent => "content",
    }
}

/// What kind of private value a node holds, as the browser classified it.
///
/// A sensitive node's label and value never cross (the graph payload
/// withholds both), so without this a model shown `text field (label
/// withheld)` on an ID form cannot tell the box for an ID number from any
/// other, and cannot name the one to ask the person about. The class is the
/// browser's own verdict and says nothing the page wrote: it names the kind
/// of thing, never the thing. `None` for a node that is not sensitive.
pub const fn sensitivity_words(sensitivity: Sensitivity) -> Option<&'static str> {
    Some(match sensitivity {
        Sensitivity::NotSensitive => return None,
        Sensitivity::Personal => "personal details",
        Sensitivity::Account => "account details",
        Sensitivity::Payment => "payment details",
        Sensitivity::Identity => "identity details",
        Sensitivity::Health => "health details",
        Sensitivity::Financial => "financial details",
        Sensitivity::Legal => "legal details",
        Sensitivity::PrivateCommunication => "private messages",
        Sensitivity::Administration => "official records",
        Sensitivity::Credential => "a password or sign-in secret",
        Sensitivity::UnknownSensitive => "something private",
        Sensitivity::OneTimeCode => "a one-time code",
        Sensitivity::ChallengeResponse => "a CAPTCHA answer",
    })
}

/// A state's word, or `None` when the state is the assumed default and saying
/// it would cost bytes on every line to convey nothing.
///
/// Only two are suppressed. `Visible` and `Enabled` are what a projected node
/// is taken to be, and their opposites are separate members that *are* printed
/// — so the absence of a word is never ambiguous between "not stated" and
/// "stated to be ordinary". Everything else prints, including states a reader
/// might call cosmetic: `Focused` tells a model where a person's caret already
/// is, which is the difference between typing into a field and typing over
/// somebody.
pub const fn state_word(state: NodeState) -> Option<&'static str> {
    match state {
        NodeState::Visible | NodeState::Enabled => None,
        NodeState::NotVisible => Some("hidden"),
        NodeState::Offscreen => Some("offscreen"),
        NodeState::Obscured => Some("covered"),
        NodeState::Disabled => Some("disabled"),
        NodeState::Editable => Some("editable"),
        NodeState::ReadOnly => Some("read-only"),
        NodeState::Required => Some("required"),
        NodeState::Invalid => Some("invalid"),
        NodeState::Checked => Some("checked"),
        NodeState::Unchecked => Some("unchecked"),
        NodeState::Mixed => Some("partly checked"),
        NodeState::Selected => Some("selected"),
        NodeState::Expanded => Some("expanded"),
        NodeState::Collapsed => Some("collapsed"),
        NodeState::Focused => Some("focused"),
        NodeState::Busy => Some("busy"),
    }
}

/// What a node can be asked to do, as the verb a caller would name.
///
/// These are the tool-facing verbs rather than prose, because a model that
/// reads "can be typed into" and answers `type into` has invented a call. The
/// word printed is the word accepted.
pub const fn action_word(action: ActionType) -> &'static str {
    match action {
        ActionType::Activate => "activate",
        ActionType::Focus => "focus",
        ActionType::ScrollIntoView => "scroll",
        ActionType::SetText => "set text",
        ActionType::SelectOption => "select",
        ActionType::Toggle => "toggle",
        ActionType::SubmitForm => "submit",
    }
}

/// Where a node leads, as classes and never as a place.
///
/// No origin, no path, no host, no digest of any of them — the wire carries
/// four bits and this turns those four bits into words. A model that could
/// read the target would be a model that could be steered by it, and deciding
/// where a click may land is the policy engine's job on facts the model never
/// touches.
///
/// Returns an empty vector when the node leads nowhere, so a caller can print
/// nothing rather than print "no destination" on every heading and paragraph.
pub fn destination_words(destination: DestinationClass) -> Vec<&'static str> {
    if !destination.present() {
        return Vec::new();
    }
    let mut words = Vec::new();
    if destination.is_download() {
        words.push("downloads a file");
    }
    if destination.cross_origin() {
        words.push("another site");
    } else {
        words.push("this site");
    }
    if destination.opens_new_tab() {
        words.push("new tab");
    }
    words
}

/// What became of one tool call the model already made, in the words it reads
/// back.
///
/// Total over [`ActionState`] for the reason [`role_word`] is total over
/// `SemanticRole`, and the cost of getting it wrong is higher here: this
/// sentence is the *only* thing a replayed turn says about what happened, so a
/// `{:?}` that crept in would put `OutcomeUnknown` in front of a model as
/// though it were English, and a member added later would reach a request
/// nobody reviewed.
///
/// The three non-terminal states are worth reading twice. They are reachable
/// in a transcript only if a call of an older turn never ended, which the loop
/// does not allow — but "cannot happen" is not a sentence, and a model shown
/// nothing for a call it made concludes the call was never answered.
pub const fn outcome_word(state: ActionState) -> &'static str {
    match state {
        ActionState::Proposed => "asked for, not started yet",
        ActionState::WaitingApproval => "waiting for the person to decide",
        ActionState::Authorized => "allowed, not sent yet",
        ActionState::Dispatching => "being sent",
        ActionState::Verifying => "being checked",
        ActionState::Verified => "done, and checked",
        ActionState::Failed => "did not happen",
        ActionState::Rejected => "refused",
        ActionState::Cancelled => "cancelled",
        ActionState::OutcomeUnknown => "may or may not have happened",
    }
}

/// Whether the model should read an outcome as the tool having failed.
///
/// The four terminal states that did not do what was asked, plus the one that
/// cannot say. Everything else is `false`, including the three a call passes
/// *through*: a call still in flight has not failed, and the error flag is
/// read by a model as a fact about the world rather than as a shrug.
///
/// Separate from [`outcome_word`] rather than derived from it, because the
/// flag and the sentence go to different places on the wire — two of the four
/// protocol families carry a flag and two fold the fact into the text
/// (decision 0069 section 2) — and a word that happened to read like a failure
/// is not the same as a call that was one.
pub const fn outcome_is_failure(state: ActionState) -> bool {
    match state {
        ActionState::Proposed
        | ActionState::WaitingApproval
        | ActionState::Authorized
        | ActionState::Dispatching
        | ActionState::Verifying
        | ActionState::Verified => false,
        ActionState::Failed
        | ActionState::Rejected
        | ActionState::Cancelled
        | ActionState::OutcomeUnknown => true,
    }
}

/// Why a call in a reply was never executed, in the words the model reads back.
///
/// Total over [`NotAttempted`] for the same reason [`outcome_word`] is total
/// over [`ActionState`]. A Length-stop reply that named calls never became
/// actions, so this sentence is the only thing the next compose can say about
/// them; a `{:?}` that crept in would put `TruncatedArguments` in front of a
/// model as though it were English.
pub const fn not_attempted_word(reason: NotAttempted) -> &'static str {
    match reason {
        NotAttempted::TruncatedArguments => {
            "not executed: the reply hit the output limit, so the arguments may be \
             incomplete. Re-issue this tool call with complete arguments."
        }
        NotAttempted::PriorCallRefused => {
            "not executed: an earlier call in the same reply was refused"
        }
        NotAttempted::ToolNotAvailable => "not executed: that tool is not available",
        NotAttempted::ArgumentsRejected => "not executed: the arguments did not match",
        // What to do next, and not only what is wrong. This is now the answer
        // to a number nobody printed — never issued, or issued so long ago the
        // table has forgotten it — and the numbers that work are the ones the
        // latest snapshot shows.
        NotAttempted::HandleUnknown => {
            "not executed: that number is no longer available; use a number \
             from the latest snapshot"
        }
        // A number read from a page its tab has since left used to get the
        // sentence above (decision 0191). It is the wrong instruction here and
        // not merely a vague one: this number resolved, so telling a model to
        // use one from the latest snapshot is telling it to do what it may
        // have just done. Say which page the number belongs to instead, and
        // name the one move that makes a usable number exist (decision 0208).
        // The number is usually the right one on the wrong page: errand
        // `27fc5955` opened a search result and then named another result
        // from the list twice, and never went back to it. So the sentence
        // also names the move that returns to that page (decision 0246).
        NotAttempted::NodeHandleFromAPageTheTabLeft => {
            "not executed: that number was read from a page this tab has left; \
             read this page, then use a number from that reading, or go back \
             with browser.back to the page it came from and read it again"
        }
        // The number is good and so is the page; what is wrong is acting
        // there at all. So say whose page it is, and name the tab every move
        // belongs in and the moves that put a page in it — the same words the
        // source heading uses (decision 0237).
        NotAttempted::NodeOnThePersonsPage => {
            "not executed: that number is on the page the person asked from, which you may \
             read but not act on; act only with numbers from a page in your own tab, and \
             open a site there with browser.search or browser.navigate if it is still blank"
        }
        NotAttempted::NotAField => {
            // Points at the page's own footer rather than restating the rule.
            // The previous sentence said what the number was not and left the
            // model to find one that qualified among every number on the
            // page; it guessed wrong three times running on the myAadhaar
            // download form and the turn cap ended the errand (decision
            // 0211).
            "not executed: that number is not a form or a field a person types into; the \
             reading of this page ends with the numbers that take values — use one of those"
        }
        NotAttempted::NotATextField => {
            "not executed: that number cannot take text; fill the field whose line says it can \
             set text"
        }
        // Names the move that works on the same number, because the control
        // the model wanted is usually right: it is a button, not a link
        // (decision 0241).
        NotAttempted::NotALink => {
            "not executed: that number is not a link with an address; press it with \
             browser.dom.click, or open a line that says \"this site\" or \"another site\""
        }
        NotAttempted::TabOwnershipUnavailable => {
            "not executed: this build cannot own a newly opened task tab"
        }
        NotAttempted::OperandResidencyUnavailable => {
            "not executed: this build cannot retain those arguments for dispatch"
        }
        NotAttempted::RepeatedRefusalsAbandoned => {
            "not executed: the same call was refused too many times"
        }
        NotAttempted::NoRuntimeHere => "not executed: this build does not serve that tool",
        NotAttempted::PageUnknown => "not executed: this task has no page open to act in right now",
    }
}

/// What a transcript could not carry into this request.
///
/// One member, and an enumeration regardless, for the reason every other
/// function here is a match rather than a string: the day a second kind of
/// elision exists — a summarized run of turns, a page projection dropped out
/// of a turn that kept its calls — the match below stops compiling and
/// somebody chooses its words. Written as a bare sentence instead, the first
/// kind's wording would quietly describe the second.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Elision {
    /// Whole exchanges, oldest first, because the transcript reached its
    /// bound. A turn's calls and their results went together.
    OldestTurns,
}

impl Elision {
    /// Every kind, in declaration order.
    pub const ALL: &'static [Self] = &[Self::OldestTurns];
}

/// The two compiled-in halves of an elision line, with a count between them.
///
/// Two halves rather than one sentence because the number is the whole point
/// and it is not known here. `render`'s footers do the same thing inline; this
/// is that shape moved somewhere a reviewer can see every word at once.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ElisionWords {
    /// What precedes the count.
    pub before: &'static str,
    /// What follows it.
    pub after: &'static str,
}

/// What the model is told about turns this request does not carry.
///
/// The wording is deliberately not `render`'s "not shown", which means the
/// budget stopped early and asking again with room would work. Nothing about
/// asking again brings these back: they happened, the conversation outgrew
/// them, and every later turn will be able to carry fewer rather than more. A
/// model told "not shown" would re-plan around retrieving them.
pub const fn elision_words(elision: Elision) -> ElisionWords {
    match elision {
        Elision::OldestTurns => ElisionWords {
            before: "earlier in this task,",
            after: "turns are no longer carried — they happened, and this \
                    conversation has outgrown them",
        },
    }
}

/// What the model is told when the page was not trivial and none of it crossed.
///
/// The two numbers are the facts the payload carries whatever it withheld, so
/// they are printed rather than implied. Text that was withheld stays
/// withheld, and asking again does not change that.
///
/// A page that declared no text at all is a different case, and the usual one
/// is a page still drawing itself: a site built in script answers a read made
/// the moment it commits with a handful of empty containers. Told "this build
/// may not show any of it", the model took the page for one it could never
/// read and searched again — on 2026-09-18 it did so from the very eAadhaar
/// page it was looking for, twice, where one more read showed 350 nodes.
pub fn unreadable_page_line(node_count: usize, text_bytes: u64) -> String {
    if text_bytes == 0 {
        return format!(
            "the page had {node_count} nodes and no text yet — a page that is still \
             loading reads this way, so read it once more before leaving it"
        );
    }
    format!(
        "the page had {node_count} nodes and {text_bytes} bytes of text — \
         this build may not show any of it"
    )
}

#[cfg(test)]
mod tests {
    use super::{
        action_word, destination_words, elision_words, not_attempted_word, outcome_is_failure,
        outcome_word, role_word, state_word, Elision,
    };
    use crate::context::arena::DestinationClass;
    use bip_types::snapshot::{NodeState, SemanticRole};
    use task_engine::{ActionState, NotAttempted};

    #[test]
    fn every_word_is_lowercase_prose_and_never_an_identifier() {
        // A `{:?}` that crept in would print `UnknownInteractive` here.
        for role in SemanticRole::ALL {
            let word = role_word(*role);
            assert!(!word.is_empty());
            assert_eq!(word, word.to_lowercase(), "{word} is not prose");
        }
    }

    #[test]
    fn only_the_two_assumed_states_are_silent() {
        let silent: Vec<_> = NodeState::ALL
            .iter()
            .filter(|state| state_word(**state).is_none())
            .collect();
        assert_eq!(silent, vec![&NodeState::Visible, &NodeState::Enabled]);
    }

    #[test]
    fn a_node_that_leads_nowhere_says_nothing_about_where() {
        assert!(destination_words(DestinationClass::default()).is_empty());
    }

    #[test]
    fn a_download_on_another_site_says_both_and_never_the_site() {
        let words = destination_words(DestinationClass::from_bits(0b1111));
        assert_eq!(words, vec!["downloads a file", "another site", "new tab"]);
        assert!(!words.iter().any(|word| word.contains("://")));
    }

    #[test]
    fn an_action_prints_the_verb_a_caller_would_name() {
        assert_eq!(
            action_word(bip_types::action::ActionType::SetText),
            "set text"
        );
    }

    #[test]
    fn every_outcome_has_prose_and_no_two_states_share_a_sentence() {
        // Sharing one would make two different things that happened read as
        // one, which is the whole reason the match is exhaustive.
        let mut seen: Vec<&str> = Vec::new();
        for state in ActionState::ALL {
            let word = outcome_word(*state);
            assert!(!word.is_empty(), "{state:?}");
            assert_eq!(word, word.to_lowercase(), "{word} is not prose");
            assert!(!seen.contains(&word), "{word} is used twice");
            seen.push(word);
        }
        assert_eq!(seen.len(), ActionState::ALL.len());
    }

    #[test]
    fn only_the_four_terminal_disappointments_and_the_unknown_are_failures() {
        let failures: Vec<_> = ActionState::ALL
            .iter()
            .filter(|state| outcome_is_failure(**state))
            .collect();
        assert_eq!(
            failures,
            vec![
                &ActionState::Failed,
                &ActionState::Rejected,
                &ActionState::Cancelled,
                &ActionState::OutcomeUnknown,
            ]
        );
        // Named from the other side too: the one outcome that did what was
        // asked must never arrive at a model wearing an error flag.
        assert!(!outcome_is_failure(ActionState::Verified));
    }

    #[test]
    fn every_not_attempted_reason_has_prose_and_no_two_share_a_sentence() {
        let mut seen: Vec<&str> = Vec::new();
        for reason in NotAttempted::ALL {
            let word = not_attempted_word(*reason);
            assert!(!word.is_empty(), "{reason:?}");
            assert!(!seen.contains(&word), "{word} is used twice");
            seen.push(word);
        }
        assert_eq!(seen.len(), NotAttempted::ALL.len());
        assert_eq!(
            not_attempted_word(NotAttempted::TruncatedArguments),
            "not executed: the reply hit the output limit, so the arguments may be \
             incomplete. Re-issue this tool call with complete arguments."
        );
        for reason in NotAttempted::ALL {
            if matches!(reason, NotAttempted::TruncatedArguments) {
                continue;
            }
            let word = not_attempted_word(*reason);
            assert_eq!(word, word.to_lowercase(), "{word} is not prose");
        }
    }

    #[test]
    fn only_a_sensitive_node_names_a_private_class() {
        use bip_types::snapshot::Sensitivity;
        assert_eq!(super::sensitivity_words(Sensitivity::NotSensitive), None);
        for sensitivity in [
            Sensitivity::Personal,
            Sensitivity::Account,
            Sensitivity::Payment,
            Sensitivity::Identity,
            Sensitivity::Health,
            Sensitivity::Financial,
            Sensitivity::Legal,
            Sensitivity::PrivateCommunication,
            Sensitivity::Administration,
            Sensitivity::Credential,
            Sensitivity::UnknownSensitive,
            Sensitivity::OneTimeCode,
            Sensitivity::ChallengeResponse,
        ] {
            let words = super::sensitivity_words(sensitivity).expect("a class");
            assert_eq!(words, words.to_lowercase().replace("captcha", "CAPTCHA"));
        }
    }

    #[test]
    fn a_page_with_no_text_yet_is_read_again_and_a_withheld_one_is_not() {
        let loading = super::unreadable_page_line(9, 0);
        assert!(loading.contains("still loading"));
        assert!(loading.contains("read it once more"));
        let withheld = super::unreadable_page_line(9, 4096);
        assert!(withheld.contains("4096 bytes"));
        assert!(!withheld.contains("read it once more"));
    }

    #[test]
    fn an_elision_line_never_says_a_turn_could_be_shown_again() {
        for elision in Elision::ALL {
            let words = elision_words(*elision);
            assert!(!words.before.is_empty());
            assert!(!words.after.is_empty());
            // `render`'s "not shown" means asking again with room would work.
            // Nothing brings an elided turn back, so borrowing that phrase
            // here would send a model looking for it.
            assert!(!words.before.contains("not shown"));
            assert!(!words.after.contains("not shown"));
        }
    }
}
