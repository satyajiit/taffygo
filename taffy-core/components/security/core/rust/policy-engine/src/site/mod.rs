// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Restricted destination classes (threat model section 10.5, invariant I-18).
//!
//! Some destinations are dangerous to arrive at on a page's suggestion rather
//! than a person's. A mailbox, a bank, an administration console — each is
//! reached inside a live authenticated session, and the ordinary controls treat
//! a navigation there exactly like any other in-scope navigation. This module
//! is the table that tells them apart, and the one rule that uses it.
//!
//! # What this constrains, and what it does not
//!
//! It constrains **what Taffy may propose**. It has nothing to say about what a
//! person may browse, what the renderer observes, or what Chromium concludes
//! about a site. That is invariant I-01, and it holds here by construction
//! rather than by a flag: manual browsing never reaches this crate at all, and
//! [`assistant_navigation_verdict`] takes an [`ActionClass`] precisely so that
//! there is no signature under which it could be asked about a person's own
//! navigation.
//!
//! A destination the person's own task scope names is never refused. A research
//! task pointed at the user's own bank may go there; that is the difference
//! between a control and an obstruction.
//!
//! # Why the table is compiled in
//!
//! Decision 0015 states that catalog data can never change route semantics,
//! disclosure classes, or security policy. This table is security policy: a
//! remotely updatable one would let a compromised publisher remove an origin
//! and thereby widen what the assistant may do. Whether a strictly widening
//! overlay is ever acceptable is `[Open (OD-081)]`, along with the table's
//! membership, ownership, and review cadence.

mod table;

pub use crate::site::table::{
    classify_site, SiteRow, SiteTable, TableError, CORPUS_TABLE, SHIPPING_TABLE,
};

use crate::action_class::ActionClass;
use crate::denial::DenialReason;
use crate::origin::NormalizedOrigin;
use crate::risk::RiskClass;

/// What kind of destination an origin is.
///
/// Nine classes, each naming a place where arriving under someone else's
/// suggestion is materially worse than arriving deliberately.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SiteClass {
    /// Retail or business banking.
    Banking,
    /// Cryptocurrency exchange or wallet.
    Crypto,
    /// Webmail.
    Email,
    /// File storage and document suites.
    CloudStorage,
    /// A password manager or credential vault.
    PasswordManager,
    /// Government and public-sector services.
    Government,
    /// Health records and patient portals.
    Health,
    /// An administration console for an account, tenant, or fleet.
    AdminConsole,
    /// Payment processing and money movement.
    Payments,
}

impl SiteClass {
    /// Every class, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Banking,
        Self::Crypto,
        Self::Email,
        Self::CloudStorage,
        Self::PasswordManager,
        Self::Government,
        Self::Health,
        Self::AdminConsole,
        Self::Payments,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Banking => "banking",
            Self::Crypto => "crypto",
            Self::Email => "email",
            Self::CloudStorage => "cloud_storage",
            Self::PasswordManager => "password_manager",
            Self::Government => "government",
            Self::Health => "health",
            Self::AdminConsole => "admin_console",
            Self::Payments => "payments",
        }
    }

    /// Parses a class name from the table. `None` means the token is outside
    /// the closed list, which makes the row a parse failure rather than an
    /// unclassified origin.
    pub fn from_token(token: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|c| c.label() == token)
    }

    /// The floor an assistant-proposed action against this class sits at.
    ///
    /// Every class is at least a sensitive disclosure. That used to mean "which
    /// no ratified milestone authorizes", and decision 0089 ended it for the
    /// four classes sitting exactly there: at
    /// [`crate::action_class::PolicyMilestone::M5`] a sensitive disclosure is
    /// authorizable, so this floor no longer refuses an email, cloud-storage,
    /// health or government destination on its own.
    ///
    /// **What refuses them is [`assistant_navigation_verdict`], and it never
    /// consulted this floor.** It refuses by class, for any navigating action,
    /// unless the person's own task scope named the destination — which is
    /// decision 0089 section 5 holding: ratifying the write surface makes writes
    /// possible where Taffy is already allowed to be, and does not make Taffy
    /// allowed anywhere new. The floor stays as the second, independent reading
    /// it always was; it is now a weaker one, and saying so here is cheaper than
    /// a reader inferring a refusal from it that a milestone has taken away.
    pub const fn baseline_risk(self) -> RiskClass {
        match self {
            Self::Email | Self::CloudStorage | Self::Health | Self::Government => {
                RiskClass::SensitiveDisclosure
            }
            Self::Banking
            | Self::Crypto
            | Self::PasswordManager
            | Self::AdminConsole
            | Self::Payments => RiskClass::ExcludedCommitment,
        }
    }
}

/// Whether the assistant may propose navigating to `destination`.
///
/// `None` means this module has nothing to refuse it for — which is not the
/// same as calling the destination safe. Every other check still applies.
///
/// `scope_names_destination` comes from the task's own source scope. When the
/// person named the destination, the class does not refuse it: they asked.
#[must_use]
pub fn assistant_navigation_verdict(
    class: ActionClass,
    destination: &NormalizedOrigin,
    scope_names_destination: bool,
) -> Option<DenialReason> {
    if !class.navigates() {
        return None;
    }
    if scope_names_destination {
        return None;
    }
    classify_site(destination).map(|_| DenialReason::DestinationClassRestricted)
}

#[cfg(test)]
mod tests {
    use super::{assistant_navigation_verdict, classify_site, SiteClass};
    use crate::action_class::{ActionClass, PolicyMilestone};
    use crate::denial::DenialReason;
    use crate::origin::{normalize, NormalizedOrigin};
    use crate::risk::RiskClass;
    use bip_types::identity::{Origin, OriginKind};

    fn tuple(serialization: &str) -> NormalizedOrigin {
        normalize(&Origin {
            kind: OriginKind::Tuple,
            serialization: Some(serialization.to_owned()),
            opaque_id: None,
        })
        .expect("a tuple origin normalizes")
    }

    #[test]
    fn every_class_sits_above_what_the_read_oriented_surface_authorizes() {
        // Renamed from `every_class_sits_above_what_a_ratified_milestone_authorizes`,
        // because decision 0089 ratified a milestone that authorizes a
        // sensitive disclosure and four of the nine classes sit exactly there.
        // The claim, restated to the surface it is true of.
        for class in SiteClass::ALL {
            assert!(
                !class.baseline_risk().can_be_authorized_today(),
                "{} would be authorizable",
                class.label()
            );
            for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
                assert!(
                    !class.baseline_risk().can_be_authorized_at(milestone),
                    "{} would be authorizable at {}",
                    class.label(),
                    milestone.label()
                );
            }
        }
    }

    #[test]
    fn the_write_milestone_takes_the_risk_floor_away_and_the_class_still_refuses() {
        // The floor stopped refusing four of the nine at M5, and nothing about
        // the destination table moved (decision 0089 section 5). This states
        // both halves together so the weakened reading cannot be mistaken for a
        // control that is still doing the work.
        let softened: Vec<&str> = SiteClass::ALL
            .iter()
            .filter(|class| {
                class
                    .baseline_risk()
                    .can_be_authorized_at(PolicyMilestone::M5)
            })
            .map(|class| class.label())
            .collect();
        assert_eq!(
            softened,
            vec!["email", "cloud_storage", "government", "health"]
        );

        // And every one of them is still refused for a navigating class the
        // person's own scope did not name, which is the check that actually
        // holds invariant I-18.
        for class in SiteClass::ALL {
            assert!(class.baseline_risk() >= RiskClass::SensitiveDisclosure);
        }
        let mailbox = tuple("https://mail.google.com");
        assert_eq!(
            assistant_navigation_verdict(ActionClass::OpenLink, &mailbox, false),
            Some(DenialReason::DestinationClassRestricted)
        );
    }

    #[test]
    fn every_class_has_a_distinct_name_that_parses_back() {
        let mut labels: Vec<&str> = SiteClass::ALL.iter().map(|c| c.label()).collect();
        let count = labels.len();
        labels.sort_unstable();
        labels.dedup();
        assert_eq!(labels.len(), count);
        for class in SiteClass::ALL {
            assert_eq!(SiteClass::from_token(class.label()), Some(*class));
        }
        assert_eq!(SiteClass::from_token("not_a_class"), None);
    }

    #[test]
    fn a_destination_the_person_named_is_never_refused_by_a_class() {
        // A classified host, or the test would pass for the wrong reason.
        let mailbox = tuple("https://mail.google.com");
        assert!(classify_site(&mailbox).is_some());
        for class in ActionClass::ALL {
            assert_eq!(assistant_navigation_verdict(*class, &mailbox, true), None);
        }
    }

    #[test]
    fn an_unclassified_origin_is_not_refused_here_and_is_not_thereby_safe() {
        let ordinary = tuple("https://example.test");
        assert_eq!(classify_site(&ordinary), None);
        assert_eq!(
            assistant_navigation_verdict(ActionClass::OpenLink, &ordinary, false),
            None
        );
    }

    #[test]
    fn an_opaque_origin_has_no_host_and_is_left_to_the_other_checks() {
        let opaque = normalize(&Origin {
            kind: OriginKind::Opaque,
            serialization: None,
            opaque_id: Some("opaque_01".to_owned()),
        })
        .expect("an opaque origin normalizes");
        assert_eq!(classify_site(&opaque), None);
    }

    #[test]
    fn a_class_refuses_only_a_navigating_action_class() {
        let mailbox = tuple("https://mail.google.com");
        assert!(classify_site(&mailbox).is_some());
        for class in ActionClass::ALL {
            let verdict = assistant_navigation_verdict(*class, &mailbox, false);
            if class.navigates() {
                assert_eq!(
                    verdict,
                    Some(DenialReason::DestinationClassRestricted),
                    "{}",
                    class.label()
                );
            } else {
                assert_eq!(verdict, None, "{}", class.label());
            }
        }
    }
}
