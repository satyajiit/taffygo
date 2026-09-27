// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one lifecycle, and who may move it.
//!
//! `Draft → Active → Superseded → Retired`, with `Disabled` reachable from any
//! of them and reachable **only** by a person.
//!
//! # One lifecycle because there is one record
//!
//! A second status enumeration for recorded procedures would be a second place
//! to express "turned off", and a person who turned one off would have turned
//! off half of what they were looking at. There is one enumeration, and it does
//! not read [`crate::ProcedureProvenance`].
//!
//! # Why the actor is an argument
//!
//! Turning a procedure off is a standing decision about what the assistant may
//! do without being asked again, and record 0009's "personalization is
//! configuration" only holds while the configuration is a person's. If the
//! product could reach `Disabled` on its own then a procedure could be quietly
//! suspended and quietly resumed, and the person who thought they had turned it
//! off would have no way to tell the two apart. So the actor is an argument
//! rather than an assumption, and [`LifecycleRefusal::NotAPerson`] is a value a
//! caller has to handle.
//!
//! Leaving `Disabled` is a person's act for the same reason and in the same
//! direction: it returns to [`ProcedureStatus::Draft`], never to `Active`.
//! Re-enabling something straight into use would restore a standing arrangement
//! whose steps nobody has re-read, which is the state the disabling was about.

/// Where a procedure is in its one lifecycle.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ProcedureStatus {
    /// Written down and not in use. Every procedure enters here.
    Draft,
    /// In use.
    Active,
    /// Replaced by a later version, and kept so an audit record can still name
    /// the steps that ran.
    Superseded,
    /// Out of use for good.
    Retired,
    /// Turned off by a person.
    Disabled,
}

impl ProcedureStatus {
    /// Every member, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Draft,
        Self::Active,
        Self::Superseded,
        Self::Retired,
        Self::Disabled,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Draft => "draft",
            Self::Active => "active",
            Self::Superseded => "superseded",
            Self::Retired => "retired",
            Self::Disabled => "disabled",
        }
    }

    /// Whether a procedure in this status may be proposed from.
    ///
    /// Exactly one member says yes. A draft is not "nearly active" and a
    /// superseded procedure is not "the previous good one": both are records
    /// kept for a person to read.
    pub const fn is_runnable(self) -> bool {
        matches!(self, Self::Active)
    }
}

/// Who is asking for a lifecycle move.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LifecycleActor {
    /// The person whose profile holds the procedure.
    Person,
    /// The product, on its own initiative.
    Product,
}

impl LifecycleActor {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Person => "person",
            Self::Product => "product",
        }
    }
}

/// Why a lifecycle move was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LifecycleRefusal {
    /// The move is not one this lifecycle has.
    NotAStep,
    /// The move exists and only a person may make it.
    NotAPerson,
    /// The move is to the status the procedure already holds. Refused rather
    /// than treated as a no-op: a caller that meant to advance and did not is
    /// better off being told.
    AlreadyThere,
}

impl LifecycleRefusal {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::NotAStep => "not_a_step",
            Self::NotAPerson => "not_a_person",
            Self::AlreadyThere => "already_there",
        }
    }
}

/// Every move this lifecycle has, as `(from, to)`.
///
/// A table rather than a match. What a reviewer needs to check here is *which
/// moves exist*, and a set of arms all returning `true` reads as several rules
/// when it is one list — the two shapes are also indistinguishable to a reader
/// counting what is reachable. `every_pair_of_statuses_is_decided` walks the
/// whole cross product against it, so a move that is neither listed nor
/// deliberately absent cannot hide.
const LIFECYCLE_STEPS: &[(ProcedureStatus, ProcedureStatus)] = &[
    // The forward sequence, one hop at a time. Skipping a hop is refused
    // because each hop is a separate thing somebody decided.
    (ProcedureStatus::Draft, ProcedureStatus::Active),
    (ProcedureStatus::Active, ProcedureStatus::Superseded),
    (ProcedureStatus::Superseded, ProcedureStatus::Retired),
    // Off, from anywhere that is not already off.
    (ProcedureStatus::Draft, ProcedureStatus::Disabled),
    (ProcedureStatus::Active, ProcedureStatus::Disabled),
    (ProcedureStatus::Superseded, ProcedureStatus::Disabled),
    (ProcedureStatus::Retired, ProcedureStatus::Disabled),
    // Back on, as a draft and never straight into use.
    (ProcedureStatus::Disabled, ProcedureStatus::Draft),
];

/// Whether `to` is a move this lifecycle has at all, ignoring who is asking.
fn is_a_step(from: ProcedureStatus, to: ProcedureStatus) -> bool {
    LIFECYCLE_STEPS
        .iter()
        .any(|(listed_from, listed_to)| *listed_from == from && *listed_to == to)
}

/// Whether a move may only be made by a person.
const fn needs_a_person(from: ProcedureStatus, to: ProcedureStatus) -> bool {
    matches!(to, ProcedureStatus::Disabled) || matches!(from, ProcedureStatus::Disabled)
}

/// The status after `actor` moves a procedure from `from` to `to`, or why not.
pub fn transition(
    from: ProcedureStatus,
    to: ProcedureStatus,
    actor: LifecycleActor,
) -> Result<ProcedureStatus, LifecycleRefusal> {
    if from == to {
        return Err(LifecycleRefusal::AlreadyThere);
    }
    if !is_a_step(from, to) {
        return Err(LifecycleRefusal::NotAStep);
    }
    if needs_a_person(from, to) && matches!(actor, LifecycleActor::Product) {
        return Err(LifecycleRefusal::NotAPerson);
    }
    Ok(to)
}

#[cfg(test)]
mod tests {
    use super::{transition, LifecycleActor, LifecycleRefusal, ProcedureStatus};

    #[test]
    fn the_forward_sequence_is_one_hop_at_a_time() {
        assert_eq!(
            transition(
                ProcedureStatus::Draft,
                ProcedureStatus::Active,
                LifecycleActor::Product
            ),
            Ok(ProcedureStatus::Active)
        );
        assert_eq!(
            transition(
                ProcedureStatus::Active,
                ProcedureStatus::Superseded,
                LifecycleActor::Product
            ),
            Ok(ProcedureStatus::Superseded)
        );
        assert_eq!(
            transition(
                ProcedureStatus::Superseded,
                ProcedureStatus::Retired,
                LifecycleActor::Product
            ),
            Ok(ProcedureStatus::Retired)
        );
        // Skipping a hop, and going backwards, are both refused.
        assert_eq!(
            transition(
                ProcedureStatus::Draft,
                ProcedureStatus::Superseded,
                LifecycleActor::Person
            ),
            Err(LifecycleRefusal::NotAStep)
        );
        assert_eq!(
            transition(
                ProcedureStatus::Retired,
                ProcedureStatus::Active,
                LifecycleActor::Person
            ),
            Err(LifecycleRefusal::NotAStep)
        );
    }

    #[test]
    fn only_a_person_can_turn_a_procedure_off_or_back_on() {
        for from in [
            ProcedureStatus::Draft,
            ProcedureStatus::Active,
            ProcedureStatus::Superseded,
            ProcedureStatus::Retired,
        ] {
            assert_eq!(
                transition(from, ProcedureStatus::Disabled, LifecycleActor::Person),
                Ok(ProcedureStatus::Disabled),
                "{}",
                from.label()
            );
            assert_eq!(
                transition(from, ProcedureStatus::Disabled, LifecycleActor::Product),
                Err(LifecycleRefusal::NotAPerson),
                "{}",
                from.label()
            );
        }
        assert_eq!(
            transition(
                ProcedureStatus::Disabled,
                ProcedureStatus::Draft,
                LifecycleActor::Product
            ),
            Err(LifecycleRefusal::NotAPerson)
        );
    }

    #[test]
    fn leaving_disabled_lands_in_draft_and_never_straight_into_use() {
        assert_eq!(
            transition(
                ProcedureStatus::Disabled,
                ProcedureStatus::Draft,
                LifecycleActor::Person
            ),
            Ok(ProcedureStatus::Draft)
        );
        assert_eq!(
            transition(
                ProcedureStatus::Disabled,
                ProcedureStatus::Active,
                LifecycleActor::Person
            ),
            Err(LifecycleRefusal::NotAStep)
        );
    }

    #[test]
    fn moving_to_the_status_already_held_is_refused_rather_than_ignored() {
        for status in ProcedureStatus::ALL {
            assert_eq!(
                transition(*status, *status, LifecycleActor::Person),
                Err(LifecycleRefusal::AlreadyThere),
                "{}",
                status.label()
            );
        }
    }

    #[test]
    fn exactly_one_status_is_runnable() {
        let runnable: Vec<&str> = ProcedureStatus::ALL
            .iter()
            .filter(|status| status.is_runnable())
            .map(|status| status.label())
            .collect();
        assert_eq!(runnable, vec!["active"]);
    }

    #[test]
    fn every_pair_of_statuses_is_decided() {
        // The whole cross product, so a move that is neither listed as a step
        // nor deliberately absent cannot exist. A person acts, because a person
        // may make every move the product may and one more.
        for from in ProcedureStatus::ALL {
            for to in ProcedureStatus::ALL {
                let verdict = transition(*from, *to, LifecycleActor::Person);
                let listed = super::LIFECYCLE_STEPS
                    .iter()
                    .any(|(listed_from, listed_to)| listed_from == from && listed_to == to);
                let expected = if from == to {
                    Err(LifecycleRefusal::AlreadyThere)
                } else if listed {
                    Ok(*to)
                } else {
                    Err(LifecycleRefusal::NotAStep)
                };
                assert_eq!(verdict, expected, "{} -> {}", from.label(), to.label());
            }
        }
    }

    #[test]
    fn every_status_has_a_distinct_compiled_in_label() {
        let mut seen: Vec<&str> = Vec::new();
        for status in ProcedureStatus::ALL {
            assert!(!seen.contains(&status.label()), "{}", status.label());
            seen.push(status.label());
        }
    }
}
