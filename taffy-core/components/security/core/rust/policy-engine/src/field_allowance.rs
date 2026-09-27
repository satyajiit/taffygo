// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What Taffy may write into a field, by the class of the field (decision
//! 0061; protocol specification section 9.2; roadmap milestone M5).
//!
//! [`crate::action_class`] answers whether a *class of effect* is on the
//! ratified surface. This module answers the other half of a fill: supposing
//! writing to a page were authorized at all, what does the class of *this
//! field* permit. They are two independent gates over one decision and a
//! proposal passes both or neither, exactly as the milestone surface and the
//! risk lattice already are.
//!
//! # Two properties, and only one of them is a table
//!
//! **Credential material is never fillable, and that is structural rather
//! than tabular.** [`FillClearance`] is the only value that says a class may
//! carry a value Taffy prepared, its fields are private, and
//! [`FieldClassAllowance::clearance_for`] is its only constructor — which
//! answers `None` for [`FieldClassAllowance::NeverFillable`]. So there is no
//! value of type [`FillClearance`] naming [`Sensitivity::Credential`], and
//! there is no way to write one. Two compile-time assertions at the foot of
//! this file hold the table to that: one pins the credential class, and one
//! walks [`NeverExtractClass::ALL`] so that a never-extract class which
//! stopped resolving to the credential classification fails the build rather
//! than acquiring an allowance. Passwords, passcodes, personal identification
//! numbers, card verification values, one-time codes, recovery codes,
//! passkeys, private keys, seed phrases, authentication tokens and session
//! cookies are all members of that list. They are a permanent product
//! prohibition, not a setting, and nothing here takes a parameter that could
//! relax one.
//!
//! **A field this build cannot classify is completed by the person.**
//! [`Sensitivity::UnknownSensitive`] resolves to
//! [`FieldClassAllowance::CompletedByThePerson`], which is *stricter* than the
//! identity answer. That is what makes the renderer's identity vocabulary a
//! convenience rather than a control: a national scheme nobody has heard of
//! reaches this table as unclassified, and the answer is that Taffy hands the
//! field to the person rather than filling it. An incomplete list therefore
//! costs a person one manual entry; it never costs them a disclosure.
//!
//! # What the write milestone did and did not open
//!
//! [`ActionClass::FillField`] is authorized from M5 only through the
//! browser-owned path that binds a visible exact-value confirmation to one
//! supplied position, live field, document, and expiry. The policy approval is
//! necessary but not sufficient: the browser spends that private binding
//! before this decision can reach an executor. **No row of the table moved.**
//! The table remains the independent field-class gate: payment, health,
//! financial, legal, private communication, administration and unclassified
//! stay at [`FieldClassAllowance::CompletedByThePerson`], and credential stays
//! at [`FieldClassAllowance::NeverFillable`] permanently.
//!
//! [`Sensitivity::Identity`] carries
//! [`FieldClassAllowance::PreparedCommitAndPerUseConfirmation`], and a clearance
//! is not a confirmation. [`per_use_confirmation_required`] is what the two
//! deciders ask so that a fill of such a field cannot be authorized without a
//! person having answered one exact question about it (decision 0089 section
//! 3).
//!
//! There is deliberately no [`crate::denial::DenialReason`] mapping here. A
//! denial reason carries a user-visible sentence and an `ActionResultCode` on
//! the contract; this table instead supplies the field-class half of the one
//! ordinary policy decision.

use bip_types::sensitivity::{NeverExtractClass, SensitivitySet};
use bip_types::snapshot::Sensitivity;

use crate::action_class::{ActionClass, PolicyMilestone};

/// What Taffy may do about a field of a given class.
///
/// Ordered from permissive to strict, so combining the classes a field drew is
/// a maximum and can never relax one of them.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum FieldClassAllowance {
    /// Taffy may prepare the value and complete it through the prepare and
    /// commit path of decision 0022. The person still sees the staged effect
    /// and still confirms the commit; what they do not do is retype the value.
    PreparedCommit,
    /// As [`Self::PreparedCommit`], and every individual use is confirmed
    /// again. An approval covering the task does not cover the next field of
    /// this class: the person is asked each time the value is used, because
    /// the identifier outlives the form it is typed into and cannot be
    /// reissued if it goes somewhere it should not have.
    PreparedCommitAndPerUseConfirmation,
    /// Taffy does not carry the value at all. It may bring the field into
    /// view and say what the field is; the person types it. This is both the
    /// standing answer for the classes with no design and the fail-safe answer
    /// for a field nothing classified.
    CompletedByThePerson,
    /// Taffy does not touch the control, and no value of this class enters the
    /// data plane a model can see. Permanent, and not reachable through any
    /// approval.
    NeverFillable,
}

impl FieldClassAllowance {
    /// Every allowance, least restrictive first.
    pub const ALL: &'static [Self] = &[
        Self::PreparedCommit,
        Self::PreparedCommitAndPerUseConfirmation,
        Self::CompletedByThePerson,
        Self::NeverFillable,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::PreparedCommit => "prepared_commit",
            Self::PreparedCommitAndPerUseConfirmation => "prepared_commit_and_per_use_confirmation",
            Self::CompletedByThePerson => "completed_by_the_person",
            Self::NeverFillable => "never_fillable",
        }
    }

    /// The allowance one classification carries.
    ///
    /// The match is exhaustive on purpose: a classification added to the
    /// protocol without an entry here does not compile, rather than falling
    /// through a catch-all into whichever answer happened to be first.
    pub const fn of(class: Sensitivity) -> Self {
        match class {
            // The ordinary form-assistance surface milestone M5 is about: the
            // fields a person would otherwise retype, none of which names them
            // to an authority.
            Sensitivity::NotSensitive | Sensitivity::Personal | Sensitivity::Account => {
                Self::PreparedCommit
            }
            // The single narrowing. A government identifier is the case the
            // ordinary answer is too loose for and the closed answer is too
            // tight for: a person filing a return or completing a know-your-
            // customer form types the same number into field after field, and
            // refusing outright makes Taffy useless for exactly the errand it
            // is most useful in. What it may not be is *quiet*, because the
            // number cannot be reissued.
            Sensitivity::Identity => Self::PreparedCommitAndPerUseConfirmation,
            // Short-lived values read and typed by the person. Decision 0088
            // permits the browser to hold each for one confirmed use while
            // extraction remains permanently closed.
            Sensitivity::OneTimeCode | Sensitivity::ChallengeResponse => {
                Self::PreparedCommitAndPerUseConfirmation
            }
            // Two different reasons reaching one answer, which is why they are
            // one arm rather than two: identical bodies in a security table are
            // a lint, and splitting them to satisfy prose would leave a reader
            // hunting for a difference that is not there.
            //
            // The first six are closed. Each is a commitment, a regulated
            // record, or somebody else's confidence, and none has a design;
            // decision 0022 gives them a mechanism, not an authorization.
            //
            // The seventh is the fail-safe, and it is deliberately stricter
            // than the identity answer rather than equal to it: an unclassified
            // field may be a national identifier this build has no token for,
            // and it may equally be something worse.
            Sensitivity::Payment
            | Sensitivity::Financial
            | Sensitivity::Health
            | Sensitivity::Legal
            | Sensitivity::PrivateCommunication
            | Sensitivity::Administration
            | Sensitivity::UnknownSensitive => Self::CompletedByThePerson,
            Sensitivity::Credential => Self::NeverFillable,
        }
    }

    /// The allowance a never-extract class carries, which is always
    /// [`Self::NeverFillable`].
    ///
    /// It is a function rather than a constant so that the class is named at
    /// the call site and so the assertion below can walk every member.
    pub const fn of_never_extract(class: NeverExtractClass) -> Self {
        Self::of(class.sensitivity())
    }

    /// The allowance a field carries once every classification that fired is
    /// counted.
    ///
    /// The strictest member wins, and an empty element takes the ordinary
    /// answer — which is the same rule
    /// [`SensitivitySet::handling_for`] applies, stated over this table so the
    /// two cannot disagree about a field that drew both identity and payment.
    pub fn of_set(set: SensitivitySet) -> Self {
        set.members()
            .into_iter()
            .fold(Self::of(Sensitivity::NotSensitive), |strictest, class| {
                if strictest >= Self::of(class) {
                    strictest
                } else {
                    Self::of(class)
                }
            })
    }

    /// Whether Taffy may hold a value of this class at all.
    pub const fn taffy_may_carry_the_value(self) -> bool {
        matches!(
            self,
            Self::PreparedCommit | Self::PreparedCommitAndPerUseConfirmation
        )
    }

    /// Whether each individual use needs its own confirmation.
    pub const fn requires_per_use_confirmation(self) -> bool {
        matches!(self, Self::PreparedCommitAndPerUseConfirmation)
    }

    /// The ceiling this class permits, when it permits anything.
    ///
    /// The only constructor of [`FillClearance`]. `None` is the answer for
    /// every class Taffy may not carry a value for, which is what makes a
    /// clearance over credential material unwritable rather than merely
    /// absent from a table.
    pub const fn clearance_for(class: Sensitivity) -> Option<FillClearance> {
        let allowance = Self::of(class);
        match allowance {
            Self::NeverFillable | Self::CompletedByThePerson => None,
            Self::PreparedCommit | Self::PreparedCommitAndPerUseConfirmation => {
                Some(FillClearance { class, allowance })
            }
        }
    }
}

/// Proof that a field class permits Taffy to prepare a value for it.
///
/// Carries the class it cleared, so a clearance drawn for one field cannot be
/// presented for a field of another class. Constructed only by
/// [`FieldClassAllowance::clearance_for`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct FillClearance {
    class: Sensitivity,
    allowance: FieldClassAllowance,
}

impl FillClearance {
    /// The classification this clearance was drawn for.
    pub const fn class(self) -> Sensitivity {
        self.class
    }

    /// The allowance that class carries.
    pub const fn allowance(self) -> FieldClassAllowance {
        self.allowance
    }

    /// Whether each individual use needs its own confirmation.
    pub const fn requires_per_use_confirmation(self) -> bool {
        self.allowance.requires_per_use_confirmation()
    }
}

/// Why a fill was refused.
///
/// The class reasons are permanent for as long as the table says so; the
/// milestone reason is the one that a ratified write milestone would remove.
/// They are reported in that order, so a refusal does not appear to soften
/// when a milestone widens.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FillRefusal {
    /// The class is never fillable, whatever any milestone or person says.
    ClassNeverFillable,
    /// The class is one only the person completes.
    ClassMustBeCompletedByThePerson,
    /// This milestone does not authorize writing to a field at all. True of
    /// M2 and M3; M5 and later authorize only the browser-bound fill path.
    WriteNotAuthorizedAtThisMilestone,
}

impl FillRefusal {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::ClassNeverFillable,
        Self::ClassMustBeCompletedByThePerson,
        Self::WriteNotAuthorizedAtThisMilestone,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ClassNeverFillable => "class_never_fillable",
            Self::ClassMustBeCompletedByThePerson => "class_must_be_completed_by_the_person",
            Self::WriteNotAuthorizedAtThisMilestone => "write_not_authorized_at_this_milestone",
        }
    }

    /// Whether a ratified write milestone would remove this refusal.
    pub const fn a_later_milestone_could_lift_this(self) -> bool {
        matches!(self, Self::WriteNotAuthorizedAtThisMilestone)
    }
}

/// Whether Taffy may fill a field of this class at this milestone.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FillVerdict {
    /// Refused, and why.
    Refused(FillRefusal),
    /// The class permits it and the milestone authorizes writing.
    Cleared(FillClearance),
}

impl FillVerdict {
    /// Whether this verdict permits the fill.
    pub const fn is_cleared(self) -> bool {
        matches!(self, Self::Cleared(_))
    }
}

/// The whole decision: the class ceiling, then the milestone gate.
///
/// The class is asked first so that a permanent refusal is reported as one. A
/// credential field is refused with [`FillRefusal::ClassNeverFillable`] at
/// every milestone, ratified or not, rather than with a reason that would read
/// as temporary.
pub fn fill_verdict(class: Sensitivity, milestone: PolicyMilestone) -> FillVerdict {
    match FieldClassAllowance::of(class) {
        FieldClassAllowance::NeverFillable => {
            return FillVerdict::Refused(FillRefusal::ClassNeverFillable)
        }
        FieldClassAllowance::CompletedByThePerson => {
            return FillVerdict::Refused(FillRefusal::ClassMustBeCompletedByThePerson)
        }
        FieldClassAllowance::PreparedCommit
        | FieldClassAllowance::PreparedCommitAndPerUseConfirmation => {}
    }

    // Unreachable while the two agree, and a refusal rather than an assertion
    // if they ever part: this function stays total, and it parts in the safe
    // direction.
    let Some(clearance) = FieldClassAllowance::clearance_for(class) else {
        return FillVerdict::Refused(FillRefusal::ClassNeverFillable);
    };

    // The second gate. It is
    // consulted through `action_class` rather than restated, so this table can
    // never be the place a write surface is quietly opened.
    if ActionClass::FillField.is_authorized_at(milestone) {
        FillVerdict::Cleared(clearance)
    } else {
        FillVerdict::Refused(FillRefusal::WriteNotAuthorizedAtThisMilestone)
    }
}

/// Whether a proposal of `class` disclosing `data_classes` may only be
/// authorized once a person has confirmed this particular use (decision 0089
/// section 3).
///
/// The property this exists to hold: **a surface that forgets to ask cannot
/// make the fill happen.** The absence of a question is the absence of an
/// approval reference, and that is a denial rather than a default. Were it the
/// other way round, a screen that never drew the sheet would fill national
/// identifiers silently and pass every test the policy engine has, because the
/// policy engine would have said yes.
///
/// Two details are deliberate.
///
/// **It is stated over the field class, not read off the risk beside it.** A
/// fill's baseline risk is [`crate::risk::RiskClass::SensitiveDisclosure`],
/// which already requires an exact approval, so today the two agree. They are
/// separate claims all the same: a risk table that stopped calling some fill a
/// sensitive disclosure would take this question away with it, silently, and
/// the class of field would never have been consulted.
///
/// **It reads the joined allowance as an order rather than as an equality.**
/// [`FieldClassAllowance::of_set`] takes the strictest member, so a field that
/// drew identity *and* payment answers
/// [`FieldClassAllowance::CompletedByThePerson`] — stricter than identity, and
/// not equal to the per-use value. Asking
/// `requires_per_use_confirmation()` of that answer would say "no question
/// needed" about the stricter case, which is the wrong direction to be wrong
/// in. Everything at or above
/// [`FieldClassAllowance::PreparedCommitAndPerUseConfirmation`] is asked about.
pub fn per_use_confirmation_required(class: ActionClass, data_classes: SensitivitySet) -> bool {
    class == ActionClass::FillField
        && FieldClassAllowance::of_set(data_classes)
            >= FieldClassAllowance::PreparedCommitAndPerUseConfirmation
}

/// Whether every never-extract class except the two person-supplied per-use
/// classes still resolves to a never-fillable allowance.
///
/// Walks the slice without indexing so the workspace's `indexing_slicing`
/// denial holds in a const context too.
const fn permanent_never_extract_classes_are_never_fillable(
    mut rest: &[NeverExtractClass],
) -> bool {
    while let Some((first, tail)) = rest.split_first() {
        let expected = match first {
            NeverExtractClass::OneTimeCode | NeverExtractClass::ChallengeResponse => {
                FieldClassAllowance::PreparedCommitAndPerUseConfirmation
            }
            NeverExtractClass::Password
            | NeverExtractClass::Passcode
            | NeverExtractClass::PersonalIdentificationNumber
            | NeverExtractClass::CardVerificationValue
            | NeverExtractClass::RecoveryCode
            | NeverExtractClass::Passkey
            | NeverExtractClass::PrivateKey
            | NeverExtractClass::SeedPhrase
            | NeverExtractClass::AuthenticationToken
            | NeverExtractClass::SessionCookie
            | NeverExtractClass::PasswordManagerSuggestion
            | NeverExtractClass::PlatformCredentialPayload => FieldClassAllowance::NeverFillable,
        };
        if !matches!(
            (FieldClassAllowance::of_never_extract(*first), expected),
            (
                FieldClassAllowance::PreparedCommitAndPerUseConfirmation,
                FieldClassAllowance::PreparedCommitAndPerUseConfirmation
            ) | (
                FieldClassAllowance::NeverFillable,
                FieldClassAllowance::NeverFillable
            )
        ) {
            return false;
        }
        rest = tail;
    }
    true
}

// The build fails here if the credential row is edited, or if a never-extract
// class stops carrying the credential classification. Both are the same
// mistake seen from two sides, and neither is a thing a test should be the
// first to notice.
const _: () = assert!(matches!(
    FieldClassAllowance::of(Sensitivity::Credential),
    FieldClassAllowance::NeverFillable
));
const _: () = assert!(permanent_never_extract_classes_are_never_fillable(
    NeverExtractClass::ALL
));
const _: () = assert!(FieldClassAllowance::clearance_for(Sensitivity::Credential).is_none());

#[cfg(test)]
mod tests;
