// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Approvals and what invalidates one (domain model section 12.5, threat model
//! sections 8.2 and 18.4).
//!
//! An approval is a person's answer to one exact question. It is not a
//! preference, not a setting, and not a standing permission, so everything here
//! is built to make a reused answer useless the moment the question changes.
//!
//! # The binding is the question
//!
//! [`ApprovalBinding`] carries every fact the approval sheet showed: which
//! effect, in which half of its authorization, on which node, in which
//! document, at which origin, going where, over which data classes, at which
//! risk. Consuming an approval re-derives that
//! binding from the request being authorized and compares it field by field.
//! Anything different is a different question, and
//! [`ApprovalInvalidation`] names which part of it moved.
//!
//! The comparison is exact in both directions. A binding that looks *safer*
//! than the approved one invalidates the approval too: a lowered risk or a
//! narrowed data class is still not the thing the person was shown, and
//! accepting it would make the sheet's contents advisory.
//!
//! # An approval is spent once
//!
//! The default repeat scope is once (domain model section 12.5), and it is the
//! only scope any ratified milestone authorizes. The scoped alternative exists
//! as a value so refusing it is a table a test can walk rather than an absence
//! nobody notices.
//!
//! # No page text
//!
//! The summaries a person reads are composed from trusted local templates in
//! the browser user interface. Nothing in this module holds one, so a page or a
//! model cannot supply the sentence a person approves.

//! # What lives where
//!
//! | Module | Holds |
//! |---|---|
//! | this one | The identifier, the gesture receipt, the repeat scope, the answer, and the refusals |
//! | [`binding`] | What the sheet showed, and what makes a shown answer stop applying |
//! | [`book`] | The record of every question shown and every answer given |

pub mod binding;
pub mod book;

use core::fmt;

use bip_types::identity::ApprovalReceiptReference;

pub use crate::approval::binding::{ApprovalBinding, ApprovalInvalidation};
pub use crate::approval::book::{
    Approval, ApprovalBook, ApprovalRequest, MAX_APPROVALS_PER_SESSION,
};

/// Opaque approval identifier.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ApprovalId(String);

impl ApprovalId {
    /// Wraps an identifier minted by an identifier source.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value, for equality and audit correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// The protocol receipt reference for this approval.
    ///
    /// The reference points at a decision record the broker holds. It is not
    /// transferable authority and it is never sent to a renderer.
    pub fn to_receipt(&self) -> ApprovalReceiptReference {
        ApprovalReceiptReference(self.0.clone())
    }
}

impl fmt::Display for ApprovalId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Evidence that a person, rather than a page or a model, answered.
///
/// The trusted browser user interface mints one from a real input event. The
/// value is opaque here: this module checks that one is present and never
/// parses it.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct UserGestureReceipt(String);

impl UserGestureReceipt {
    /// Wraps a receipt minted by the trusted approval surface.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value, for audit correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// How often one answer may be used.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RepeatScope {
    /// One answer authorizes one action. The default, and the only scope any
    /// ratified milestone authorizes.
    Once,
    /// A declared pre-approved scope covering repeats of the same exact action
    /// until the approval expires. Reserved: the requirements and the
    /// threat-model extension for it are not ratified, so it is refused.
    TaskScopedUntilExpiry,
}

impl RepeatScope {
    /// Every scope, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Once, Self::TaskScopedUntilExpiry];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Once => "once",
            Self::TaskScopedUntilExpiry => "task_scoped_until_expiry",
        }
    }

    /// Whether a ratified milestone authorizes this scope.
    pub const fn is_authorized_today(self) -> bool {
        matches!(self, Self::Once)
    }
}

/// What a person did with the request (domain model section 12.5).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ApprovalDecision {
    /// Presented and not yet answered.
    Pending,
    /// Answered yes to this exact action.
    Approved,
    /// Answered no.
    Denied,
    /// Dismissed without answering.
    Dismissed,
    /// The request timed out.
    Expired,
    /// Something the sheet showed changed before the answer was used.
    Invalidated,
}

impl ApprovalDecision {
    /// Every decision, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Pending,
        Self::Approved,
        Self::Denied,
        Self::Dismissed,
        Self::Expired,
        Self::Invalidated,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Pending => "pending",
            Self::Approved => "approved",
            Self::Denied => "denied",
            Self::Dismissed => "dismissed",
            Self::Expired => "expired",
            Self::Invalidated => "invalidated",
        }
    }

    /// Whether this decision could authorize anything.
    pub const fn grants_authority(self) -> bool {
        matches!(self, Self::Approved)
    }

    /// Whether the answer is final.
    pub const fn is_final(self) -> bool {
        !matches!(self, Self::Pending)
    }
}

/// Why an approval could not be presented or used.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ApprovalError {
    /// No approval with that identifier exists.
    Unknown,
    /// The approval belongs to another task.
    TaskMismatch,
    /// The approval has not been answered yes.
    NotGranted(ApprovalDecision),
    /// The approval is past its expiry.
    Expired,
    /// The approval was already used. One answer authorizes one action.
    AlreadyUsed,
    /// Something the sheet showed changed.
    Invalidated(ApprovalInvalidation),
    /// No ratified milestone authorizes the requested repeat scope.
    RepeatScopeNotAuthorized,
    /// The answer carried no evidence that a person gave it.
    MissingUserGesture,
    /// The requested expiry is now or in the past.
    ExpiryNotInFuture,
    /// The approval has already been answered and is not answered twice.
    AlreadyDecided,
    /// The identifier source is exhausted.
    IdSourceExhausted,
    /// The book already holds [`MAX_APPROVALS_PER_SESSION`] answers and may
    /// not drop one to make room: a dropped answer is an answer that could be
    /// presented a second time.
    BookFull,
}

impl fmt::Display for ApprovalError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::BookFull => formatter.write_str("the approval book is full"),
            Self::Unknown => formatter.write_str("no such approval"),
            Self::TaskMismatch => formatter.write_str("the approval belongs to another task"),
            Self::NotGranted(decision) => {
                write!(formatter, "the approval was {}", decision.label())
            }
            Self::Expired => formatter.write_str("the approval expired"),
            Self::AlreadyUsed => formatter.write_str("the approval was already used"),
            Self::Invalidated(reason) => {
                write!(
                    formatter,
                    "the approval was invalidated: {}",
                    reason.label()
                )
            }
            Self::RepeatScopeNotAuthorized => {
                formatter.write_str("that repeat scope is not authorized")
            }
            Self::MissingUserGesture => formatter.write_str("no user gesture receipt"),
            Self::ExpiryNotInFuture => {
                formatter.write_str("an approval expiry must be in the future")
            }
            Self::AlreadyDecided => formatter.write_str("the approval was already answered"),
            Self::IdSourceExhausted => formatter.write_str("the identifier source is exhausted"),
        }
    }
}
