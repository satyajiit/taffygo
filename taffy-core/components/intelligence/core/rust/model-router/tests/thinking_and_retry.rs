// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The thinking ladder, and retry and failover planning.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::WireApi;
use model_router::defaults;
use model_router::request::{
    ErrorClass, RetryDisposition, StopReason, TerminalResult, TerminalSlot,
};
use model_router::retry::{
    honor_server_delay, semantic_backoff_millis, transport_backoff_millis, verdict, AttemptVerdict,
    Jitter, NoJitter, ServerDelayVerdict,
};
use model_router::thinking::{plan, LevelSupport, ThinkingLevels, LADDER};
use model_router::{ids::RequestId, ThinkingLevel, ThinkingPlan, TokenUsage};

/// A model that cannot turn thinking off and publishes only two rungs.
fn narrowed() -> ThinkingLevels {
    ThinkingLevels::from_entries([
        (ThinkingLevel::Off, None),
        (ThinkingLevel::Minimal, Some("minimal".to_owned())),
        (ThinkingLevel::Low, None),
        (ThinkingLevel::Medium, None),
        (ThinkingLevel::High, Some("high".to_owned())),
    ])
}

#[test]
fn an_empty_map_means_adapter_defaults_below_the_opt_in_rungs() {
    let levels = ThinkingLevels::adapter_defaults();
    for level in LADDER {
        let expected = if level.is_opt_in() {
            LevelSupport::Unsupported
        } else {
            LevelSupport::AdapterDefault
        };
        assert_eq!(levels.support(level), expected, "{level:?}");
    }
    assert_eq!(levels.selectable().len(), 5);
}

#[test]
fn an_unsupported_rung_clamps_upward_first_and_downward_after() {
    let levels = narrowed();
    // LOW and MEDIUM are published as unsupported, so both find HIGH by
    // searching up.
    assert_eq!(levels.clamp(ThinkingLevel::Low), Some(ThinkingLevel::High));
    assert_eq!(
        levels.clamp(ThinkingLevel::Medium),
        Some(ThinkingLevel::High)
    );
    // The two opt-in rungs are above everything supported, so they come back
    // down to HIGH.
    assert_eq!(levels.clamp(ThinkingLevel::Max), Some(ThinkingLevel::High));
    // A model that cannot stop thinking moves a request off, not away.
    assert_eq!(
        levels.clamp(ThinkingLevel::Off),
        Some(ThinkingLevel::Minimal)
    );
    // A supported rung is left alone.
    assert_eq!(levels.clamp(ThinkingLevel::High), Some(ThinkingLevel::High));
}

#[test]
fn a_ladder_narrowed_to_nothing_answers_nothing() {
    let levels = ThinkingLevels::from_entries(LADDER.map(|level| (level, None)));
    assert_eq!(levels.clamp(ThinkingLevel::Medium), None);
    assert!(plan(
        WireApi::OpenAiResponses,
        &levels,
        ThinkingLevel::Medium,
        1_000,
        4_000
    )
    .is_err());
}

#[test]
fn an_effort_family_sends_the_value_the_catalog_supplies() {
    let levels = narrowed();
    let planned = plan(
        WireApi::OpenAiResponses,
        &levels,
        ThinkingLevel::Medium,
        1_000,
        4_000,
    )
    .expect("plans");
    assert_eq!(
        planned,
        ThinkingPlan::Effort {
            level: ThinkingLevel::High,
            value: Some("high".to_owned()),
        }
    );
}

#[test]
fn a_budget_family_always_leaves_room_for_an_answer() {
    let levels = ThinkingLevels::adapter_defaults();
    // The ceiling is smaller than the thinking budget for this rung, so the
    // thinking phase gives way rather than the answer.
    let planned = plan(
        WireApi::AnthropicMessages,
        &levels,
        ThinkingLevel::High,
        defaults::RESERVED_ANSWER_TOKENS,
        defaults::RESERVED_ANSWER_TOKENS,
    )
    .expect("plans");
    assert_eq!(
        planned,
        ThinkingPlan::Budget {
            level: ThinkingLevel::High,
            thinking_tokens: 0,
            answer_tokens: defaults::RESERVED_ANSWER_TOKENS,
        }
    );

    // With room, the budget rides on top of the caller's own answer allowance.
    let planned = plan(
        WireApi::AnthropicMessages,
        &levels,
        ThinkingLevel::Medium,
        4_000,
        64_000,
    )
    .expect("plans");
    assert_eq!(
        planned,
        ThinkingPlan::Budget {
            level: ThinkingLevel::Medium,
            thinking_tokens: defaults::thinking_budget_tokens(ThinkingLevel::Medium),
            answer_tokens: 4_000,
        }
    );
}

#[test]
fn the_opt_in_rungs_settle_onto_the_top_published_budget() {
    let levels = ThinkingLevels::from_entries([
        (ThinkingLevel::Max, Some("max".to_owned())),
        (ThinkingLevel::High, Some("high".to_owned())),
    ]);
    let planned = plan(
        WireApi::AnthropicMessages,
        &levels,
        ThinkingLevel::Max,
        4_000,
        64_000,
    )
    .expect("plans");
    assert_eq!(
        planned,
        ThinkingPlan::Budget {
            level: ThinkingLevel::Max,
            thinking_tokens: defaults::thinking_budget_tokens(ThinkingLevel::High),
            answer_tokens: 4_000,
        }
    );
}

#[test]
fn turning_thinking_off_produces_no_control_at_all() {
    let levels = ThinkingLevels::adapter_defaults();
    assert_eq!(
        plan(
            WireApi::OpenAiResponses,
            &levels,
            ThinkingLevel::Off,
            1_000,
            4_000
        ),
        Ok(ThinkingPlan::Disabled)
    );
}

#[test]
fn every_class_that_cannot_be_retried_says_so_first() {
    for class in [
        ErrorClass::Auth,
        ErrorClass::Quota,
        ErrorClass::InvalidRequest,
        ErrorClass::Overflow,
        ErrorClass::Canceled,
        ErrorClass::Unknown,
    ] {
        assert_eq!(class.disposition(), RetryDisposition::Terminal, "{class:?}");
    }
    for class in [ErrorClass::Overloaded, ErrorClass::Network] {
        assert_eq!(
            class.disposition(),
            RetryDisposition::Retryable,
            "{class:?}"
        );
    }
    // Cancelled work is terminal and is never handed to another candidate.
    assert!(!ErrorClass::Canceled.permits_failover());
    assert!(!ErrorClass::InvalidRequest.permits_failover());
    assert!(ErrorClass::Quota.permits_failover());
}

struct FixedJitter(u32);

impl Jitter for FixedJitter {
    fn fraction_percent(&self, _attempt: u32) -> u32 {
        self.0
    }
}

#[test]
fn backoff_doubles_to_a_cap_and_jitter_only_subtracts() {
    assert_eq!(
        transport_backoff_millis(0, &NoJitter),
        defaults::TRANSPORT_BACKOFF_BASE_MILLIS
    );
    assert_eq!(
        transport_backoff_millis(1, &NoJitter),
        defaults::TRANSPORT_BACKOFF_BASE_MILLIS * 2
    );
    assert_eq!(
        transport_backoff_millis(20, &NoJitter),
        defaults::TRANSPORT_BACKOFF_CAP_MILLIS
    );
    let full = transport_backoff_millis(20, &FixedJitter(100));
    assert!(full < defaults::TRANSPORT_BACKOFF_CAP_MILLIS);
    assert_eq!(
        full,
        defaults::TRANSPORT_BACKOFF_CAP_MILLIS
            - defaults::TRANSPORT_BACKOFF_CAP_MILLIS
                * u64::from(defaults::TRANSPORT_BACKOFF_JITTER_PERCENT)
                / 100
    );
    // The same inputs give the same schedule every run.
    assert_eq!(
        transport_backoff_millis(3, &FixedJitter(40)),
        transport_backoff_millis(3, &FixedJitter(40))
    );
}

#[test]
fn a_delay_longer_than_the_cap_fails_fast_instead_of_waiting() {
    assert_eq!(
        honor_server_delay(1_000),
        ServerDelayVerdict::Wait { millis: 1_000 }
    );
    let requested = defaults::SERVER_RETRY_DELAY_CAP_MILLIS + 1;
    assert_eq!(
        honor_server_delay(requested),
        ServerDelayVerdict::FailFast {
            requested_millis: requested
        }
    );
    assert_eq!(
        verdict(ErrorClass::Overloaded, 0, Some(requested), false, &NoJitter),
        AttemptVerdict::Stop
    );
    assert_eq!(
        verdict(ErrorClass::Overloaded, 0, Some(requested), true, &NoJitter),
        AttemptVerdict::Failover
    );
}

#[test]
fn the_semantic_attempt_budget_runs_out() {
    assert!(semantic_backoff_millis(0).is_some());
    assert!(semantic_backoff_millis(defaults::SEMANTIC_RETRY_ATTEMPTS - 1).is_none());
    assert_eq!(
        verdict(
            ErrorClass::Network,
            defaults::SEMANTIC_RETRY_ATTEMPTS,
            None,
            false,
            &NoJitter
        ),
        AttemptVerdict::Stop
    );
}

#[test]
fn the_final_total_attempt_slot_reaches_a_disclosed_failover() {
    assert_eq!(
        verdict(ErrorClass::Overloaded, 0, None, true, &NoJitter),
        AttemptVerdict::RetryAfter { millis: 500 }
    );
    assert_eq!(
        verdict(ErrorClass::Overloaded, 1, None, true, &NoJitter),
        AttemptVerdict::Failover
    );
}

#[test]
fn a_request_ends_exactly_once() {
    let mut slot = TerminalSlot::new();
    assert!(!slot.is_terminated());
    let first = TerminalResult {
        request_id: RequestId::from_bytes([1; 16]),
        stop: StopReason::Complete,
        usage: TokenUsage::default(),
        provider_stop_reason: None,
        error: None,
    };
    let second = TerminalResult {
        request_id: RequestId::from_bytes([1; 16]),
        stop: StopReason::Error,
        usage: TokenUsage::default(),
        provider_stop_reason: None,
        error: None,
    };
    assert!(slot.complete(first.clone()).is_ok());
    assert!(slot.complete(second).is_err());
    assert_eq!(slot.result(), Some(&first));
}
