// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Why a proposal was refused, in terms a trusted local template can render.
//!
//! Every variant names a decision the broker made. None of them carries page
//! text, model text, or a renderer string, because the sentence shown to a
//! person is composed from these names and nothing else (threat model section
//! 11).
//!
//! # The denial table is the release surface, written down
//!
//! [`DenialReason::for_class`] is a total function over class and milestone. It
//! answers `None` exactly when the ratified surface authorizes the class, and
//! otherwise names which boundary refused it: not yet authorized, reserved for
//! the write milestone, outside this release, or permanently prohibited. A test
//! walks every class times every milestone, so the release's action surface is
//! an enumerable table rather than a claim.
//!
//! Adding a class to [`ActionClass`] without deciding its availability does not
//! compile, and this function then reports the refusal that availability
//! implies. There is no default arm that could quietly let a new class through.

use bip_types::ActionResultCode;

use crate::action_class::{ActionClass, ClassAvailability, PolicyMilestone};
use crate::approval::{ApprovalError, ApprovalInvalidation};
use crate::prepared::CommitRefusal;
use crate::risk::RiskClass;

/// Why a proposal was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DenialReason {
    /// The action class is not on the ratified surface for this milestone.
    ActionClassNotAuthorized,
    /// The action class is reserved for a later milestone.
    ActionClassReservedForWriteMilestone,
    /// The action class is outside this release until a separate decision.
    ActionClassExcludedFromRelease,
    /// The action class is permanently prohibited.
    ActionClassProhibited,
    /// The effective risk is above what any ratified milestone authorizes.
    EffectiveRiskNotAuthorized,
    /// The typed task/direct context and its subject do not describe one
    /// permitted authority family.
    AuthorityContextInvalid,
    /// The control mode holds no actor lease.
    ControlModeGrantsNoLease,
    /// There is no standing lease for the tab.
    NoStandingLease,
    /// The lease covers a different tab or a different task.
    LeaseDoesNotCoverTarget,
    /// The requested expiry is not usable.
    ExpiryNotUsable,
    /// A node-targeted class arrived without a node.
    NodeTargetMissing,
    /// The approval named by the proposal does not exist or belongs elsewhere.
    ApprovalNotFound,
    /// The approval was denied, dismissed, expired, or never answered.
    ApprovalNotGranted,
    /// The approval was already used. One answer authorizes one action.
    ApprovalAlreadyUsed,
    /// Something the approval sheet showed changed before the answer was used.
    ApprovalInvalidated,
    /// The approval's repeat scope is not one a ratified milestone authorizes.
    ApprovalRepeatScopeNotAuthorized,
    /// The approval carried no evidence that a person gave it.
    ApprovalGestureMissing,
    /// The identifier source is exhausted.
    IdSourceExhausted,
    /// A register the broker keeps for the session — the capability ledger or
    /// the approval book — is full. Both are session-lifetime records that
    /// exist to refuse a replay, so neither may drop an entry to make room.
    AuthorityRegisterFull,
    /// The outbound destination is outside the task's authorized scope.
    EgressDestinationOutOfScope,
    /// The outbound unit carries a value observed on a page.
    EgressCarriesObservedValue,
    /// The outbound unit carries free text nobody trusted wrote.
    EgressCarriesUntrustedContent,
    /// Something in the unit has no nameable author.
    EgressTrustNotAuthorized,
    /// The outbound unit would disclose a never-extract value.
    EgressWouldDiscloseCredential,
    /// The destination origin is in a class the assistant does not enter on its
    /// own initiative.
    DestinationClassRestricted,
    /// The effect being committed is not the effect that was prepared.
    PreparedEffectChanged,
    /// A commit names no prepare, or names one this task never completed.
    CommitWithoutPrepare,
    /// The gesture authorizing a commit does not postdate the prepare it
    /// claims to answer, so it cannot have been a response to it.
    CommitGestureDoesNotFollowPrepare,
    /// The preparation expired before the commit arrived. A prepared effect is
    /// refused past its expiry, never renewed.
    PreparedEffectExpired,
    /// The preparation was already committed. One prepare, one commit.
    PreparedEffectAlreadyCommitted,
}

impl DenialReason {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::ActionClassNotAuthorized,
        Self::ActionClassReservedForWriteMilestone,
        Self::ActionClassExcludedFromRelease,
        Self::ActionClassProhibited,
        Self::EffectiveRiskNotAuthorized,
        Self::AuthorityContextInvalid,
        Self::ControlModeGrantsNoLease,
        Self::NoStandingLease,
        Self::LeaseDoesNotCoverTarget,
        Self::ExpiryNotUsable,
        Self::NodeTargetMissing,
        Self::ApprovalNotFound,
        Self::ApprovalNotGranted,
        Self::ApprovalAlreadyUsed,
        Self::ApprovalInvalidated,
        Self::ApprovalRepeatScopeNotAuthorized,
        Self::ApprovalGestureMissing,
        Self::IdSourceExhausted,
        Self::AuthorityRegisterFull,
        Self::EgressDestinationOutOfScope,
        Self::EgressCarriesObservedValue,
        Self::EgressCarriesUntrustedContent,
        Self::EgressTrustNotAuthorized,
        Self::EgressWouldDiscloseCredential,
        Self::DestinationClassRestricted,
        Self::PreparedEffectChanged,
        Self::CommitWithoutPrepare,
        Self::CommitGestureDoesNotFollowPrepare,
        Self::PreparedEffectExpired,
        Self::PreparedEffectAlreadyCommitted,
    ];

    /// The sentence shown to a person, composed from a trusted local template.
    pub const fn user_visible_reason(self) -> &'static str {
        match self {
            Self::ActionClassNotAuthorized => "Taffy is not allowed to do that yet.",
            Self::ActionClassReservedForWriteMilestone => {
                "Taffy cannot change anything on a page yet."
            }
            Self::ActionClassExcludedFromRelease => "Taffy never does that on your behalf.",
            Self::ActionClassProhibited => "Taffy never does that.",
            Self::EffectiveRiskNotAuthorized => "That would go further than Taffy is allowed to.",
            Self::AuthorityContextInvalid => "That request does not belong to this page.",
            Self::ControlModeGrantsNoLease => "You are browsing, so Taffy will only suggest.",
            Self::NoStandingLease => "Taffy is not the one browsing this tab.",
            Self::LeaseDoesNotCoverTarget => "That tab is not part of this task.",
            Self::ExpiryNotUsable => "That request has already expired.",
            Self::NodeTargetMissing => "Taffy lost track of what to act on.",
            Self::ApprovalNotFound | Self::ApprovalGestureMissing => {
                "Taffy needs you to confirm that first."
            }
            Self::ApprovalNotGranted => "You did not say yes to that.",
            Self::ApprovalAlreadyUsed => "You already used that confirmation.",
            Self::ApprovalInvalidated => "The page changed, so Taffy will ask again.",
            Self::ApprovalRepeatScopeNotAuthorized => "Taffy asks every time.",
            Self::IdSourceExhausted | Self::AuthorityRegisterFull => {
                "Taffy could not start that safely."
            }
            Self::EgressDestinationOutOfScope => "That is outside what you asked Taffy to look at.",
            Self::EgressCarriesObservedValue => {
                "Taffy will not put something from the page into that link."
            }
            Self::EgressCarriesUntrustedContent => "Taffy will not send page text there.",
            Self::EgressTrustNotAuthorized => "Taffy cannot tell where that came from.",
            Self::EgressWouldDiscloseCredential => "Taffy never sends sign-in details anywhere.",
            Self::DestinationClassRestricted => "Taffy will not go there on its own.",
            // One sentence for all five. Which of them refused is in the audit
            // record; telling them apart on screen would tell a page which of
            // its attempts came closest — including how close its timing was,
            // which is why an expiry does not get a sentence of its own either.
            Self::PreparedEffectChanged
            | Self::CommitWithoutPrepare
            | Self::CommitGestureDoesNotFollowPrepare
            | Self::PreparedEffectExpired
            | Self::PreparedEffectAlreadyCommitted => {
                "That is not what you confirmed, so Taffy stopped."
            }
        }
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ActionClassNotAuthorized => "action_class_not_authorized",
            Self::ActionClassReservedForWriteMilestone => "action_class_reserved_write_milestone",
            Self::ActionClassExcludedFromRelease => "action_class_excluded_from_release",
            Self::ActionClassProhibited => "action_class_prohibited",
            Self::EffectiveRiskNotAuthorized => "effective_risk_not_authorized",
            Self::AuthorityContextInvalid => "authority_context_invalid",
            Self::ControlModeGrantsNoLease => "control_mode_grants_no_lease",
            Self::NoStandingLease => "no_standing_lease",
            Self::LeaseDoesNotCoverTarget => "lease_does_not_cover_target",
            Self::ExpiryNotUsable => "expiry_not_usable",
            Self::NodeTargetMissing => "node_target_missing",
            Self::ApprovalNotFound => "approval_not_found",
            Self::ApprovalNotGranted => "approval_not_granted",
            Self::ApprovalAlreadyUsed => "approval_already_used",
            Self::ApprovalInvalidated => "approval_invalidated",
            Self::ApprovalRepeatScopeNotAuthorized => "approval_repeat_scope_not_authorized",
            Self::ApprovalGestureMissing => "approval_gesture_missing",
            Self::IdSourceExhausted => "id_source_exhausted",
            Self::AuthorityRegisterFull => "authority_register_full",
            Self::EgressDestinationOutOfScope => "egress_destination_out_of_scope",
            Self::EgressCarriesObservedValue => "egress_carries_observed_value",
            Self::EgressCarriesUntrustedContent => "egress_carries_untrusted_content",
            Self::EgressTrustNotAuthorized => "egress_trust_not_authorized",
            Self::EgressWouldDiscloseCredential => "egress_would_disclose_credential",
            Self::DestinationClassRestricted => "destination_class_restricted",
            Self::PreparedEffectChanged => "prepared_effect_changed",
            Self::CommitWithoutPrepare => "commit_without_prepare",
            Self::CommitGestureDoesNotFollowPrepare => "commit_gesture_does_not_follow_prepare",
            Self::PreparedEffectExpired => "prepared_effect_expired",
            Self::PreparedEffectAlreadyCommitted => "prepared_effect_already_committed",
        }
    }

    /// The protocol result code this refusal ends the action with.
    pub const fn result_code(self) -> ActionResultCode {
        match self {
            Self::NoStandingLease | Self::ControlModeGrantsNoLease => {
                ActionResultCode::ActorLeaseMissing
            }
            Self::ExpiryNotUsable => ActionResultCode::CapabilityExpired,
            Self::ApprovalNotGranted => ActionResultCode::ApprovalDenied,
            Self::ApprovalNotFound
            | Self::ApprovalGestureMissing
            | Self::ApprovalAlreadyUsed
            | Self::ApprovalInvalidated
            | Self::ApprovalRepeatScopeNotAuthorized => ActionResultCode::ApprovalRequired,
            Self::ActionClassNotAuthorized
            | Self::ActionClassReservedForWriteMilestone
            | Self::ActionClassExcludedFromRelease
            | Self::ActionClassProhibited
            | Self::EffectiveRiskNotAuthorized
            | Self::AuthorityContextInvalid
            | Self::LeaseDoesNotCoverTarget
            | Self::NodeTargetMissing
            | Self::IdSourceExhausted
            | Self::AuthorityRegisterFull => ActionResultCode::DeniedByPolicy,
            Self::EgressDestinationOutOfScope
            | Self::EgressCarriesObservedValue
            | Self::EgressWouldDiscloseCredential => ActionResultCode::EgressNotAuthorized,
            Self::EgressCarriesUntrustedContent | Self::EgressTrustNotAuthorized => {
                ActionResultCode::UntrustedContentOrigin
            }
            Self::DestinationClassRestricted => ActionResultCode::DestinationClassRestricted,
            Self::PreparedEffectChanged => ActionResultCode::PreparedEffectChanged,
            // A gesture that does not follow the prepare is, as far as the
            // protocol is concerned, a commit with no prepare behind it: there
            // is no act of confirmation it could be answering.
            //
            // An expired preparation and one already committed report the same
            // code for the same reason: at the moment the commit arrives there
            // is no prepare standing behind it, and the difference between "it
            // ran out" and "it was already spent" is a decision fact for the
            // audit record rather than a code a page can observe.
            Self::CommitWithoutPrepare
            | Self::CommitGestureDoesNotFollowPrepare
            | Self::PreparedEffectExpired
            | Self::PreparedEffectAlreadyCommitted => ActionResultCode::CommitWithoutPrepare,
        }
    }

    /// Why `milestone` refuses `class`, or `None` when it authorizes it.
    ///
    /// The class half of the surface as a function: every excluded class
    /// answers [`Self::ActionClassExcludedFromRelease`] and the two prohibited
    /// classes answer [`Self::ActionClassProhibited`] whatever else is true of
    /// the request, at every milestone.
    ///
    /// The write classes are the half that moved. Before decision 0089 every
    /// one of them answered [`Self::ActionClassReservedForWriteMilestone`]
    /// everywhere; at [`PolicyMilestone::M5`] the five that milestone took
    /// answer `None`, and [`ActionClass::UploadFile`] — reserved by M5 and
    /// named by no allowlist — still answers the reservation. The allowlist is
    /// asked first for exactly that reason: availability alone cannot tell the
    /// six apart, and there is deliberately no reason meaning "reserved but
    /// authorized", because `None` already means authorized.
    pub fn for_class(class: ActionClass, milestone: PolicyMilestone) -> Option<Self> {
        if class.is_authorized_at(milestone) {
            return None;
        }
        Some(match class.availability() {
            ClassAvailability::WriteMilestone => Self::ActionClassReservedForWriteMilestone,
            ClassAvailability::ExcludedFromRelease => Self::ActionClassExcludedFromRelease,
            ClassAvailability::Prohibited => Self::ActionClassProhibited,
            ClassAvailability::ReadOriented
            | ClassAvailability::LibraryMilestone
            | ClassAvailability::MemoryMilestone
            | ClassAvailability::ComputationMilestone
            | ClassAvailability::NotYetAuthorized => Self::ActionClassNotAuthorized,
        })
    }

    /// Why an effective risk is refused at `milestone`, or `None` when that
    /// milestone authorizes it.
    ///
    /// The second, independent gate over the same decision. A class on the
    /// ratified surface whose context raised its risk is still refused.
    ///
    /// It takes the milestone because it has to. Both deciders run this gate
    /// before they reach the approval branch, and a fill's baseline risk is
    /// [`RiskClass::SensitiveDisclosure`], so a milestone-blind reading here
    /// would have refused every write M5 ratified — changing the *reason* a
    /// fill is refused and nothing else. The widening belongs to
    /// [`RiskClass::can_be_authorized_at`], which leaves
    /// [`RiskClass::requires_exact_approval`] and the "effective risk only
    /// rises" lattice exactly where they were.
    pub const fn for_risk(risk: RiskClass, milestone: PolicyMilestone) -> Option<Self> {
        if risk.can_be_authorized_at(milestone) {
            None
        } else {
            Some(Self::EffectiveRiskNotAuthorized)
        }
    }

    /// The reason a refused commit is reported as (decision 0022).
    ///
    /// Total over [`CommitRefusal`], so a comparison added to the ledger has to
    /// decide what a person is told rather than inheriting whatever the nearest
    /// arm said.
    pub const fn for_commit(refusal: CommitRefusal) -> Self {
        match refusal {
            // Every one of these is a commit with no prepare standing behind
            // it, whatever the record says about why.
            CommitRefusal::NoSuchPrepare
            | CommitRefusal::OtherTask
            | CommitRefusal::OtherLease
            | CommitRefusal::PrepareDidNotRun
            | CommitRefusal::Discarded => Self::CommitWithoutPrepare,
            CommitRefusal::AlreadyCommitted => Self::PreparedEffectAlreadyCommitted,
            CommitRefusal::Expired => Self::PreparedEffectExpired,
            CommitRefusal::EffectDigestChanged
            | CommitRefusal::ScopeChanged
            | CommitRefusal::ActionClassChanged
            | CommitRefusal::PrepareTimeChanged => Self::PreparedEffectChanged,
            CommitRefusal::GestureDoesNotFollowPrepare => Self::CommitGestureDoesNotFollowPrepare,
        }
    }

    /// The reason an approval failure is reported as.
    pub const fn for_approval(error: ApprovalError) -> Self {
        match error {
            ApprovalError::Unknown | ApprovalError::TaskMismatch => Self::ApprovalNotFound,
            ApprovalError::NotGranted(_) | ApprovalError::Expired => Self::ApprovalNotGranted,
            ApprovalError::AlreadyUsed => Self::ApprovalAlreadyUsed,
            ApprovalError::Invalidated(_) => Self::ApprovalInvalidated,
            ApprovalError::RepeatScopeNotAuthorized => Self::ApprovalRepeatScopeNotAuthorized,
            ApprovalError::MissingUserGesture => Self::ApprovalGestureMissing,
            ApprovalError::ExpiryNotInFuture | ApprovalError::AlreadyDecided => {
                Self::ApprovalNotGranted
            }
            ApprovalError::IdSourceExhausted => Self::IdSourceExhausted,
            ApprovalError::BookFull => Self::AuthorityRegisterFull,
        }
    }
}

/// A refusal, with everything an audit record and an approval surface need.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Denial {
    /// Why the proposal was refused.
    pub reason: DenialReason,
    /// The result code the action ends with.
    pub code: ActionResultCode,
    /// Which part of an approval moved, when an approval was what refused it.
    pub approval_invalidation: Option<ApprovalInvalidation>,
}

impl Denial {
    /// Builds a refusal whose code follows from its reason.
    pub const fn new(reason: DenialReason) -> Self {
        Self {
            reason,
            code: reason.result_code(),
            approval_invalidation: None,
        }
    }

    /// Builds the refusal an approval failure produces.
    pub const fn from_approval(error: ApprovalError) -> Self {
        let reason = DenialReason::for_approval(error);
        Self {
            reason,
            code: reason.result_code(),
            approval_invalidation: match error {
                ApprovalError::Invalidated(invalidation) => Some(invalidation),
                _ => None,
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{Denial, DenialReason};
    use crate::action_class::{ActionClass, ClassAvailability, PolicyMilestone};
    use crate::approval::{ApprovalDecision, ApprovalError, ApprovalInvalidation};
    use crate::risk::RiskClass;
    use bip_types::ActionResultCode;

    #[test]
    fn every_write_milestone_class_is_denied_with_the_reason_that_names_it_until_it_is_taken() {
        for class in ActionClass::WRITE_MILESTONE {
            for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
                assert_eq!(
                    DenialReason::for_class(*class, milestone),
                    Some(DenialReason::ActionClassReservedForWriteMilestone),
                    "{} at {}",
                    class.label(),
                    milestone.label()
                );
            }
        }
        // At M5 the browser-owned fill and download flows leave the reservation.
        for class in ActionClass::WRITE_MILESTONE {
            let expected = if matches!(*class, ActionClass::FillField | ActionClass::StartDownload)
            {
                None
            } else {
                Some(DenialReason::ActionClassReservedForWriteMilestone)
            };
            assert_eq!(
                DenialReason::for_class(*class, PolicyMilestone::M5),
                expected,
                "{} at M5",
                class.label()
            );
        }
    }

    #[test]
    fn the_denial_table_is_total_over_class_and_milestone() {
        for class in ActionClass::ALL {
            for milestone in PolicyMilestone::ALL {
                let reason = DenialReason::for_class(*class, *milestone);
                match class.availability() {
                    ClassAvailability::ReadOriented => assert_eq!(reason, None),
                    ClassAvailability::NotYetAuthorized => {
                        assert_eq!(reason, Some(DenialReason::ActionClassNotAuthorized));
                    }
                    // Availability says which milestone reserved a class; the
                    // allowlist says whether it was taken. At M5 field fill
                    // and download initiation were taken. Select, toggle,
                    // submit, and upload keep the reservation as their denial.
                    ClassAvailability::WriteMilestone => match milestone {
                        PolicyMilestone::M2 | PolicyMilestone::M3 => assert_eq!(
                            reason,
                            Some(DenialReason::ActionClassReservedForWriteMilestone),
                            "{} at {}",
                            class.label(),
                            milestone.label()
                        ),
                        PolicyMilestone::M5 | PolicyMilestone::M6 | PolicyMilestone::M7 => {
                            let expected = if matches!(
                                *class,
                                ActionClass::FillField | ActionClass::StartDownload
                            ) {
                                None
                            } else {
                                Some(DenialReason::ActionClassReservedForWriteMilestone)
                            };
                            assert_eq!(reason, expected, "{} at M5", class.label());
                        }
                    },
                    ClassAvailability::LibraryMilestone | ClassAvailability::MemoryMilestone => {
                        match milestone {
                            PolicyMilestone::M2 | PolicyMilestone::M3 | PolicyMilestone::M5 => {
                                assert_eq!(
                                    reason,
                                    Some(DenialReason::ActionClassNotAuthorized),
                                    "{} at {}",
                                    class.label(),
                                    milestone.label()
                                );
                            }
                            PolicyMilestone::M6 | PolicyMilestone::M7 => {
                                assert_eq!(reason, None, "{} at M6", class.label());
                            }
                        }
                    }
                    ClassAvailability::ComputationMilestone => {
                        let expected = if *milestone == PolicyMilestone::M7 {
                            None
                        } else {
                            Some(DenialReason::ActionClassNotAuthorized)
                        };
                        assert_eq!(
                            reason,
                            expected,
                            "{} at {}",
                            class.label(),
                            milestone.label()
                        );
                    }
                    ClassAvailability::ExcludedFromRelease => {
                        assert_eq!(reason, Some(DenialReason::ActionClassExcludedFromRelease));
                    }
                    ClassAvailability::Prohibited => {
                        assert_eq!(reason, Some(DenialReason::ActionClassProhibited));
                    }
                }
            }
        }
    }

    #[test]
    fn risk_refuses_independently_of_the_class_surface() {
        for milestone in PolicyMilestone::ALL {
            assert_eq!(
                DenialReason::for_risk(RiskClass::LocalRead, *milestone),
                None
            );
            assert_eq!(
                DenialReason::for_risk(RiskClass::ReversibleDisclosure, *milestone),
                None
            );
            // Nothing above a sensitive disclosure is authorized by any
            // ratified milestone.
            for risk in [RiskClass::ExcludedCommitment, RiskClass::ProhibitedAbuse] {
                assert_eq!(
                    DenialReason::for_risk(risk, *milestone),
                    Some(DenialReason::EffectiveRiskNotAuthorized),
                    "{} at {}",
                    risk.label(),
                    milestone.label()
                );
            }
        }
        // The one row the write milestone moved, stated from both sides.
        for milestone in [PolicyMilestone::M2, PolicyMilestone::M3] {
            assert_eq!(
                DenialReason::for_risk(RiskClass::SensitiveDisclosure, milestone),
                Some(DenialReason::EffectiveRiskNotAuthorized),
                "{}",
                milestone.label()
            );
        }
        assert_eq!(
            DenialReason::for_risk(RiskClass::SensitiveDisclosure, PolicyMilestone::M5),
            None
        );
        assert_eq!(
            DenialReason::for_risk(RiskClass::SensitiveDisclosure, PolicyMilestone::M6),
            None
        );
    }

    #[test]
    fn an_invalidated_approval_records_which_part_of_it_moved() {
        let denial = Denial::from_approval(ApprovalError::Invalidated(
            ApprovalInvalidation::DestinationChanged,
        ));
        assert_eq!(denial.reason, DenialReason::ApprovalInvalidated);
        assert_eq!(denial.code, ActionResultCode::ApprovalRequired);
        assert_eq!(
            denial.approval_invalidation,
            Some(ApprovalInvalidation::DestinationChanged)
        );

        let denied = Denial::from_approval(ApprovalError::NotGranted(ApprovalDecision::Denied));
        assert_eq!(denied.code, ActionResultCode::ApprovalDenied);
        assert_eq!(denied.approval_invalidation, None);
    }

    #[test]
    fn every_commit_refusal_reports_a_prepared_effect_reason_that_fails_closed() {
        use crate::prepared::CommitRefusal;

        for refusal in CommitRefusal::ALL {
            let reason = DenialReason::for_commit(*refusal);
            assert!(
                matches!(
                    reason,
                    DenialReason::CommitWithoutPrepare
                        | DenialReason::CommitGestureDoesNotFollowPrepare
                        | DenialReason::PreparedEffectChanged
                        | DenialReason::PreparedEffectExpired
                        | DenialReason::PreparedEffectAlreadyCommitted
                ),
                "{} reported {}",
                refusal.label(),
                reason.label()
            );
            assert!(reason.result_code().fails_closed(), "{}", refusal.label());
            assert_eq!(refusal.result_code(), reason.result_code());
        }
        // The two protocol codes the taxonomy reserves for this mechanism are
        // both reachable, so neither is a value nothing produces.
        assert_eq!(
            DenialReason::for_commit(CommitRefusal::EffectDigestChanged).result_code(),
            ActionResultCode::PreparedEffectChanged
        );
        assert_eq!(
            DenialReason::for_commit(CommitRefusal::NoSuchPrepare).result_code(),
            ActionResultCode::CommitWithoutPrepare
        );
    }

    #[test]
    fn every_reason_has_a_distinct_name_and_a_sentence_of_its_own() {
        let mut labels: Vec<&str> = DenialReason::ALL
            .iter()
            .map(|reason| reason.label())
            .collect();
        let count = labels.len();
        labels.sort_unstable();
        labels.dedup();
        assert_eq!(labels.len(), count);
        for reason in DenialReason::ALL {
            assert!(!reason.user_visible_reason().is_empty());
            assert!(reason.result_code().fails_closed());
        }
    }
}
