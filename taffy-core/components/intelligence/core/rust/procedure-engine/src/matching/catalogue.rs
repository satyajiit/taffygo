// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The closed catalogue of phrases a procedure may name.
//!
//! # A table, not a language
//!
//! Every entry is a literal string. There is no wildcard, no anchor, no
//! character class and no escape, and `no_form_is_a_pattern` asserts as much
//! over the whole table — so a form cannot become a pattern by being added
//! carelessly, which is the way a refused feature usually comes back.
//!
//! # Normalization is the whole of the flexibility
//!
//! [`normalize_label`] trims, lowercases and collapses whitespace. That is all
//! it does. It does not strip punctuation, expand contractions, remove
//! diacritics, or stem anything: each of those is a small language, and a
//! reader would have to run it in their head to know what a stored procedure
//! matches. Lowercasing is ASCII-only for the same reason — Unicode case
//! folding is locale-dependent, and a phrase that matched in one locale and not
//! another would be a stored procedure whose behaviour depends on a setting
//! nobody looked at.
//!
//! The bound exists because the label is page text: a caller may hand this a
//! label of any size the arena allowed, and a comparison against every form of
//! every phrase should cost the catalogue rather than the page.

/// How many bytes of a label are considered.
///
/// Every catalogued form is far shorter than this, so a longer label cannot
/// match anything: the bound refuses work rather than truncating a comparison.
pub const MAX_LABEL_BYTES: usize = 128;

/// A phrase the catalogue knows.
///
/// Small on purpose. It is not a list of everything a page can say — it is the
/// list of things a stored procedure may claim to have seen, and every member
/// is a phrase whose meaning is the same on every site that uses it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PhraseId {
    /// Starting a session.
    SignIn,
    /// Ending one.
    SignOut,
    /// Looking something up.
    Search,
    /// Putting an item in a basket.
    AddToCart,
    /// Looking at the basket.
    ViewCart,
    /// Beginning to pay.
    Checkout,
    /// Moving on in a sequence of steps.
    Continue,
    /// Agreeing to what was just described.
    Confirm,
    /// Backing out of what was just described.
    Cancel,
    /// The next page of a list.
    NextPage,
    /// Retrieving a document the current page makes available.
    Download,
}

impl PhraseId {
    /// Every member, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::SignIn,
        Self::SignOut,
        Self::Search,
        Self::AddToCart,
        Self::ViewCart,
        Self::Checkout,
        Self::Continue,
        Self::Confirm,
        Self::Cancel,
        Self::NextPage,
        Self::Download,
    ];

    /// A short, compiled-in name, safe to record in an audit event and safe to
    /// show a person as the reason a procedure matched.
    pub const fn label(self) -> &'static str {
        match self {
            Self::SignIn => "sign_in",
            Self::SignOut => "sign_out",
            Self::Search => "search",
            Self::AddToCart => "add_to_cart",
            Self::ViewCart => "view_cart",
            Self::Checkout => "checkout",
            Self::Continue => "continue",
            Self::Confirm => "confirm",
            Self::Cancel => "cancel",
            Self::NextPage => "next_page",
            Self::Download => "download",
        }
    }
}

/// The forms of one phrase. Every one is already normalized.
const fn forms_of(phrase: PhraseId) -> &'static [&'static str] {
    match phrase {
        PhraseId::SignIn => &["sign in", "log in", "login", "signin"],
        PhraseId::SignOut => &["sign out", "log out", "logout", "signout"],
        PhraseId::Search => &["search", "search this site", "find"],
        PhraseId::AddToCart => &["add to cart", "add to basket", "add to bag"],
        PhraseId::ViewCart => &["view cart", "cart", "basket", "view basket"],
        PhraseId::Checkout => &["checkout", "check out", "go to checkout"],
        PhraseId::Continue => &["continue", "next", "proceed"],
        // No bare "yes", "ok" or "no". They are the affirmatives of every
        // dialog a page can raise, including the ones a procedure has nothing
        // to do with, and a condition built on one would claim a page it never
        // saw. A phrase earns a row by meaning the same thing on every site
        // that uses it, and "ok" does not.
        PhraseId::Confirm => &["confirm", "confirm order", "confirm booking"],
        PhraseId::Cancel => &["cancel", "discard", "go back"],
        PhraseId::NextPage => &["next page", "show more", "load more"],
        PhraseId::Download => &[
            "download",
            "download pdf",
            "download aadhaar",
            "download e-aadhaar",
        ],
    }
}

/// The catalogued forms of `phrase`.
pub const fn phrase_forms(phrase: PhraseId) -> &'static [&'static str] {
    forms_of(phrase)
}

/// A label as the catalogue compares it: trimmed, ASCII-lowercased, and with
/// runs of whitespace collapsed to one space.
///
/// Returns nothing for a label past [`MAX_LABEL_BYTES`], because a label that
/// long matches no form and normalizing it would be work done to reach that
/// conclusion.
pub fn normalize_label(label: &str) -> Option<String> {
    if label.len() > MAX_LABEL_BYTES {
        return None;
    }
    let mut normalized = String::with_capacity(label.len());
    for word in label.split_whitespace() {
        if !normalized.is_empty() {
            normalized.push(' ');
        }
        normalized.push_str(&word.to_ascii_lowercase());
    }
    Some(normalized)
}

/// Which catalogued phrase `label` is, if it is one.
///
/// Equality against a normalized form, and nothing else. A label that contains
/// a form is not that form: see the module header on why the loose reading is
/// the expensive mistake and the strict one only costs a model call.
pub fn classify_phrase(label: &str) -> Option<PhraseId> {
    let normalized = normalize_label(label)?;
    if normalized.is_empty() {
        return None;
    }
    PhraseId::ALL
        .iter()
        .copied()
        .find(|phrase| forms_of(*phrase).contains(&normalized.as_str()))
}

#[cfg(test)]
mod tests {
    use super::{classify_phrase, normalize_label, phrase_forms, PhraseId, MAX_LABEL_BYTES};

    #[test]
    fn no_form_is_a_pattern() {
        // The refused feature comes back by somebody writing a form that reads
        // like a regular expression and finding that it "works" because
        // comparison is equality. It would not work, and the day it stopped
        // being obvious is the day this table would grow a matcher. Every
        // metacharacter of every common pattern syntax is refused here so that
        // a form which looks like one fails review rather than fails quietly.
        const METACHARACTERS: &[char] = &[
            '*', '?', '+', '[', ']', '(', ')', '{', '}', '|', '^', '$', '\\', '.', '%', '<', '>',
        ];
        for phrase in PhraseId::ALL {
            for form in phrase_forms(*phrase) {
                for character in METACHARACTERS {
                    assert!(
                        !form.contains(*character),
                        "{form} carries {character} and reads as a pattern"
                    );
                }
            }
        }
    }

    #[test]
    fn every_form_is_already_normalized_and_no_phrase_is_formless() {
        for phrase in PhraseId::ALL {
            let forms = phrase_forms(*phrase);
            assert!(!forms.is_empty(), "{} has no form", phrase.label());
            for form in forms {
                assert_eq!(
                    normalize_label(form).as_deref(),
                    Some(*form),
                    "{form} is not stored normalized"
                );
                assert!(form.len() <= MAX_LABEL_BYTES, "{form}");
            }
        }
    }

    #[test]
    fn no_form_belongs_to_two_phrases() {
        // `classify_phrase` takes the first match, so a shared form would make
        // one phrase unreachable — and the reviewer would go on reading a
        // catalogue that lists it.
        let mut seen: Vec<(&str, &str)> = Vec::new();
        for phrase in PhraseId::ALL {
            for form in phrase_forms(*phrase) {
                if let Some((owner, _)) = seen.iter().find(|(_, other)| other == form) {
                    unreachable!("{form} belongs to both {owner} and {}", phrase.label());
                }
                seen.push((phrase.label(), form));
            }
        }
    }

    #[test]
    fn classification_is_equality_and_never_containment() {
        assert_eq!(classify_phrase("Sign In"), Some(PhraseId::SignIn));
        assert_eq!(classify_phrase("  sign\tin  "), Some(PhraseId::SignIn));
        // Containment, prefix and suffix all answer nothing.
        for label in [
            "sign in to continue",
            "please sign in",
            "sign",
            "signing in",
            "sign-in",
        ] {
            assert_eq!(classify_phrase(label), None, "{label}");
        }
    }

    #[test]
    fn normalization_does_only_the_three_things_it_says() {
        assert_eq!(
            normalize_label("  Add   To\nCart ").as_deref(),
            Some("add to cart")
        );
        // Punctuation stays, and non-ASCII case is left exactly as the page
        // wrote it: `É` is not lowered while the ASCII beside it is. That is
        // the visible cost of refusing Unicode case folding, and it is the
        // cheaper half of the trade — a folded phrase would match or not match
        // depending on a locale nobody looked at, whereas this one simply needs
        // its form catalogued the way the page spells it.
        assert_eq!(normalize_label("Sign-In!").as_deref(), Some("sign-in!"));
        assert_eq!(normalize_label("ÉCOUTER").as_deref(), Some("Écouter"));
        assert_eq!(normalize_label("").as_deref(), Some(""));
    }

    #[test]
    fn a_label_past_the_bound_matches_nothing_rather_than_being_truncated() {
        let long = "sign in".to_owned() + &" ".repeat(MAX_LABEL_BYTES);
        assert_eq!(normalize_label(&long), None);
        assert_eq!(classify_phrase(&long), None);
        // A truncating implementation would have said `SignIn` here, which is
        // the whole reason the bound refuses instead of trimming.
        assert_eq!(classify_phrase(""), None);
    }

    #[test]
    fn every_phrase_has_a_distinct_compiled_in_label_and_classifies_from_its_forms() {
        let mut seen: Vec<&str> = Vec::new();
        for phrase in PhraseId::ALL {
            assert!(!seen.contains(&phrase.label()), "{}", phrase.label());
            seen.push(phrase.label());
            for form in phrase_forms(*phrase) {
                assert_eq!(classify_phrase(form), Some(*phrase), "{form}");
            }
        }
    }
}
