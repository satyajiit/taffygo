// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which half of a two-step authorization a request is (decision
//! `docs/decisions/0022-prepare-and-commit-for-consequential-actions.md`).
//!
//! A consequential effect is authorized twice. A **prepare** stages it and has
//! no consequence outside the browser process; a **commit** causes it, names
//! the prepare it completes, and carries a digest over the effect exactly as
//! the trusted surface rendered it.
//!
//! The phase is a property of the request, never of the action class. A
//! submission's consequence does not change because it arrived in two calls,
//! so there is no prepared action class — a class whose entire purpose was to
//! look cheaper is the shape the risk lattice exists to refuse.
//!
//! # Why there is no lowering function here
//!
//! [`crate::risk`] states, in the module that owns the lattice, that no
//! operation there lowers a reading, and the threat model states that effective
//! risk can only increase. A prepare *is* cheaper than the commit it stages,
//! and the way that is expressed is a phase-aware **baseline**:
//! [`crate::action_class::ActionClass::baseline_risk_in`] is a total table over
//! class and phase, joined with context exactly as before. A function taking a
//! risk class and returning a lower one would have identical expressive power
//! and would turn the strongest property in this crate into a convention that
//! holds because everyone remembers it.
//!
//! [`ActionPhase::join`] is a maximum over the phases themselves, and the
//! baseline table is monotone in it, so joining phases never lowers a baseline.
//! That is the property `tests/phase_risk_properties.rs` generates over: a
//! composition that reduced risk is exactly how a dangerous step would hide
//! behind a safe one.

/// Which half of a two-step authorization a request is.
///
/// Declared least consequential first, so the derived order is by consequence
/// and [`Self::join`] is a maximum. There is deliberately no `Default`: a
/// request that did not say which half it is has not been decided, and
/// defaulting it would decide it silently in whichever direction the default
/// happened to point.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ActionPhase {
    /// Staging an effect so a person can read exactly what would happen.
    /// Nothing leaves the browser process, and preparation mints nothing that
    /// the commit does not have to ask for again.
    Prepare,
    /// Causing the effect. A single-step action is a commit: nothing was
    /// staged, so there is no cheaper reading of it to take.
    Commit,
}

impl ActionPhase {
    /// Every phase, least consequential first.
    pub const ALL: &'static [Self] = &[Self::Prepare, Self::Commit];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Prepare => "prepare",
            Self::Commit => "commit",
        }
    }

    /// The more consequential of two phases.
    ///
    /// The only combinator this type has, and the same shape as
    /// [`crate::risk::RiskClass::join`]: there is no `meet` and no `lower`,
    /// because a phase that could be argued downwards would be a way to ask for
    /// a commit under a prepare's baseline.
    #[must_use]
    pub fn join(self, other: Self) -> Self {
        if self >= other {
            self
        } else {
            other
        }
    }

    /// Whether this phase causes the effect outside the browser process.
    pub const fn causes_effect(self) -> bool {
        matches!(self, Self::Commit)
    }

    /// Whether this phase only stages an effect.
    pub const fn stages_only(self) -> bool {
        matches!(self, Self::Prepare)
    }
}

#[cfg(test)]
mod tests {
    use super::ActionPhase;

    #[test]
    fn a_join_is_the_more_consequential_phase_whichever_way_round_it_is_asked() {
        for left in ActionPhase::ALL {
            for right in ActionPhase::ALL {
                let joined = left.join(*right);
                assert_eq!(joined, right.join(*left));
                assert!(joined >= *left);
                assert!(joined >= *right);
            }
        }
    }

    #[test]
    fn nothing_here_lowers_a_phase() {
        for phase in ActionPhase::ALL {
            assert_eq!(phase.join(ActionPhase::Prepare), *phase);
            assert_eq!(phase.join(ActionPhase::Commit), ActionPhase::Commit);
        }
    }

    #[test]
    fn only_a_commit_causes_an_effect_and_only_a_prepare_stages_one() {
        for phase in ActionPhase::ALL {
            assert_ne!(
                phase.causes_effect(),
                phase.stages_only(),
                "{}",
                phase.label()
            );
        }
        assert!(ActionPhase::Commit.causes_effect());
        assert!(ActionPhase::Prepare.stages_only());
    }

    #[test]
    fn every_phase_has_a_distinct_compiled_in_name() {
        let mut labels: Vec<&str> = ActionPhase::ALL.iter().map(|phase| phase.label()).collect();
        let count = labels.len();
        labels.sort_unstable();
        labels.dedup();
        assert_eq!(labels.len(), count);
    }
}
