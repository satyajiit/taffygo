// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Injected identifier sources and the typed identifiers this crate mints.
//!
//! Nothing here reads a clock, a random source, or global state. Identifiers
//! arrive from an [`IdSource`] the caller owns, so a run of the reducer is a
//! pure function of the values it was given
//! (`docs/development/testing-and-delivery.md` section 3.2).
//!
//! Exhaustion returns `None` rather than wrapping. A repeated identifier would
//! let one plan, step, action, or artifact impersonate another, and the journal
//! would then reconcile two different things into one, so refusing to mint is
//! the only safe answer.

use core::fmt;

/// Which namespace an identifier is minted into.
///
/// Namespaces never share a counter, so an implementation cannot serve a plan
/// identifier where an action identifier was asked for.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum IdKind {
    /// A plan revision (domain model section 11.1).
    Plan,
    /// A plan step (domain model section 11.2).
    PlanStep,
    /// An action proposal (domain model section 12.1).
    Action,
    /// An artifact (domain model section 16).
    Artifact,
}

impl IdKind {
    /// Every namespace, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Plan, Self::PlanStep, Self::Action, Self::Artifact];

    /// A short, compiled-in name for the namespace.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Plan => "plan",
            Self::PlanStep => "step",
            Self::Action => "act",
            Self::Artifact => "artifact",
        }
    }
}

/// A source of opaque identifiers.
///
/// The shipping implementation draws unguessable values from the browser
/// process (domain model section 3).
pub trait IdSource {
    /// Issues the next identifier in `kind`, or `None` once the source is
    /// exhausted.
    fn next_id(&mut self, kind: IdKind) -> Option<String>;
}

impl<T> IdSource for Box<T>
where
    T: IdSource + ?Sized,
{
    fn next_id(&mut self, kind: IdKind) -> Option<String> {
        self.as_mut().next_id(kind)
    }
}

/// A deterministic counter-backed identifier source.
///
/// Values look like `plan_0`, `act_0`, `act_1`. They are predictable, so this
/// is a test and harness implementation and never the shipping one: domain
/// model section 3 requires unguessable identifiers in the product. It is here
/// because a deterministic replay test needs a conforming sequence to check
/// itself against.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SequentialIds {
    plan: u64,
    plan_step: u64,
    action: u64,
    artifact: u64,
}

impl SequentialIds {
    /// A source whose first identifier in every namespace is numbered zero.
    pub const fn new() -> Self {
        Self {
            plan: 0,
            plan_step: 0,
            action: 0,
            artifact: 0,
        }
    }
}

impl IdSource for SequentialIds {
    fn next_id(&mut self, kind: IdKind) -> Option<String> {
        let counter = match kind {
            IdKind::Plan => &mut self.plan,
            IdKind::PlanStep => &mut self.plan_step,
            IdKind::Action => &mut self.action,
            IdKind::Artifact => &mut self.artifact,
        };
        let issued = *counter;
        *counter = counter.checked_add(1)?;
        Some(format!("{}_{issued}", kind.label()))
    }
}

macro_rules! opaque_id {
    ($($name:ident => $what:literal),+ $(,)?) => { $(
        #[doc = concat!("An opaque ", $what, ".")]
        ///
        /// The value is stored as given. Nothing here parses it, and no
        /// consumer may infer structure, order, or provenance from its bytes.
        #[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
        pub struct $name(String);

        impl $name {
            /// Wraps an identifier minted by an [`IdSource`].
            pub fn new(value: impl Into<String>) -> Self {
                Self(value.into())
            }

            /// The opaque value, for equality and audit correlation only.
            pub fn as_str(&self) -> &str {
                &self.0
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                formatter.write_str(&self.0)
            }
        }
    )+ };
}

opaque_id! {
    PlanId => "plan revision identifier",
    PlanStepId => "plan step identifier",
    ArtifactId => "artifact identifier",
    IdempotencyKey => "command idempotency key",
    ModelCallId => "model call identity",
    ToolJobId => "tool job identity",
}

/// Mints a plan identifier, or `None` when the source is exhausted.
pub fn next_plan_id(ids: &mut impl IdSource) -> Option<PlanId> {
    ids.next_id(IdKind::Plan).map(PlanId::new)
}

/// Mints a plan step identifier, or `None` when the source is exhausted.
pub fn next_plan_step_id(ids: &mut impl IdSource) -> Option<PlanStepId> {
    ids.next_id(IdKind::PlanStep).map(PlanStepId::new)
}

/// Mints an action identifier, or `None` when the source is exhausted.
pub fn next_action_id(ids: &mut impl IdSource) -> Option<bip_types::identity::ActionId> {
    ids.next_id(IdKind::Action)
        .map(bip_types::identity::ActionId::new)
}

/// Mints an artifact identifier, or `None` when the source is exhausted.
pub fn next_artifact_id(ids: &mut impl IdSource) -> Option<ArtifactId> {
    ids.next_id(IdKind::Artifact).map(ArtifactId::new)
}

#[cfg(test)]
mod tests {
    use super::{IdKind, IdSource, PlanId, SequentialIds};

    #[test]
    fn identifier_namespaces_do_not_share_a_counter() {
        let mut ids = SequentialIds::new();
        assert_eq!(ids.next_id(IdKind::Plan).as_deref(), Some("plan_0"));
        assert_eq!(ids.next_id(IdKind::Action).as_deref(), Some("act_0"));
        assert_eq!(ids.next_id(IdKind::Action).as_deref(), Some("act_1"));
        assert_eq!(ids.next_id(IdKind::Plan).as_deref(), Some("plan_1"));
    }

    #[test]
    fn every_namespace_mints_and_every_label_is_distinct() {
        let mut ids = SequentialIds::new();
        let mut seen: Vec<String> = Vec::new();
        for kind in IdKind::ALL {
            let minted = ids.next_id(*kind).unwrap_or_default();
            assert!(!minted.is_empty(), "{} minted nothing", kind.label());
            assert!(!seen.contains(&minted), "{minted} repeated");
            seen.push(minted);
        }
    }

    #[test]
    fn an_opaque_identifier_round_trips_its_value_and_nothing_else() {
        let plan = PlanId::new("plan_7");
        assert_eq!(plan.as_str(), "plan_7");
        assert_eq!(plan.to_string(), "plan_7");
    }
}
