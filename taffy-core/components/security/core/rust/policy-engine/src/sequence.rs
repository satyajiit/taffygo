// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The complete section 12 sequence as one pure state machine.
//!
//! [`crate::precondition::evaluate_dispatch`] decides steps one to six.
//! [`StaleNodeSequence`] is the whole of section 12: it consumes that verdict,
//! then the outcome of each effect step, and it ends in exactly one
//! [`ActionResultCode`]. It reads no clock, holds no reference to a browser,
//! and performs nothing, so every branch of every step is reachable from a
//! table in a test.
//!
//! # Why the effect steps are a decision too
//!
//! Steps seven to ten are performed by the browser broker, but what each of
//! their outcomes *means* is a policy decision, and a decision made in three
//! places drifts. Recording an intent that never dispatched, a renderer
//! acknowledgement that is not a verification, a cancellation that arrives
//! after the command was sent, and a ledger that finds the authority already
//! spent all have exactly one right answer, and it is written down once, here.
//!
//! # Two rules the machine holds
//!
//! - **The order is the specification's order.** An outcome that belongs to
//!   another step ends the sequence with an internal fault rather than being
//!   applied out of turn. The type-state wrapper in [`crate::dispatch`] makes
//!   that unreachable through the ordinary interface; the machine still refuses
//!   it, because a machine that only behaves when driven correctly is not a
//!   control.
//! - **After the journal step, nothing claims the page was untouched.** A
//!   sequence that has recorded its intent can only end in a code whose side
//!   effect is `Performed` or `Possible`, never in one that says the action was
//!   refused before it began.

use bip_types::ActionResultCode;

use crate::precondition::DispatchVerdict;
use crate::report::{ConsumptionOutcome, DispatchAck, JournalOutcome, PostconditionReport};
use crate::step::StaleNodeStep;

/// What the sequence is waiting for.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SequenceState {
    /// Steps one to six have not been decided yet.
    AwaitingPreconditions,
    /// Step 7 — waiting for the journal write.
    AwaitingJournal,
    /// Step 8 — waiting for the input path.
    AwaitingDispatch,
    /// Step 9 — waiting for the verifier.
    AwaitingPostcondition,
    /// Step 10 — the result is settled and the capability has not been spent.
    AwaitingRecord {
        /// The code the action ends with, unless the ledger disagrees.
        code: ActionResultCode,
    },
    /// The sequence is over.
    Ended {
        /// The recorded terminal code.
        code: ActionResultCode,
    },
}

impl SequenceState {
    /// The step this state is waiting on, or the last step for an ended
    /// sequence.
    pub const fn awaited_step(self) -> StaleNodeStep {
        match self {
            Self::AwaitingPreconditions => StaleNodeStep::ResolveTabAndFrame,
            Self::AwaitingJournal => StaleNodeStep::JournalIntent,
            Self::AwaitingDispatch => StaleNodeStep::Dispatch,
            Self::AwaitingPostcondition => StaleNodeStep::ObservePostconditions,
            Self::AwaitingRecord { .. } | Self::Ended { .. } => StaleNodeStep::RecordTerminalResult,
        }
    }
}

/// One outcome fed to the sequence.
///
/// The alphabet is closed and each member belongs to exactly one step, so a
/// caller cannot answer a question the sequence did not ask.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SequenceInput {
    /// Steps 1 to 6 — the verdict of the pre-dispatch checks.
    Preconditions(DispatchVerdict),
    /// Step 7 — whether the intent was recorded.
    Journal(JournalOutcome),
    /// Step 8 — what the input path did.
    Dispatch(DispatchAck),
    /// Step 9 — what the verifier saw.
    Postcondition(PostconditionReport),
    /// Step 10 — what the capability ledger did.
    Record(ConsumptionOutcome),
}

impl SequenceInput {
    /// The step this outcome answers.
    pub const fn step(self) -> StaleNodeStep {
        match self {
            Self::Preconditions(_) => StaleNodeStep::ResolveTabAndFrame,
            Self::Journal(_) => StaleNodeStep::JournalIntent,
            Self::Dispatch(_) => StaleNodeStep::Dispatch,
            Self::Postcondition(_) => StaleNodeStep::ObservePostconditions,
            Self::Record(_) => StaleNodeStep::RecordTerminalResult,
        }
    }
}

/// What one input did to the sequence.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SequenceProgress {
    /// The step completed and the sequence moved on.
    Advanced {
        /// The step that completed.
        step: StaleNodeStep,
        /// The step the sequence now waits on.
        next: StaleNodeStep,
    },
    /// The step needs another outcome before it can complete. Only step 9 can
    /// answer this: a dispatch the renderer alone acknowledged is not yet
    /// verified.
    Waiting {
        /// The step still in progress.
        step: StaleNodeStep,
    },
    /// The sequence ended.
    Ended {
        /// The step the terminal result was decided at.
        step: StaleNodeStep,
        /// The code the action ends with.
        code: ActionResultCode,
    },
}

/// The section 12 sequence.
///
/// Construct it with [`Self::new`], feed it one outcome per step, and read the
/// terminal code with [`Self::result`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StaleNodeSequence {
    state: SequenceState,
    furthest: StaleNodeStep,
    refusal_step: Option<StaleNodeStep>,
    journalled: bool,
}

impl Default for StaleNodeSequence {
    fn default() -> Self {
        Self::new()
    }
}

impl StaleNodeSequence {
    /// A sequence waiting on the pre-dispatch checks.
    pub const fn new() -> Self {
        Self {
            state: SequenceState::AwaitingPreconditions,
            furthest: StaleNodeStep::ResolveTabAndFrame,
            refusal_step: None,
            journalled: false,
        }
    }

    /// What the sequence is waiting for.
    pub const fn state(&self) -> SequenceState {
        self.state
    }

    /// The furthest step the sequence reached.
    pub const fn furthest_step(&self) -> StaleNodeStep {
        self.furthest
    }

    /// The step a refusal stopped at, once one has.
    pub const fn refusal_step(&self) -> Option<StaleNodeStep> {
        self.refusal_step
    }

    /// The terminal code, once the sequence has ended.
    pub const fn result(&self) -> Option<ActionResultCode> {
        match self.state {
            SequenceState::Ended { code } => Some(code),
            _ => None,
        }
    }

    /// Whether a side effect can no longer be ruled out.
    ///
    /// The journal step is the boundary, and it is recorded as a fact rather
    /// than inferred from how far the sequence got: a sequence that reached
    /// step 7 and chose not to record an intent sent nothing, and saying
    /// otherwise would forbid a safe retry.
    pub const fn may_have_reached_the_page(&self) -> bool {
        self.journalled
    }

    /// Whether the dispatching intent was recorded.
    pub const fn journalled_intent(&self) -> bool {
        self.journalled
    }

    /// Applies one outcome.
    ///
    /// An outcome for a step the sequence is not on ends it with
    /// [`ActionResultCode::InternalError`]. That is the fail-closed answer
    /// rather than a convenience: an out-of-order outcome means the broker and
    /// this machine disagree about what has already happened, and neither of
    /// them can be trusted to say the page is untouched.
    pub fn step(&mut self, input: SequenceInput) -> SequenceProgress {
        match (self.state, input) {
            (SequenceState::AwaitingPreconditions, SequenceInput::Preconditions(verdict)) => {
                self.apply_preconditions(verdict)
            }
            (SequenceState::AwaitingJournal, SequenceInput::Journal(outcome)) => {
                self.apply_journal(outcome)
            }
            (SequenceState::AwaitingDispatch, SequenceInput::Dispatch(ack)) => {
                self.apply_dispatch(ack)
            }
            (SequenceState::AwaitingPostcondition, SequenceInput::Postcondition(report)) => {
                self.apply_postcondition(report)
            }
            (SequenceState::AwaitingRecord { code }, SequenceInput::Record(outcome)) => {
                self.apply_record(code, outcome)
            }
            // An already ended sequence is not reopened, and the recorded
            // result is not rewritten by a late outcome.
            (SequenceState::Ended { code }, _) => SequenceProgress::Ended {
                step: StaleNodeStep::RecordTerminalResult,
                code,
            },
            (state, _) => self.fault(state.awaited_step()),
        }
    }

    /// Steps 1 to 6.
    fn apply_preconditions(&mut self, verdict: DispatchVerdict) -> SequenceProgress {
        match verdict {
            DispatchVerdict::Proceed => {
                self.furthest = StaleNodeStep::ReevaluateNode;
                self.state = SequenceState::AwaitingJournal;
                SequenceProgress::Advanced {
                    step: StaleNodeStep::ReevaluateNode,
                    next: StaleNodeStep::JournalIntent,
                }
            }
            DispatchVerdict::Refuse { step, code } => {
                self.furthest = step;
                self.refusal_step = Some(step);
                self.state = SequenceState::AwaitingRecord { code };
                SequenceProgress::Advanced {
                    step,
                    next: StaleNodeStep::RecordTerminalResult,
                }
            }
        }
    }

    /// Step 7 — the intent is durable before the effect.
    fn apply_journal(&mut self, outcome: JournalOutcome) -> SequenceProgress {
        self.furthest = StaleNodeStep::JournalIntent;
        match outcome {
            JournalOutcome::Recorded => {
                self.journalled = true;
                self.state = SequenceState::AwaitingDispatch;
                SequenceProgress::Advanced {
                    step: StaleNodeStep::JournalIntent,
                    next: StaleNodeStep::Dispatch,
                }
            }
            // Nothing was sent, but the sequence had already committed to
            // sending: the authority is in flight and the honest answer is that
            // the broker failed, not that the page refused.
            JournalOutcome::WriteFailed => {
                self.refusal_step = Some(StaleNodeStep::JournalIntent);
                self.settle(
                    StaleNodeStep::JournalIntent,
                    ActionResultCode::InternalError,
                )
            }
            // The broker gave the authority back rather than using it. Nothing
            // was recorded and nothing was sent, so the page is untouched.
            JournalOutcome::NotAttempted => {
                self.refusal_step = Some(StaleNodeStep::JournalIntent);
                self.settle(
                    StaleNodeStep::JournalIntent,
                    ActionResultCode::CancelledByUser,
                )
            }
        }
    }

    /// Step 8 — the normal browser and renderer input path.
    fn apply_dispatch(&mut self, ack: DispatchAck) -> SequenceProgress {
        self.furthest = StaleNodeStep::Dispatch;
        match ack {
            DispatchAck::AcceptedByExecutor => {
                self.state = SequenceState::AwaitingPostcondition;
                SequenceProgress::Advanced {
                    step: StaleNodeStep::Dispatch,
                    next: StaleNodeStep::ObservePostconditions,
                }
            }
            DispatchAck::RejectedByExecutor => {
                self.refuse_at_dispatch(ActionResultCode::DispatchFailed)
            }
            DispatchAck::RendererGone => self.refuse_at_dispatch(ActionResultCode::RendererCrashed),
            DispatchAck::TargetGoneAtDispatch => {
                self.refuse_at_dispatch(ActionResultCode::NodeGone)
            }
            DispatchAck::CancelledByUserBeforeSend => {
                self.refuse_at_dispatch(ActionResultCode::CancelledByUser)
            }
            DispatchAck::NavigationCommittedBeforeSend => {
                self.refuse_at_dispatch(ActionResultCode::CancelledByNavigation)
            }
        }
    }

    /// Step 9 — observe until deadline, cancellation, navigation, crash, or
    /// contradiction.
    fn apply_postcondition(&mut self, report: PostconditionReport) -> SequenceProgress {
        self.furthest = StaleNodeStep::ObservePostconditions;
        let code = match report {
            PostconditionReport::Satisfied => ActionResultCode::Verified,
            // A dispatch the renderer alone acknowledged. The sequence stays on
            // step 9 and keeps waiting for browser-owned corroboration.
            PostconditionReport::AcknowledgedOnly => {
                return SequenceProgress::Waiting {
                    step: StaleNodeStep::ObservePostconditions,
                }
            }
            PostconditionReport::Contradicted => ActionResultCode::PostconditionFailed,
            PostconditionReport::DeadlineExpired => ActionResultCode::PostconditionTimeout,
            PostconditionReport::NavigationCommitted => ActionResultCode::CancelledByNavigation,
            // The command was already sent, so a cancellation cannot rule the
            // effect out. Reporting it as a cancellation would license a retry
            // that an unconfirmed side effect forbids.
            PostconditionReport::CancelledByUserAfterSend | PostconditionReport::Unreconciled => {
                ActionResultCode::OutcomeUnknown
            }
            PostconditionReport::RendererCrashed => ActionResultCode::RendererCrashed,
        };
        if code != ActionResultCode::Verified {
            self.refusal_step = Some(StaleNodeStep::ObservePostconditions);
        }
        self.settle(StaleNodeStep::ObservePostconditions, code)
    }

    /// Step 10 — record the terminal result and consume the capability.
    fn apply_record(
        &mut self,
        code: ActionResultCode,
        outcome: ConsumptionOutcome,
    ) -> SequenceProgress {
        let journalled = self.journalled;
        let recorded = match outcome {
            ConsumptionOutcome::Spent => code,
            // Nothing to spend is the ordinary shape of a refusal that happened
            // before any authority was put in flight. After the intent was
            // journalled it is a fault: the sequence believed it held authority
            // and the ledger disagreed.
            ConsumptionOutcome::NothingToSpend if !journalled => code,
            ConsumptionOutcome::NothingToSpend | ConsumptionOutcome::AlreadySpent => {
                self.refusal_step = Some(StaleNodeStep::RecordTerminalResult);
                ActionResultCode::InternalError
            }
        };
        self.furthest = StaleNodeStep::RecordTerminalResult;
        self.state = SequenceState::Ended { code: recorded };
        SequenceProgress::Ended {
            step: StaleNodeStep::RecordTerminalResult,
            code: recorded,
        }
    }

    /// Ends step 8 with a refusal.
    fn refuse_at_dispatch(&mut self, code: ActionResultCode) -> SequenceProgress {
        self.refusal_step = Some(StaleNodeStep::Dispatch);
        self.settle(StaleNodeStep::Dispatch, code)
    }

    /// Moves to step 10 with a settled code.
    fn settle(&mut self, step: StaleNodeStep, code: ActionResultCode) -> SequenceProgress {
        self.state = SequenceState::AwaitingRecord { code };
        SequenceProgress::Advanced {
            step,
            next: StaleNodeStep::RecordTerminalResult,
        }
    }

    /// Ends the sequence because an outcome arrived for the wrong step.
    fn fault(&mut self, step: StaleNodeStep) -> SequenceProgress {
        self.refusal_step = Some(step);
        self.state = SequenceState::Ended {
            code: ActionResultCode::InternalError,
        };
        SequenceProgress::Ended {
            step,
            code: ActionResultCode::InternalError,
        }
    }
}
