// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The content-trust lattice (specification section 9.4).
//!
//! [`Sensitivity`](crate::sensitivity::Sensitivity) says how much harm the
//! disclosure of a value would cause. This module answers a different question
//! — who wrote it — and the two axes are independent. A bank statement is
//! highly sensitive and first-party; a comment on the same page is not
//! sensitive at all and was written by a stranger. Neither answer may be
//! derived from the other, and treating one as a proxy for the other is the
//! mistake this axis exists to prevent.
//!
//! Three rules from section 9.4 are encoded rather than described:
//!
//! - a derived value carries the labels of everything it was derived from, so
//!   combining evidence can only add authors and can never remove one;
//! - an absent label is [`ContentTrust::UnknownUntrusted`], never an unlabelled
//!   default, so a renderer that omits the field is treated as strictly as one
//!   that admits it does not know;
//! - [`ContentTrust::UserAuthored`] and [`ContentTrust::TaffyAuthored`] are the
//!   only labels the isolated core mints for itself, and a renderer that claims one is
//!   refused by the browser broker the way a renderer-reported origin is
//!   overwritten. Nothing in this module can raise a label.
//!
//! # This is a set, not a ranking
//!
//! There is deliberately no ordering. A document is not more or less
//! trustworthy than a transcript; they are different authors, and a value
//! derived from both was written by both. The only order is set containment,
//! which is what [`TrustSet::is_at_least_as_untrusted_as`] answers. A `rank`
//! function would invite `if trust <= PageRendered`, which is a comparison
//! nobody can defend and which would quietly decide policy questions this
//! module has no business deciding.
//!
//! What this module does not do is decide policy. It says who wrote something;
//! `policy-engine` owns whether a given task, origin, or destination may
//! receive it, and may only make the answer stricter.

pub use crate::generated::snapshot::ContentTrust;

/// The labels that name an author whose content is not the product's own, in
/// schema order.
///
/// [`ContentTrust::UserAuthored`] and [`ContentTrust::TaffyAuthored`] are
/// absent: they are the two the product mints for itself, and a destination
/// policy is never written about them. [`ContentTrust::UnknownUntrusted`] is
/// absent for the opposite reason — it is the deliberate absence of a name, and
/// is handled at least as strictly as any label here.
pub const NAMED_UNTRUSTED: &[ContentTrust] = &[
    ContentTrust::FirstPartyDocument,
    ContentTrust::UserGeneratedContent,
    ContentTrust::ThirdPartyEmbedded,
    ContentTrust::ModelAuthored,
];

impl ContentTrust {
    /// The label an observation carries when the wire field was absent.
    ///
    /// Absence is not evidence of a trustworthy author. An endpoint that never
    /// ran the labeller and one that ran it and could not attribute a run are
    /// indistinguishable here, and both resolve to the strictest answer.
    #[must_use]
    pub const fn of(label: Option<Self>) -> Self {
        match label {
            Some(known) => known,
            None => Self::UnknownUntrusted,
        }
    }

    /// Whether content with this label may carry an instruction a page or a
    /// model wrote.
    ///
    /// True for everything except the two labels the product mints for itself.
    /// It is deliberately true for [`Self::UnknownUntrusted`]: an author nobody
    /// can name is an author who might be anyone.
    #[must_use]
    pub const fn may_carry_foreign_instructions(self) -> bool {
        !matches!(self, Self::UserAuthored | Self::TaffyAuthored)
    }

    /// The lattice bit for this label.
    ///
    /// [`Self::UserAuthored`] carries no bit and is the bottom element, so a
    /// set that contains nothing is exactly content the person wrote.
    const fn bit(self) -> u16 {
        match self {
            Self::UserAuthored => 0,
            Self::TaffyAuthored => 1 << 0,
            Self::FirstPartyDocument => 1 << 1,
            Self::UserGeneratedContent => 1 << 2,
            Self::ThirdPartyEmbedded => 1 << 3,
            Self::ModelAuthored => 1 << 4,
            Self::UnknownUntrusted => 1 << 5,
        }
    }
}

/// A point in the trust lattice: every author a value has drawn from.
///
/// Authorship arrives from several places — a renderer adapter, the browser's
/// own frame topology, the person's input, a model adapter, a stored record
/// read back — and a derived value belongs to all of them. The lattice element
/// is therefore the set of everything that contributed, joined by union:
///
/// - [`Self::EMPTY`] is the bottom element and means the person wrote it;
/// - [`Self::join`] is the least upper bound: commutative, associative,
///   idempotent, and never smaller than either input;
/// - [`Self::is_at_least_as_untrusted_as`] is the partial order, which is set
///   containment.
///
/// There is no complement, no difference, and no removal, because section 9.4
/// allows an author to be added to a value's history and never taken out of it.
/// This is the same shape as
/// [`SensitivitySet`](crate::sensitivity::SensitivitySet), and deliberately so:
/// two lattices a reader has to hold at once should not disagree about what a
/// join means.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[must_use]
pub struct TrustSet(u16);

impl TrustSet {
    /// The bottom of the lattice: content the person wrote, and nothing else.
    pub const EMPTY: Self = Self(0);

    /// The set holding one label.
    pub const fn of(label: ContentTrust) -> Self {
        Self(label.bit())
    }

    /// The set holding whatever a wire field carried, treating absence as
    /// [`ContentTrust::UnknownUntrusted`].
    pub const fn of_optional(label: Option<ContentTrust>) -> Self {
        Self::of(ContentTrust::of(label))
    }

    /// The least upper bound of two elements: every author either one carries.
    ///
    /// This is the whole propagation rule. Every hop in the pipeline joins, and
    /// no hop assigns: an answer a model wrote from a page was written by both,
    /// so it carries both.
    pub const fn join(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    /// This element joined with one more label.
    ///
    /// The result never carries fewer authors than the input, whatever is
    /// added.
    pub const fn with(self, label: ContentTrust) -> Self {
        self.join(Self::of(label))
    }

    /// Whether this element carries `label`.
    ///
    /// [`ContentTrust::UserAuthored`] carries no bit, so asking for it asks
    /// whether the set is empty.
    pub const fn contains(self, label: ContentTrust) -> bool {
        let bit = label.bit();
        if bit == 0 {
            self.0 == 0
        } else {
            self.0 & bit == bit
        }
    }

    /// Whether nothing but the person contributed to this value.
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }

    /// Whether this element carries every author `other` carries.
    ///
    /// This is the lattice's partial order. It holds exactly when this element
    /// is at least as untrusted as `other` everywhere, which is what makes
    /// [`Self::join`] safe to apply to evidence from sources that disagree: the
    /// result is at least as untrusted as each of them.
    pub const fn is_at_least_as_untrusted_as(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }

    /// Whether anything here could carry an instruction a page or a model
    /// wrote.
    ///
    /// The question the egress check asks before letting free text reach a
    /// destination the person did not name.
    #[must_use]
    pub fn carries_foreign_instructions(self) -> bool {
        self.members()
            .into_iter()
            .any(ContentTrust::may_carry_foreign_instructions)
    }

    /// Whether a model contributed to this value.
    pub const fn is_model_derived(self) -> bool {
        self.contains(ContentTrust::ModelAuthored)
    }

    /// Whether any contributing author could not be named.
    pub const fn has_unknown_author(self) -> bool {
        self.contains(ContentTrust::UnknownUntrusted)
    }

    /// Every label in this element, in schema order.
    ///
    /// An empty element yields an empty list, not
    /// [`ContentTrust::UserAuthored`]: the absence of a contributing author is
    /// not a contributing author.
    #[must_use]
    pub fn members(self) -> Vec<ContentTrust> {
        ContentTrust::ALL
            .iter()
            .copied()
            .filter(|label| {
                let bit = label.bit();
                bit != 0 && self.0 & bit == bit
            })
            .collect()
    }
}

impl FromIterator<ContentTrust> for TrustSet {
    fn from_iter<I: IntoIterator<Item = ContentTrust>>(iter: I) -> Self {
        iter.into_iter().fold(Self::EMPTY, Self::with)
    }
}

impl Extend<ContentTrust> for TrustSet {
    fn extend<I: IntoIterator<Item = ContentTrust>>(&mut self, iter: I) {
        for label in iter {
            *self = self.with(label);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{ContentTrust, TrustSet, NAMED_UNTRUSTED};

    #[test]
    fn the_empty_element_is_exactly_content_the_person_wrote() {
        assert!(TrustSet::EMPTY.is_empty());
        assert!(TrustSet::EMPTY.contains(ContentTrust::UserAuthored));
        assert_eq!(TrustSet::of(ContentTrust::UserAuthored), TrustSet::EMPTY);
        assert!(TrustSet::EMPTY.members().is_empty());
        assert!(!TrustSet::EMPTY.carries_foreign_instructions());
    }

    #[test]
    fn an_absent_label_is_the_strictest_one_and_never_a_default() {
        assert_eq!(ContentTrust::of(None), ContentTrust::UnknownUntrusted);
        let absent = TrustSet::of_optional(None);
        assert!(absent.has_unknown_author());
        assert!(absent.carries_foreign_instructions());
    }

    #[test]
    fn every_named_author_occupies_its_own_place_in_the_lattice() {
        for label in NAMED_UNTRUSTED {
            let element = TrustSet::of(*label);
            assert_eq!(element.members(), vec![*label]);
            assert!(!element.is_empty());
            assert!(element.carries_foreign_instructions());
        }
    }

    #[test]
    fn a_join_never_drops_an_author_whichever_way_round_it_is_asked() {
        for left in ContentTrust::ALL {
            for right in ContentTrust::ALL {
                let a = TrustSet::of(*left);
                let b = TrustSet::of(*right);
                let joined = a.join(b);
                assert_eq!(joined, b.join(a));
                assert!(joined.is_at_least_as_untrusted_as(a));
                assert!(joined.is_at_least_as_untrusted_as(b));
                assert_eq!(joined.join(joined), joined);
            }
        }
    }

    #[test]
    fn an_answer_a_model_wrote_from_a_page_was_written_by_both() {
        let page = TrustSet::of(ContentTrust::FirstPartyDocument);
        let answer = page.with(ContentTrust::ModelAuthored);

        assert!(answer.is_model_derived());
        assert!(answer.contains(ContentTrust::FirstPartyDocument));
        assert!(answer.is_at_least_as_untrusted_as(page));
        assert!(!page.is_at_least_as_untrusted_as(answer));
    }

    #[test]
    fn only_the_two_labels_the_product_mints_carry_no_foreign_instruction() {
        let own: Vec<ContentTrust> = ContentTrust::ALL
            .iter()
            .copied()
            .filter(|label| !label.may_carry_foreign_instructions())
            .collect();
        assert_eq!(
            own,
            vec![ContentTrust::UserAuthored, ContentTrust::TaffyAuthored]
        );
    }
}
