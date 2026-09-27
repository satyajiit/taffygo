// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The cells for the commands that decide whether a task runs at all.
//!
//! Consent, holding, resuming, stopping, settling, and the two ways a task
//! waits for a person. One function per command: the grid's totality is
//! proved by the exhaustive match in [`super`], so each cell here is a
//! decision about one command that can be read without the other thirty-five.

use super::super::disposition::{moves, moves_if, records, records_if, refuse};
use super::super::guard::Guard;
use super::super::refusal::RefusalReason as R;
use super::super::Disposition;
use crate::task::TaskState;
use crate::task::TaskState as S;

/// Creation is a constructor, not a command applied to an existing task.
pub(super) const fn create_task(_state: TaskState) -> Disposition {
    refuse(R::TaskAlreadyExists)
}

/// The scope is only editable while the task is still being set up.
pub(super) const fn edit_scope(state: TaskState) -> Disposition {
    match state {
        S::Draft => records(),
        S::AwaitingConsent => moves_if(&[S::Draft], &[Guard::InitialConsentPending]),
        S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::ScopeIsFrozen),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// Starting carries the preview, so a draft can never skip consent.
pub(super) const fn start_task(state: TaskState) -> Disposition {
    match state {
        S::Draft => moves_if(&[S::AwaitingConsent], &[Guard::WithinBudget]),
        S::AwaitingConsent => refuse(R::ConsentDecisionPending),
        S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::AlreadyStarted),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn accept_initial_consent(state: TaskState) -> Disposition {
    match state {
        S::AwaitingConsent => moves_if(&[S::Queued], &[Guard::InitialConsentPending]),
        S::Draft
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotAwaitingConsent),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// A prepared discovery tab is data, not a task-state edge. It may arrive
/// after the executor has started or while a control transition is settling,
/// but never before initial consent and never after the task has ended.
pub(super) const fn record_discovery_tab(state: TaskState) -> Disposition {
    match state {
        S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => records_if(&[Guard::DiscoveryAuthorityMatches]),
        S::Draft | S::AwaitingConsent => refuse(R::NotStarted),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn approve_action(state: TaskState) -> Disposition {
    match state {
        S::AwaitingConsent => moves_if(
            &[S::Running],
            &[Guard::InTaskApprovalPending, Guard::ApprovalStillCurrent],
        ),
        S::Draft
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotAwaitingConsent),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn deny_action(state: TaskState) -> Disposition {
    match state {
        S::AwaitingConsent => moves_if(&[S::Pausing], &[Guard::InTaskApprovalPending]),
        S::Draft
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotAwaitingConsent),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn pause_task(state: TaskState) -> Disposition {
    match state {
        S::AwaitingConsent => moves_if(&[S::Pausing], &[Guard::InTaskApprovalPending]),
        S::Running | S::WaitingUser => moves(&[S::Pausing]),
        // Already holding, or already on the way to it.
        S::Pausing | S::Paused => records(),
        S::Draft => refuse(R::NotStarted),
        // Section 9.2 draws no pause edge from these three.
        S::Queued | S::Completing => refuse(R::NotPausable),
        S::Cancelling => refuse(R::AlreadySettling),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn take_over(state: TaskState) -> Disposition {
    match state {
        S::AwaitingConsent => moves_if(&[S::Pausing], &[Guard::InTaskApprovalPending]),
        S::Running | S::WaitingUser => moves(&[S::Pausing]),
        S::Pausing | S::Paused => records(),
        S::Draft => refuse(R::NotStarted),
        // Nothing is executing and no lease stands, so there is nothing to
        // take back.
        S::Queued => refuse(R::NoActorLeaseHere),
        S::Completing => refuse(R::NotPausable),
        S::Cancelling => refuse(R::AlreadySettling),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn pause_settled(state: TaskState) -> Disposition {
    match state {
        S::Pausing => moves_if(
            &[S::Paused],
            &[Guard::NoActionWorkInFlight, Guard::NoModelTurnInFlight],
        ),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotSettlingAPause),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// Resume re-enters the queue, so browser, source, and provider state is
/// revalidated rather than assumed.
pub(super) const fn resume_task(state: TaskState) -> Disposition {
    match state {
        S::Paused => moves(&[S::Queued]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Cancelling
        | S::Completing => refuse(R::NotPaused),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn cancel_task(state: TaskState) -> Disposition {
    match state {
        // Discarding a draft has no authority to revoke and no journal to
        // settle, so it ends immediately.
        S::Draft => moves(&[S::Cancelled]),
        S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Paused
        | S::Completing => moves(&[S::Cancelling]),
        S::Cancelling => records(),
        // Section 9.2 draws no cancel edge from PAUSING; the task settles
        // to PAUSED, which does have one.
        S::Pausing => refuse(R::AlreadySettling),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn cancel_settled(state: TaskState) -> Disposition {
    match state {
        S::Cancelling => moves_if(
            &[S::Cancelled],
            &[Guard::NoActionWorkInFlight, Guard::NoModelTurnInFlight],
        ),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Completing => refuse(R::NotSettlingACancel),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn executor_started(state: TaskState) -> Disposition {
    match state {
        S::Queued => moves(&[S::Running]),
        S::Draft
        | S::AwaitingConsent
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotQueued),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn request_approval(state: TaskState) -> Disposition {
    match state {
        S::Running => moves_if(&[S::AwaitingConsent], &[Guard::ActionKnown]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn request_user_input(state: TaskState) -> Disposition {
    match state {
        S::Running => moves(&[S::WaitingUser]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// A follow-up reopens a finished task (decision 0137).
///
/// `COMPLETED` and `PARTIAL` are the two ends a conversation may continue
/// from: the task produced something and the person wants more. A task that
/// has not finished is refused as not finished rather than as terminal, and
/// the two ends that produced nothing — `FAILED` and `CANCELLED` — stay
/// terminal, because there is no conversation to continue.
pub(super) const fn follow_up(state: TaskState) -> Disposition {
    match state {
        S::Completed | S::Partial => moves(&[S::Running]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotFinished),
        S::Cancelled | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn supply_user_input(state: TaskState) -> Disposition {
    match state {
        S::WaitingUser => moves_if(&[S::Running], &[Guard::UserInputPending]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotWaitingForUser),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn request_permission(state: TaskState) -> Disposition {
    match state {
        S::Running => moves(&[S::WaitingUser]),
        S::Draft | S::AwaitingConsent | S::Queued | S::WaitingUser | S::Paused | S::Completing => {
            refuse(R::NotRunning)
        }
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// Handing the page over is a `RUNNING` decision, like asking for a native
/// permission and for the same reason: it is the assistant stopping, and a
/// task that is not executing has nothing to stop.
///
/// A settling task refuses rather than handing over. It is already giving its
/// authority back; opening a window that waits on a person would be waiting on
/// something the settlement is not allowed to depend on.
/// Asking the person to fill a form in. Reachable only from `Running`, like
/// every other way the assistant stops and waits, and it leaves the task's
/// lease standing: the assistant carries on in the same tab once the values
/// are held (decision 0088, unlike a handover).
pub(super) const fn request_field_values(state: TaskState) -> Disposition {
    match state {
        S::Running => moves(&[S::WaitingUser]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// The person answered. Guarded on the request being the open one, so an
/// answer to a request that is no longer pending cannot resume the task.
pub(super) const fn supply_field_values(state: TaskState) -> Disposition {
    match state {
        S::WaitingUser => moves_if(&[S::Running], &[Guard::FieldValueRequestMatches]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotWaitingForUser),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn request_handover(state: TaskState) -> Disposition {
    match state {
        S::Running => moves(&[S::WaitingUser]),
        S::Draft | S::AwaitingConsent | S::Queued | S::WaitingUser | S::Paused | S::Completing => {
            refuse(R::NotRunning)
        }
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// The person came back. Two guards, because two different things could be
/// wrong: this could answer a handover that is not the open one, and it could
/// resume under the lease the handover already revoked.
pub(super) const fn complete_handover(state: TaskState) -> Disposition {
    match state {
        S::WaitingUser => moves_if(
            &[S::Running],
            &[Guard::PendingHandoverMatches, Guard::ResumptionLeaseIsNew],
        ),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotWaitingForUser),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// Nobody came back, so the task is held rather than resumed.
///
/// It goes to `PAUSING` and not back to `RUNNING`: the assistant gave the page
/// away and has no standing to take it back on a timer. Held is a state the
/// person resumes from when they choose, and resuming re-enters the queue,
/// where the browser, the sources and the route are revalidated — which is
/// exactly right after a stretch of time in which a person was doing things in
/// the tab.
pub(super) const fn expire_handover(state: TaskState) -> Disposition {
    match state {
        S::WaitingUser => moves_if(&[S::Pausing], &[Guard::PendingHandoverMatches]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotWaitingForUser),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn record_permission_result(state: TaskState) -> Disposition {
    match state {
        S::WaitingUser => moves_if(&[S::Running], &[Guard::PendingPermissionMatches]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotWaitingForUser),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}
