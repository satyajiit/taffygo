// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one dispatch is allowed to commit, resolved before the section 12
//! sequence runs (decision 0022).
//!
//! [`plan_commit`] is a pure reading of the ledger. It answers three different
//! questions with three different consequences, and keeping them apart is what
//! puts each refusal where the specification's order wants it:
//!
//! - **The proposal's shape is wrong** — a commit of a two-phase class carrying
//!   no prepared-effect binding, two of them, or one attached to a request that
//!   commits nothing prepared. That is a fact about the authority rather than
//!   about the world, so it refuses immediately, beside the digest and scope
//!   checks the broker already makes at step four.
//! - **The ledger refuses the named prepare.** That is a precondition, and it
//!   is reported at step six with everything else the proposal declared — so a
//!   document that moved is still reported as a document that moved rather than
//!   as a missing prepare.
//! - **The ledger agrees.** Nothing is spent yet. The binding is carried back
//!   so the caller can spend it once the rest of the sequence has proceeded,
//!   because a commit refused at step two never reached the page and must not
//!   cost the preparation.

use bip_types::action::{Precondition, PreconditionKind, PreparedEffectBinding};
use bip_types::identity::{MonotonicMillis, TaskId};
use bip_types::ActionResultCode;

use crate::action_class::ActionClass;
use crate::capability::CapabilityScope;
use crate::lease::ActorLeaseId;
use crate::phase::ActionPhase;
use crate::prepared::ledger::PreparedEffectLedger;

/// What the broker already knows about the request being committed.
///
/// Every field comes from the capability that authorized it, never from the
/// message: a commit cannot present the task, the lease, the class, or the
/// place it wants to be compared against.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CommitContext<'a> {
    /// The task the commit belongs to.
    pub task_id: &'a TaskId,
    /// The actor lease the commit is authorized under.
    pub lease_id: &'a ActorLeaseId,
    /// What the effect does.
    pub action_class: ActionClass,
    /// Which half of the authorization this request is.
    pub phase: ActionPhase,
    /// Where the effect would happen.
    pub scope: &'a CapabilityScope,
}

impl CommitContext<'_> {
    /// Whether this request is the commit half of a prepared effect.
    ///
    /// Both halves of the question matter. A prepare commits nothing whatever
    /// its class, and a class nobody is asked about has nothing to have
    /// prepared, so only their conjunction may carry a binding.
    pub const fn commits_a_prepared_effect(&self) -> bool {
        self.phase.causes_effect() && self.action_class.is_two_phase()
    }
}

/// What the broker decided about the prepare a commit names.
///
/// Supplied to the precondition evaluator so that
/// [`PreconditionKind::PreparedEffectUnchanged`] is answered from the ledger
/// rather than from the message that carries it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum PreparedEffectStanding {
    /// Nobody asked the ledger. The default, and it refuses: reporting the
    /// effect unchanged would claim a comparison nothing performed.
    #[default]
    NotChecked,
    /// The ledger compared the commit against its record and the effect is the
    /// one that was prepared, rendered, and confirmed.
    Unchanged,
    /// The ledger refused, with the code the action ends with.
    Refused(ActionResultCode),
}

impl PreparedEffectStanding {
    /// The refusal code, or `None` when the commit may proceed.
    pub const fn refusal(self) -> Option<ActionResultCode> {
        match self {
            Self::Unchanged => None,
            Self::Refused(code) => Some(code),
            // Named rather than folded into the arm above, because "nobody
            // checked" and "the ledger refused" are different facts that happen
            // to end the action the same way.
            Self::NotChecked => Some(ActionResultCode::Unsupported),
        }
    }
}

/// What one dispatch may commit.
#[derive(Clone, Debug, PartialEq)]
pub enum CommitPlan {
    /// The request commits nothing prepared, and nothing claimed it did.
    NoPreparedEffect,
    /// The named prepare stands. The binding is carried so it can be spent once
    /// the rest of the sequence proceeds; nothing has been spent yet.
    Committable(PreparedEffectBinding),
    /// The proposal's shape is wrong. Refuse before the sequence runs.
    Malformed(ActionResultCode),
    /// The ledger refused the named prepare. Report at step six.
    Refused(ActionResultCode),
}

impl CommitPlan {
    /// What the precondition evaluator should be told.
    pub const fn standing(&self) -> PreparedEffectStanding {
        match self {
            Self::Committable(_) => PreparedEffectStanding::Unchanged,
            Self::Refused(code) => PreparedEffectStanding::Refused(*code),
            // A malformed plan never reaches the evaluator, and a plan with no
            // prepared effect has no binding for the evaluator to answer about.
            Self::NoPreparedEffect | Self::Malformed(_) => PreparedEffectStanding::NotChecked,
        }
    }

    /// The code a shape refusal ends the action with, when it is one.
    pub const fn malformed_code(&self) -> Option<ActionResultCode> {
        match self {
            Self::Malformed(code) => Some(*code),
            Self::NoPreparedEffect | Self::Committable(_) | Self::Refused(_) => None,
        }
    }
}

/// Reads the ledger for one dispatch and decides what it may commit.
///
/// Pure: it spends nothing and mutates nothing. The caller spends the prepared
/// effect only after the rest of the sequence has proceeded.
pub fn plan_commit(
    ledger: &PreparedEffectLedger,
    context: &CommitContext<'_>,
    preconditions: &[Precondition],
    now: MonotonicMillis,
) -> CommitPlan {
    let mut declared = preconditions
        .iter()
        .filter(|precondition| precondition.kind == PreconditionKind::PreparedEffectUnchanged);
    let first = declared.next();
    // A commit that names two prepares names none: there is no rule for
    // choosing between them that is not a rule for ignoring one.
    if declared.next().is_some() {
        return CommitPlan::Malformed(ActionResultCode::CommitWithoutPrepare);
    }

    match (first, context.commits_a_prepared_effect()) {
        // A commit of a two-phase class with nothing claiming a prepare. Rule
        // one of decision 0022: a commit cannot invent a prepare that did not
        // run, and one that names none has invented the absence of the check.
        (None, true) => CommitPlan::Malformed(ActionResultCode::CommitWithoutPrepare),
        (None, false) => CommitPlan::NoPreparedEffect,
        // A binding on a prepare, or on a class that is never prepared at all.
        // Neither has a prepare to complete, so neither may carry the operand
        // that says it does.
        (Some(_), false) => CommitPlan::Malformed(ActionResultCode::DeniedByPolicy),
        (Some(precondition), true) => match precondition.prepared_effect.as_ref() {
            // The kind without its operand. Left to the evaluator, which
            // refuses a precondition whose operand its kind selects is absent —
            // the same way every other kind here fails closed.
            None => CommitPlan::NoPreparedEffect,
            Some(binding) => match ledger.verdict(binding, context, now) {
                Ok(()) => CommitPlan::Committable(binding.clone()),
                Err(refusal) => CommitPlan::Refused(refusal.result_code()),
            },
        },
    }
}
