// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one definitive provider failure becomes: the next attempt, or the gap
//! the journal records and the class the walk reads beside it.
//!
//! Beside the composition rather than inside it because the retry verdict is
//! the router's table applied to a held plan, and the settlement is the one
//! place a provider's answer is kept for the walk (decision 0136).

use bip_types::identity::TaskId;
use loop_kernel::state::ProviderGapClass;
use model_router::retry::{verdict, AttemptVerdict, NoJitter};
use model_router::ErrorClass;
use task_engine::{ModelAttemptKind, TurnGap};

use crate::{model_request_id, ModelReplyStream};

use super::{ModelAttemptDispatch, ModelFailureOutcome};
use crate::composition::profile::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    /// Applies the router retry table to one definitive direct-provider failure.
    ///
    /// Outcome-unknown and cancellation terminals never enter this method.
    /// The total attempt ceiling includes the initial call, so retries and
    /// substitutes share one accounting bound rather than multiplying it.
    pub fn plan_model_failure(
        &mut self,
        task_id: &TaskId,
        call_id: &str,
        model_id: &str,
        class: ErrorClass,
        retry_after_millis: Option<u64>,
    ) -> ModelFailureOutcome {
        let Some(facts) = self
            .core
            .task(task_id)
            .map(loop_kernel::ports::TaskEnginePort::model_turn_facts)
        else {
            return ModelFailureOutcome::Invalid;
        };
        let milestone = facts.milestone;
        let Ok(request_id) = model_request_id(self.digest.as_ref(), call_id) else {
            return ModelFailureOutcome::Invalid;
        };
        let Some(held) = self.core.loop_state_mut(task_id.as_str()).held.as_mut() else {
            return ModelFailureOutcome::Invalid;
        };
        if held.call_id.as_str() != call_id
            || held.turn.request.model_id != model_id
            || facts.attempts_started.checked_sub(1) != Some(held.attempt_ordinal)
            || facts.candidate_ordinal != held.candidate_ordinal
        {
            return ModelFailureOutcome::Invalid;
        }
        // Managed failover belongs to the worker. A media handle has already
        // been spent and cannot authorize a second upload.
        if held.turn.request.wire_api == core_service_types::ProviderWireApi::Managed
            || held.turn.request.media_attachment_handle.is_some()
        {
            return self.settle_failed_turn(task_id, class, gap_for_model_error(class));
        }
        let next_ordinal = held.attempt_ordinal.checked_add(1);
        let has_attempt_slot = next_ordinal
            .is_some_and(|ordinal| ordinal < model_router::defaults::SEMANTIC_RETRY_ATTEMPTS);
        let decision = verdict(
            class,
            held.candidate_attempt,
            retry_after_millis,
            has_attempt_slot && !held.turn.failover.is_empty(),
            &NoJitter,
        );
        if !facts.can_afford_model_attempt
            && matches!(
                decision,
                AttemptVerdict::RetryAfter { .. } | AttemptVerdict::Failover
            )
        {
            return self.settle_failed_turn(task_id, class, TurnGap::Refused);
        }
        let (delay_millis, kind) = match decision {
            AttemptVerdict::RetryAfter { millis } if has_attempt_slot => {
                held.candidate_attempt = held.candidate_attempt.saturating_add(1);
                (millis, ModelAttemptKind::Retry)
            }
            AttemptVerdict::Failover if has_attempt_slot => {
                let Some(next) = held.turn.failover.pop_front() else {
                    return ModelFailureOutcome::Invalid;
                };
                let Some(candidate_ordinal) = held.candidate_ordinal.checked_add(1) else {
                    return ModelFailureOutcome::Invalid;
                };
                held.turn.request = next.request;
                held.turn.wire = next.wire;
                held.turn.context_window = next.context_window;
                held.turn.answer_tokens = next.answer_tokens;
                held.candidate_attempt = 0;
                held.candidate_ordinal = candidate_ordinal;
                (0, ModelAttemptKind::Failover)
            }
            AttemptVerdict::RetryAfter { .. } | AttemptVerdict::Failover | AttemptVerdict::Stop => {
                return self.settle_failed_turn(task_id, class, gap_for_model_error(class));
            }
        };
        let Some(attempt_ordinal) = next_ordinal else {
            return ModelFailureOutcome::Invalid;
        };
        held.attempt_ordinal = attempt_ordinal;
        held.stream = Some(ModelReplyStream::for_turn(
            &held.turn, request_id, milestone,
        ));
        ModelFailureOutcome::Dispatch(Box::new(ModelAttemptDispatch {
            request: held.turn.request.clone(),
            attempt_ordinal,
            candidate_ordinal: held.candidate_ordinal,
            kind,
            delay_millis,
        }))
    }

    /// Ends the held logical turn on a definitive provider answer.
    ///
    /// The gap is what the journal will hold; the class beside it is what the
    /// walk reads once, on the pass that turns the gap into a state.
    fn settle_failed_turn(
        &mut self,
        task_id: &TaskId,
        class: ErrorClass,
        gap: TurnGap,
    ) -> ModelFailureOutcome {
        let state = self.core.loop_state_mut(task_id.as_str());
        state.held = None;
        state.gap_class = provider_gap_class(class);
        ModelFailureOutcome::Gap(gap)
    }
}

/// What the walk may make of the answer, beyond the gap it records.
///
/// An overflow is the request's own doing and a cancellation is the person's;
/// an unknown answer is unknown. None of those is the provider saying no, so
/// none of them carries a class and the task ends as it always has.
const fn provider_gap_class(class: ErrorClass) -> Option<ProviderGapClass> {
    match class {
        ErrorClass::Auth | ErrorClass::InvalidRequest => Some(ProviderGapClass::Refused),
        ErrorClass::Quota => Some(ProviderGapClass::Limit),
        ErrorClass::Overloaded => Some(ProviderGapClass::Busy),
        ErrorClass::Network => Some(ProviderGapClass::Offline),
        ErrorClass::Canceled | ErrorClass::Overflow | ErrorClass::Unknown => None,
    }
}

const fn gap_for_model_error(class: ErrorClass) -> TurnGap {
    match class {
        ErrorClass::Canceled => TurnGap::Cancelled,
        ErrorClass::Auth
        | ErrorClass::Quota
        | ErrorClass::InvalidRequest
        | ErrorClass::Overflow => TurnGap::Refused,
        ErrorClass::Overloaded | ErrorClass::Network | ErrorClass::Unknown => TurnGap::Unavailable,
    }
}

#[cfg(test)]
mod tests {
    use super::{provider_gap_class, ErrorClass, ProviderGapClass};

    /// An overload and an exhausted allowance are different facts.
    ///
    /// They were one `Limit`, which is how a phone came to say "your AI
    /// provider limit was reached" about an HTTP 503 — the provider's own
    /// capacity — to somebody whose plan had usage left. The browser already
    /// separates them (402/429 are `Quota`, 5xx is `Overloaded`); this is the
    /// step that used to throw that away (decision 0219).
    #[test]
    fn an_overload_is_not_the_persons_limit() {
        assert_eq!(
            provider_gap_class(ErrorClass::Overloaded),
            Some(ProviderGapClass::Busy)
        );
        assert_eq!(
            provider_gap_class(ErrorClass::Quota),
            Some(ProviderGapClass::Limit)
        );
        assert_ne!(
            provider_gap_class(ErrorClass::Overloaded),
            provider_gap_class(ErrorClass::Quota)
        );
    }

    /// The classes that carry no verdict still carry none.
    ///
    /// A cancellation is the person's, an overflow is the request's own doing,
    /// and an unknown answer is unknown — none of them is the provider saying
    /// no, so the task ends as it always has rather than being held.
    #[test]
    fn only_a_providers_own_answer_carries_a_class() {
        for class in [
            ErrorClass::Canceled,
            ErrorClass::Overflow,
            ErrorClass::Unknown,
        ] {
            assert_eq!(provider_gap_class(class), None);
        }
        assert_eq!(
            provider_gap_class(ErrorClass::Network),
            Some(ProviderGapClass::Offline)
        );
        for class in [ErrorClass::Auth, ErrorClass::InvalidRequest] {
            assert_eq!(provider_gap_class(class), Some(ProviderGapClass::Refused));
        }
    }
}
