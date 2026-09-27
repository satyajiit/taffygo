// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Effective risk classes (threat model section 8.4).
//!
//! Risk is a property of the whole context, not of a tool name: the same class
//! of effect is ordinary on one origin and a commitment on another. This module
//! carries the ordered lattice and the one rule that makes it a control rather
//! than a label:
//!
//! **Effective risk only rises.** [`RiskClass::join`] is a maximum, and there
//! is no operation here that lowers one. Content, destination, ambiguity, and
//! state can each raise the class; a page, a model, or a skill can raise it and
//! can never lower it, because nothing in this module accepts a value from one.
//!
//! The names are descriptive rather than numbered. The threat model's own table
//! is the authority for what each one covers; a number repeated here would be a
//! second place for it to drift.
//!
//! # Authorizable and approvable are two different questions
//!
//! [`RiskClass::can_be_authorized_at`] asks which classes a milestone's
//! ratified surface may authorize at all, and it widens: decision 0089 made
//! [`RiskClass::SensitiveDisclosure`] authorizable at
//! [`crate::action_class::PolicyMilestone::M5`].
//! [`RiskClass::requires_exact_approval`] asks which classes may not be
//! authorized without a person having answered one exact question, and it does
//! not widen at all — it is a property of the lattice, not of a milestone.
//!
//! Both are true of a sensitive disclosure at M5, and they have to be, because
//! collapsing them into one predicate is how a milestone ratification turns
//! into a silent removal of the confirmation the ratification was granted on.

use crate::action_class::PolicyMilestone;

/// How consequential an action is, once its context is taken into account.
///
/// Ordered from least to most consequential, so a join is a maximum and a
/// comparison needs no table.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum RiskClass {
    /// A local read or transform that discloses nothing new.
    LocalRead,
    /// A reversible navigation or read to an origin the user has been shown,
    /// including transmitting a search query.
    ReversibleDisclosure,
    /// Sensitive-data disclosure, upload, download, an external intent, a form
    /// mutation, or a drafted message. Reserved for the write milestone.
    SensitiveDisclosure,
    /// A purchase, a send or publish, an account or security change, a legal or
    /// financial commitment, or a destructive delete. Outside this release
    /// until a separate explicit decision.
    ExcludedCommitment,
    /// Credential or secret extraction, defeating an access control, or another
    /// prohibited abuse. No milestone and no approval enables it.
    ProhibitedAbuse,
}

impl RiskClass {
    /// Every class, least consequential first.
    pub const ALL: &'static [Self] = &[
        Self::LocalRead,
        Self::ReversibleDisclosure,
        Self::SensitiveDisclosure,
        Self::ExcludedCommitment,
        Self::ProhibitedAbuse,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::LocalRead => "local_read",
            Self::ReversibleDisclosure => "reversible_disclosure",
            Self::SensitiveDisclosure => "sensitive_disclosure",
            Self::ExcludedCommitment => "excluded_commitment",
            Self::ProhibitedAbuse => "prohibited_abuse",
        }
    }

    /// The more consequential of two readings.
    ///
    /// The only combinator this type has. There is deliberately no `meet`, no
    /// `lower`, and no `override`: risk that could be argued downwards would
    /// stop being a control the moment a page learned to argue.
    #[must_use]
    pub fn join(self, other: Self) -> Self {
        if self >= other {
            self
        } else {
            other
        }
    }

    /// Whether the read-oriented surface could authorize an action at this
    /// class.
    ///
    /// Only the two read-oriented classes. This is what
    /// [`Self::can_be_authorized_at`] answers for M2 and M3, and it is not the
    /// production gate: since decision 0089 the product runs at
    /// [`PolicyMilestone::M5`], where a sensitive disclosure is authorizable.
    /// Use [`Self::can_be_authorized_at`] to decide anything; this predicate
    /// stays because two tests state what the surface used to be, and because
    /// the phase table's own property — that preparing an excluded class never
    /// makes it authorizable — is about the lattice rather than a milestone.
    pub const fn can_be_authorized_today(self) -> bool {
        matches!(self, Self::LocalRead | Self::ReversibleDisclosure)
    }

    /// Whether `milestone` could authorize an action at this class.
    ///
    /// The milestone-aware form of the risk gate, and the reason it exists is
    /// worth writing down. The class surface in [`crate::action_class`] and
    /// this lattice are two independent gates over one decision, so ratifying
    /// [`crate::action_class::ActionClass::FillField`] at M5 without moving
    /// this predicate would only have changed the *reason* a fill is refused:
    /// a fill's baseline is [`Self::SensitiveDisclosure`], and the risk gate
    /// runs before the approval branch in both deciders.
    ///
    /// The milestone-dependence therefore lives here, where the milestone
    /// already lives, rather than being fixed by widening
    /// [`Self::can_be_authorized_today`]. That would have been the same edit
    /// seen from the wrong side: [`Self::requires_exact_approval`] is defined
    /// as the complement of it, so widening it would have made a fill stop
    /// requiring an approval — the exact inversion decision 0089 section 3
    /// forbids.
    ///
    /// **At M5 a sensitive disclosure is authorizable, and a sensitive
    /// disclosure still requires an exact approval.** Those are two different
    /// statements about one class: the first says a milestone has ratified the
    /// surface it sits on, the second says a person has to have answered a
    /// question about this particular one. Neither implies the other, and
    /// nothing here lowers a reading — an excluded commitment and a prohibited
    /// abuse are refused at M5 exactly as they are at M2.
    pub const fn can_be_authorized_at(self, milestone: PolicyMilestone) -> bool {
        match milestone {
            PolicyMilestone::M2 | PolicyMilestone::M3 => self.can_be_authorized_today(),
            PolicyMilestone::M5 | PolicyMilestone::M6 | PolicyMilestone::M7 => matches!(
                self,
                Self::LocalRead | Self::ReversibleDisclosure | Self::SensitiveDisclosure
            ),
        }
    }

    /// Whether this class is prohibited whatever a user approves.
    pub const fn is_permanently_prohibited(self) -> bool {
        matches!(self, Self::ProhibitedAbuse)
    }

    /// Whether an action at this class needs an exact approval before it may
    /// be authorized at all.
    ///
    /// Everything above a reversible disclosure does. That the ratified surface
    /// also refuses those classes today is a second gate, not a reason to
    /// weaken this one: the requirement outlives the milestone that happens to
    /// refuse them.
    pub const fn requires_exact_approval(self) -> bool {
        !matches!(self, Self::LocalRead | Self::ReversibleDisclosure)
    }
}

#[cfg(test)]
mod tests {
    use super::RiskClass;
    use crate::action_class::PolicyMilestone;

    #[test]
    fn a_join_is_the_more_consequential_reading_whichever_way_round_it_is_asked() {
        for left in RiskClass::ALL {
            for right in RiskClass::ALL {
                let joined = left.join(*right);
                assert_eq!(joined, right.join(*left));
                assert!(joined >= *left);
                assert!(joined >= *right);
            }
        }
    }

    #[test]
    fn nothing_in_the_lattice_lowers_a_reading() {
        for class in RiskClass::ALL {
            assert_eq!(class.join(RiskClass::LocalRead), *class);
            assert_eq!(
                class.join(RiskClass::ProhibitedAbuse),
                RiskClass::ProhibitedAbuse
            );
        }
    }

    #[test]
    fn only_the_two_read_oriented_classes_can_be_authorized_today() {
        let authorized: Vec<&str> = RiskClass::ALL
            .iter()
            .filter(|class| class.can_be_authorized_today())
            .map(|class| class.label())
            .collect();
        assert_eq!(authorized, vec!["local_read", "reversible_disclosure"]);
    }

    #[test]
    fn every_class_above_a_reversible_disclosure_needs_an_exact_approval() {
        for class in RiskClass::ALL {
            assert_eq!(
                class.requires_exact_approval(),
                !class.can_be_authorized_today(),
                "{}",
                class.label()
            );
        }
        assert!(RiskClass::ProhibitedAbuse.is_permanently_prohibited());
        assert!(!RiskClass::SensitiveDisclosure.is_permanently_prohibited());
    }

    #[test]
    fn the_read_oriented_milestones_answer_exactly_what_the_read_oriented_reading_does() {
        // The two predicates must not drift. `can_be_authorized_today` is the
        // M2 and M3 column of `can_be_authorized_at`, and nothing else.
        for class in RiskClass::ALL {
            for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
                assert_eq!(
                    class.can_be_authorized_at(milestone),
                    class.can_be_authorized_today(),
                    "{} at {}",
                    class.label(),
                    milestone.label()
                );
            }
        }
    }

    #[test]
    fn the_write_milestone_authorizes_a_sensitive_disclosure_and_still_asks_about_it() {
        // The two statements decision 0089 keeps apart. The milestone widened
        // what may be authorized; it did not touch what has to be confirmed.
        assert!(RiskClass::SensitiveDisclosure.can_be_authorized_at(PolicyMilestone::M5));
        assert!(RiskClass::SensitiveDisclosure.requires_exact_approval());

        let authorized: Vec<&str> = RiskClass::ALL
            .iter()
            .filter(|class| class.can_be_authorized_at(PolicyMilestone::M5))
            .map(|class| class.label())
            .collect();
        assert_eq!(
            authorized,
            vec![
                "local_read",
                "reversible_disclosure",
                "sensitive_disclosure"
            ]
        );

        // Nothing above it moved, at any milestone. An excluded commitment and
        // a prohibited abuse are refused everywhere, which is what makes this a
        // widening of one row rather than a removal of the ceiling.
        for milestone in PolicyMilestone::ALL {
            assert!(!RiskClass::ExcludedCommitment.can_be_authorized_at(*milestone));
            assert!(!RiskClass::ProhibitedAbuse.can_be_authorized_at(*milestone));
        }
    }
}
