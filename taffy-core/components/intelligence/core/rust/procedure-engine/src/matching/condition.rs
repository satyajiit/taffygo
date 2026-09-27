// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! When a procedure applies, expressed over the decoded graph.
//!
//! # Every clause holds, or none of it does
//!
//! A condition is a conjunction and only a conjunction. There is no `or`, no
//! `not`, and no nesting, because each of those is an expression form — and
//! rule 1 refuses stored expressions wherever they appear, not only inside a
//! step. A negation is worth naming separately: "there is no sign-in button" is
//! a claim about everything the page does not contain, and a page that withheld
//! a node satisfies it exactly as a page that never had one does. The core
//! already keeps those two apart for content (`Readability`), and a condition
//! that could not tell them apart would match hardest on the pages it
//! understands least.
//!
//! # The facts come from the caller, already decoded
//!
//! [`PageFacts`] is built from a page the core has already observed and
//! bounded. This module reads roles, states and labels; it never sees a URL, a
//! selector, or a node identifier, and it does not decide what may be acted on.
//! A match changes which command is proposed and nothing about what happens to
//! that command afterwards.

use bip_types::snapshot::{NodeState, SemanticRole};
use policy_engine::origin::NormalizedOrigin;

use super::catalogue::{classify_phrase, PhraseId};

/// How many clauses one condition may hold.
///
/// A condition is read by a person deciding whether to keep a procedure, and
/// eight facts about a page is already more than a person will check. The bound
/// is about reviewability rather than cost.
pub const MAX_MATCH_CLAUSES: usize = 8;

/// One fact a procedure claims about the page it applies to.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum MatchClause {
    /// The page has a node of this role.
    RolePresent(SemanticRole),
    /// The page has a node of this role whose label is this catalogued phrase.
    PhraseAt {
        /// What kind of thing carries the phrase.
        role: SemanticRole,
        /// The phrase, from the compiled-in catalogue.
        phrase: PhraseId,
    },
    /// The page has a node of this role asserting this state.
    ///
    /// A state token present means the adapter asserted it. Absence means it
    /// was not asserted and never that its complement is true, which is why
    /// there is no clause for the negative: BIP says a consumer must not infer
    /// `NOT_VISIBLE` from a missing `VISIBLE`, and a condition that did so
    /// would be inferring it in a stored record where nobody would see it.
    StateAt {
        /// What kind of thing is in the state.
        role: SemanticRole,
        /// The state, from BIP.
        state: NodeState,
    },
}

impl MatchClause {
    /// A short, compiled-in name for the shape, safe to record in an audit
    /// event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::RolePresent(_) => "role_present",
            Self::PhraseAt { .. } => "phrase_at",
            Self::StateAt { .. } => "state_at",
        }
    }

    /// The role this clause is about.
    pub const fn role(self) -> SemanticRole {
        match self {
            Self::RolePresent(role) | Self::PhraseAt { role, .. } | Self::StateAt { role, .. } => {
                role
            }
        }
    }

    /// Whether `node` satisfies this clause on its own.
    fn holds_for(self, node: &PageNode) -> bool {
        if node.role != self.role() {
            return false;
        }
        match self {
            Self::RolePresent(_) => true,
            Self::PhraseAt { phrase, .. } => node
                .label
                .as_deref()
                .and_then(classify_phrase)
                .is_some_and(|found| found == phrase),
            Self::StateAt { state, .. } => node.states.contains(&state),
        }
    }
}

/// When a procedure applies.
///
/// Deliberately not `Default`. The default would be the empty condition, which
/// is the one shape this crate refuses — and a caller would reach it by writing
/// nothing at all, which is precisely how the refused shape gets built by
/// accident.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MatchCondition {
    clauses: Vec<MatchClause>,
}

impl MatchCondition {
    /// A condition from `clauses`.
    ///
    /// Nothing is checked here. The bounds belong to
    /// [`crate::rules::validate`], which is what runs when a procedure is
    /// stored and again when it is loaded — checking them in a constructor as
    /// well would put the same rule in two places, and the two would answer
    /// differently the first time one of them changed.
    pub fn new(clauses: Vec<MatchClause>) -> Self {
        Self { clauses }
    }

    /// The clauses, in the order they were written.
    pub fn clauses(&self) -> &[MatchClause] {
        &self.clauses
    }

    /// How many clauses this condition holds.
    pub fn len(&self) -> usize {
        self.clauses.len()
    }

    /// Whether the condition claims nothing at all.
    ///
    /// A condition that claims nothing matches every page, which is why
    /// [`crate::rules::validate`] refuses one rather than letting it stand as
    /// "applies everywhere".
    pub fn is_empty(&self) -> bool {
        self.clauses.is_empty()
    }

    /// Whether every clause holds, and the first that does not when one does
    /// not.
    pub fn evaluate(&self, facts: &PageFacts) -> MatchVerdict {
        if self.clauses.is_empty() {
            // Refused at storage and again at load, so reaching here means a
            // caller built one in memory and evaluated it without validating.
            // Answering "matched" would make the unvalidated path the
            // permissive one, which is the wrong direction for a mistake.
            return MatchVerdict::Unsound;
        }
        for clause in &self.clauses {
            if !facts.nodes().iter().any(|node| clause.holds_for(node)) {
                return MatchVerdict::Unmet(*clause);
            }
        }
        MatchVerdict::Matched
    }
}

/// What a condition concluded.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum MatchVerdict {
    /// Every clause held.
    Matched,
    /// The page is not on the origin the procedure was written for.
    ///
    /// Reported separately from an unmet clause because it is a different thing
    /// to tell a person: the procedure is about somewhere else, rather than
    /// about here and not applicable yet.
    ScopeDiffers,
    /// The first clause that did not hold. Shown to a person in the same
    /// vocabulary the assistant uses everywhere else.
    Unmet(MatchClause),
    /// The condition claims nothing, so it was not evaluated.
    Unsound,
}

impl MatchVerdict {
    /// Whether the procedure applies to this page.
    pub const fn matched(self) -> bool {
        matches!(self, Self::Matched)
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Matched => "matched",
            Self::ScopeDiffers => "scope_differs",
            Self::Unmet(_) => "unmet",
            Self::Unsound => "unsound",
        }
    }
}

/// One node of the page, as much of it as a condition may read.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PageNode {
    /// What kind of thing it is.
    pub role: SemanticRole,
    /// Its label, when one crossed. `None` covers both "no label" and "label
    /// withheld"; a condition treats them alike, because in neither case is
    /// there a phrase to compare.
    pub label: Option<String>,
    /// The states that were asserted.
    pub states: Vec<NodeState>,
}

impl PageNode {
    /// A node with a role and nothing else.
    pub const fn new(role: SemanticRole) -> Self {
        Self {
            role,
            label: None,
            states: Vec::new(),
        }
    }

    /// The same node, labelled.
    #[must_use]
    pub fn labelled(mut self, label: impl Into<String>) -> Self {
        self.label = Some(label.into());
        self
    }

    /// The same node, asserting `states`.
    #[must_use]
    pub fn asserting(mut self, states: Vec<NodeState>) -> Self {
        self.states = states;
        self
    }
}

/// The page a condition is evaluated against.
///
/// Built by the caller from an observation the core has already decoded and
/// bounded, so there is no bound here: a second one would be a second answer to
/// "how big may a page be", and the arena's is the one the contract enforces.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PageFacts {
    origin: NormalizedOrigin,
    nodes: Vec<PageNode>,
}

impl PageFacts {
    /// The facts of one observed page.
    pub const fn new(origin: NormalizedOrigin, nodes: Vec<PageNode>) -> Self {
        Self { origin, nodes }
    }

    /// The origin the page is on.
    pub const fn origin(&self) -> &NormalizedOrigin {
        &self.origin
    }

    /// The nodes, in the order the page presented them.
    pub fn nodes(&self) -> &[PageNode] {
        &self.nodes
    }
}

#[cfg(test)]
mod tests {
    use super::{MatchClause, MatchCondition, MatchVerdict, PageFacts, PageNode, PhraseId};
    use bip_types::snapshot::{NodeState, SemanticRole};
    use policy_engine::origin::normalize_serialization;

    fn facts(nodes: Vec<PageNode>) -> PageFacts {
        let Ok(origin) = normalize_serialization("https://example.test") else {
            unreachable!("the fixture origin is an origin")
        };
        PageFacts::new(origin, nodes)
    }

    fn sign_in_page() -> PageFacts {
        facts(vec![
            PageNode::new(SemanticRole::TextField).asserting(vec![NodeState::Required]),
            PageNode::new(SemanticRole::Button).labelled("Sign In"),
            PageNode::new(SemanticRole::Heading).labelled("Welcome back"),
        ])
    }

    #[test]
    fn every_clause_must_hold() {
        let condition = MatchCondition::new(vec![
            MatchClause::RolePresent(SemanticRole::TextField),
            MatchClause::PhraseAt {
                role: SemanticRole::Button,
                phrase: PhraseId::SignIn,
            },
        ]);
        assert_eq!(condition.evaluate(&sign_in_page()), MatchVerdict::Matched);
        assert!(condition.evaluate(&sign_in_page()).matched());
    }

    #[test]
    fn the_first_unmet_clause_is_named_so_a_person_can_be_shown_it() {
        let missing = MatchClause::PhraseAt {
            role: SemanticRole::Button,
            phrase: PhraseId::Checkout,
        };
        let condition = MatchCondition::new(vec![
            MatchClause::RolePresent(SemanticRole::TextField),
            missing,
            MatchClause::RolePresent(SemanticRole::Image),
        ]);
        assert_eq!(
            condition.evaluate(&sign_in_page()),
            MatchVerdict::Unmet(missing)
        );
    }

    #[test]
    fn a_phrase_clause_is_about_one_role_and_not_about_the_page() {
        // The phrase is on a button. Asking for it on a heading is a different
        // claim and it does not hold, which is what keeps a condition from
        // matching a page that merely contains the words somewhere.
        let condition = MatchCondition::new(vec![MatchClause::PhraseAt {
            role: SemanticRole::Heading,
            phrase: PhraseId::SignIn,
        }]);
        assert!(!condition.evaluate(&sign_in_page()).matched());
    }

    #[test]
    fn a_node_with_no_label_satisfies_no_phrase_clause() {
        let page = facts(vec![PageNode::new(SemanticRole::Button)]);
        let condition = MatchCondition::new(vec![MatchClause::PhraseAt {
            role: SemanticRole::Button,
            phrase: PhraseId::SignIn,
        }]);
        assert!(!condition.evaluate(&page).matched());
    }

    #[test]
    fn a_state_clause_reads_an_assertion_and_never_an_absence() {
        let condition = MatchCondition::new(vec![MatchClause::StateAt {
            role: SemanticRole::TextField,
            state: NodeState::Required,
        }]);
        assert!(condition.evaluate(&sign_in_page()).matched());
        // The same field with nothing asserted does not satisfy it, and there
        // is no clause that could be satisfied by the absence instead.
        let page = facts(vec![PageNode::new(SemanticRole::TextField)]);
        assert!(!condition.evaluate(&page).matched());
    }

    #[test]
    fn a_condition_that_claims_nothing_does_not_match_everything() {
        let condition = MatchCondition::new(Vec::new());
        assert!(condition.is_empty());
        assert_eq!(condition.evaluate(&sign_in_page()), MatchVerdict::Unsound);
        assert!(!condition.evaluate(&sign_in_page()).matched());
        // Including against a page with nothing in it, where "matches
        // everything" and "matches nothing" would otherwise look alike.
        assert_eq!(
            condition.evaluate(&facts(Vec::new())),
            MatchVerdict::Unsound
        );
    }

    #[test]
    fn every_verdict_has_a_distinct_compiled_in_label() {
        let verdicts = [
            MatchVerdict::Matched,
            MatchVerdict::ScopeDiffers,
            MatchVerdict::Unmet(MatchClause::RolePresent(SemanticRole::Button)),
            MatchVerdict::Unsound,
        ];
        let mut seen: Vec<&str> = Vec::new();
        for verdict in verdicts {
            assert!(!seen.contains(&verdict.label()), "{}", verdict.label());
            seen.push(verdict.label());
        }
        let clauses = [
            MatchClause::RolePresent(SemanticRole::Button),
            MatchClause::PhraseAt {
                role: SemanticRole::Button,
                phrase: PhraseId::SignIn,
            },
            MatchClause::StateAt {
                role: SemanticRole::Button,
                state: NodeState::Disabled,
            },
        ];
        let mut shapes: Vec<&str> = Vec::new();
        for clause in clauses {
            assert!(!shapes.contains(&clause.label()), "{}", clause.label());
            shapes.push(clause.label());
            assert_eq!(clause.role(), SemanticRole::Button);
        }
    }
}
