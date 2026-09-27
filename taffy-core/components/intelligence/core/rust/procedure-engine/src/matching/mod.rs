// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Catalogued phrases over the semantic graph, and never a pattern language.
//!
//! # Rule 4: no regular-expression engine ships for this
//!
//! Three independent reasons, any one of them sufficient:
//!
//! - A stored pattern evaluated against attacker-controlled text is a
//!   denial-of-service surface with a well-known name.
//! - A pattern over page *text* matches the thing the semantic graph exists to
//!   replace, and would put back the brittleness the graph was built to remove.
//! - A pattern is exactly the stored expression rule 1 refuses, arriving
//!   through a different door.
//!
//! So a condition names roles, states and phrase identifiers drawn from a
//! compiled-in catalogue — [`PhraseId`] and [`phrase_forms`] — evaluated by
//! [`MatchCondition`] against a graph the core has already decoded. It never
//! matches a URL, a selector, or page text through anything a caller could
//! author.
//!
//! # What "structurally" means here, precisely
//!
//! A label is normalized — trimmed, lowercased, and its runs of whitespace
//! collapsed to one space — and then compared for **equality** against the
//! catalogue's forms. Not containment, not prefix, not "starts with". Equality
//! is the strongest structural statement available and it is the one that makes
//! the catalogue closed: a page cannot widen a phrase by wrapping it in text,
//! and a form cannot accidentally match half the page.
//!
//! That is stricter than a person might expect, and deliberately: a button
//! reading "Sign in to continue" does not match [`PhraseId::SignIn`] unless the
//! catalogue lists that form. A phrase the catalogue does not list is a phrase
//! nobody reviewed, and the cost of missing one is a procedure that does not
//! match — a model call. The cost of matching loosely is a procedure that runs
//! on a page it was not written for.
//!
//! # Matching is explainable
//!
//! [`MatchVerdict`] names the first clause that did not hold, in the same
//! vocabulary the assistant uses everywhere else, so a person can be shown why
//! a procedure did or did not apply rather than told that it did not.

mod catalogue;
mod condition;

pub use self::catalogue::{
    classify_phrase, normalize_label, phrase_forms, PhraseId, MAX_LABEL_BYTES,
};
pub use self::condition::{
    MatchClause, MatchCondition, MatchVerdict, PageFacts, PageNode, MAX_MATCH_CLAUSES,
};
