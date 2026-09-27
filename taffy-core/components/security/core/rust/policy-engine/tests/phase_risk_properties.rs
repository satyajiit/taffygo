// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Phase-aware baseline risk: the composition property (decision
//! `docs/decisions/0022-prepare-and-commit-for-consequential-actions.md`).
//!
//! Decision 0022 rejects a function that takes a risk class and returns a lower
//! one, and expresses "a prepare is cheaper than a commit" as a phase-aware
//! *baseline* instead — a total table over class and phase, joined with context
//! exactly as before. The reason the record gives for preferring the table is
//! that it keeps the crate's strongest property literally true rather than true
//! by convention.
//!
//! A table is only as good as its monotonicity, and that is what is generated
//! over here. The claim under test is:
//!
//! > **No join of phases ever lowers the baseline risk.**
//!
//! A composition that reduced risk is precisely how a dangerous step would hide
//! behind a safe one: stage the purchase, join the phase of the harmless read
//! that follows it, and commit at the read's price. The property is universally
//! quantified over classes, phases, contexts and *sequences* of phases, so it
//! is generated rather than exampled — an example suite would check the pairs
//! somebody thought of, and this hazard lives in the pair nobody did.
//!
//! Two further properties keep the first one honest. `join` has to be a real
//! semilattice operation, or "the join" would not name one value; and the
//! context join has to stay monotone in the phase, or the phase could be
//! lowered at one remove by choosing the context that swallows it.

// proptest's generated harness is not written under this crate's lint budget.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use proptest::prelude::*;

use policy_engine::{ActionClass, ActionPhase, PolicyMilestone, RiskClass};

/// Any phase.
fn any_phase() -> impl Strategy<Value = ActionPhase> {
    prop::sample::select(ActionPhase::ALL)
}

/// Any ratified milestone.
fn any_milestone() -> impl Strategy<Value = PolicyMilestone> {
    prop::sample::select(PolicyMilestone::ALL)
}

/// Any action class.
fn any_class() -> impl Strategy<Value = ActionClass> {
    prop::sample::select(ActionClass::ALL)
}

/// Any risk class, standing in for whatever context declared.
fn any_risk() -> impl Strategy<Value = RiskClass> {
    prop::sample::select(RiskClass::ALL)
}

proptest! {
    /// The property decision 0022 turns on: joining phases never lowers a
    /// baseline.
    ///
    /// For every class, the baseline at the join of two phases is at least the
    /// baseline at each of them. A composition that came out cheaper than one
    /// of its parts would be a way to ask for a commit at a prepare's price.
    #[test]
    fn a_join_of_phases_never_lowers_the_baseline(
        class in any_class(),
        left in any_phase(),
        right in any_phase(),
    ) {
        let joined = class.baseline_risk_in(left.join(right));
        prop_assert!(
            joined >= class.baseline_risk_in(left),
            "{} joined below its left phase",
            class.label()
        );
        prop_assert!(
            joined >= class.baseline_risk_in(right),
            "{} joined below its right phase",
            class.label()
        );
    }

    /// The same property over a whole sequence, not just a pair.
    ///
    /// A two-element property can hold while an n-element fold drifts, and a
    /// real proposal is a sequence of steps rather than two. Folding the phases
    /// of a run of steps must land at or above every step's own baseline.
    #[test]
    fn folding_a_run_of_phases_never_lowers_the_baseline(
        class in any_class(),
        phases in prop::collection::vec(any_phase(), 1..24),
    ) {
        let folded = phases
            .iter()
            .copied()
            .reduce(ActionPhase::join)
            .expect("the generator produces at least one phase");
        let baseline = class.baseline_risk_in(folded);
        for phase in &phases {
            prop_assert!(
                baseline >= class.baseline_risk_in(*phase),
                "{} folded below {}",
                class.label(),
                phase.label()
            );
        }
        // The fold is exactly the most consequential phase in the run, so a
        // single dangerous step cannot be averaged away by the safe ones
        // around it.
        let highest = phases.iter().copied().max().expect("non-empty");
        prop_assert_eq!(folded, highest);
    }

    /// The phase join is a semilattice: commutative, associative, idempotent,
    /// and above both of its arguments.
    ///
    /// Without this, "the join of the phases" would not name one value, and the
    /// property above would be checking an operation that depends on the order
    /// the steps happened to be written in.
    #[test]
    fn the_phase_join_is_a_semilattice(
        left in any_phase(),
        middle in any_phase(),
        right in any_phase(),
    ) {
        prop_assert_eq!(left.join(right), right.join(left));
        prop_assert_eq!(
            left.join(middle).join(right),
            left.join(middle.join(right))
        );
        prop_assert_eq!(left.join(left), left);
        prop_assert!(left.join(right) >= left);
        prop_assert!(left.join(right) >= right);
    }

    /// Context cannot lower what the phase established, and the phase cannot
    /// lower what context established.
    ///
    /// The effective reading is the join of the phase-aware baseline with
    /// whatever context declared. Both halves are checked because the hazard is
    /// symmetric: a phase that could be swallowed by a context is a phase that
    /// can be argued downwards at one remove.
    #[test]
    fn joining_context_with_a_phase_lowers_neither(
        class in any_class(),
        phase in any_phase(),
        context in any_risk(),
    ) {
        let baseline = class.baseline_risk_in(phase);
        let effective = baseline.join(context);
        prop_assert!(effective >= baseline, "{}", class.label());
        prop_assert!(effective >= context, "{}", class.label());
    }

    /// Preparing an effect never makes it authorizable.
    ///
    /// Decision 0022's stated consequence: "a prepare of an excluded action
    /// still lands at a class no ratified milestone authorizes, so nothing here
    /// makes anything authorizable that is not authorizable today". If the
    /// committed reading cannot be authorized, neither may the prepared one,
    /// whatever context is joined on top.
    ///
    /// # What decision 0089 did to this property, stated rather than dropped
    ///
    /// The claim above is about the *lattice alone*, and at M5 the lattice
    /// alone no longer carries it. `SendMessage` and `Purchase` are the two
    /// classes whose phase row moves: decision 0022 reads a staged send as a
    /// draft — a sensitive disclosure — and its commit as an excluded
    /// commitment. Until M5 neither reading was authorizable, so "the phase
    /// widens nothing" was true of the risk half by itself. From M5 a sensitive
    /// disclosure *is* authorizable, so the prepared reading of a send is, and
    /// the committed one still is not.
    ///
    /// Nothing became reachable. `authorized_classes` names neither class at
    /// any milestone, both deciders ask the class gate before the risk gate,
    /// and decision 0089 section 1 turned "no allowlist names an excluded or
    /// prohibited class" into a compile-time assertion rather than a habit. So
    /// the milestone-aware half of this property is stated as what it now is:
    /// wherever preparing outruns committing, the *class surface* is what
    /// refuses it — which is a claim that fails loudly if anybody ever puts one
    /// of those two classes on an allowlist.
    #[test]
    fn preparing_an_unauthorizable_class_never_makes_it_authorizable(
        class in any_class(),
        context in any_risk(),
        milestone in any_milestone(),
    ) {
        let committed = class.baseline_risk_in(ActionPhase::Commit);
        let prepared = class.baseline_risk_in(ActionPhase::Prepare).join(context);
        if !committed.can_be_authorized_today() {
            prop_assert!(
                !prepared.can_be_authorized_today(),
                "preparing {} became authorizable",
                class.label()
            );
        }
        if prepared.can_be_authorized_at(milestone) && !committed.can_be_authorized_at(milestone) {
            prop_assert!(
                !class.is_authorized_at(milestone),
                "preparing {} outruns committing it at {} and the class surface does not refuse it",
                class.label(),
                milestone.label()
            );
        }
    }
}

/// The table is total and the commit column is the reading the crate already
/// had.
///
/// Exhaustive rather than generated, because the domain is small enough to
/// enumerate and "for every class and every phase" is then a fact rather than a
/// sample.
#[test]
fn the_phase_table_is_total_and_the_commit_column_is_unchanged() {
    for class in ActionClass::ALL {
        for phase in ActionPhase::ALL {
            // Totality: every cell answers, and the answer is a real member of
            // the lattice.
            let baseline = class.baseline_risk_in(*phase);
            assert!(RiskClass::ALL.contains(&baseline), "{}", class.label());
        }
        assert_eq!(
            class.baseline_risk(),
            class.baseline_risk_in(ActionPhase::Commit),
            "the commit column moved for {}",
            class.label()
        );
        assert!(
            class.baseline_risk_in(ActionPhase::Prepare)
                <= class.baseline_risk_in(ActionPhase::Commit),
            "preparing {} costs more than committing it",
            class.label()
        );
    }
}

/// Nothing in the crate's public surface turns a risk class into a lower one.
///
/// The alternative decision 0022 rejected was a lowering function fenced to a
/// single call site. This asserts the shape of what was built instead: `join`
/// is the only combinator, and over the whole lattice it never returns below
/// either argument — so there is no pair of readings whose combination is a
/// discount.
#[test]
fn no_combination_of_risk_readings_is_a_discount() {
    for left in RiskClass::ALL {
        for right in RiskClass::ALL {
            let joined = left.join(*right);
            assert!(joined >= *left, "{} joined below itself", left.label());
            assert!(joined >= *right, "{} joined below itself", right.label());
            assert_eq!(joined, right.join(*left));
        }
    }
}
