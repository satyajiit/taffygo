// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The tuning defaults of the model-access layer, in one place.
//!
//! The provider registry and model catalog document owns these values; this
//! module is the only place the crate spells them, so a revision there is a
//! one-line change here and nowhere else. None of them is a quality target —
//! those live in the metric registry and are not restated in code.
//!
//! Every value is a bound on work, not a promise about it. Each is confirmed or
//! revised by the credential-and-quota spike and the first workflow benchmark.

use crate::thinking::ThinkingLevel;

/// Thinking token budget for a budget-mapped protocol family.
///
/// `OFF` spends nothing. The two opt-in levels above `HIGH` have no budget of
/// their own: a budget-mapped family clamps them to `HIGH` rather than
/// inventing a larger number the provider never published.
pub fn thinking_budget_tokens(level: ThinkingLevel) -> u64 {
    match level {
        ThinkingLevel::Off => 0,
        ThinkingLevel::Minimal => 1_024,
        ThinkingLevel::Low => 2_048,
        ThinkingLevel::Medium => 8_192,
        ThinkingLevel::High | ThinkingLevel::XHigh | ThinkingLevel::Max => 16_384,
    }
}

/// Answer allowance held back when thinking and answer share one ceiling.
///
/// An uncapped thinking phase that consumes the whole response returns neither
/// an answer nor a tool call, which reads to the caller as a silent failure.
/// Reserving this much makes that outcome impossible rather than unlikely.
pub const RESERVED_ANSWER_TOKENS: u64 = 1_024;

/// Refresh an authorization token when less than this much validity remains.
pub const OAUTH_VALIDITY_FLOOR_MILLIS: u64 = 5 * 60 * 1_000;

/// Hard timeout on one token refresh.
pub const OAUTH_REFRESH_TIMEOUT_MILLIS: u64 = 15 * 1_000;

/// First transport backoff delay, before jitter.
pub const TRANSPORT_BACKOFF_BASE_MILLIS: u64 = 500;

/// Ceiling on the doubled transport backoff delay, before jitter.
pub const TRANSPORT_BACKOFF_CAP_MILLIS: u64 = 8 * 1_000;

/// Largest fraction of a backoff delay that jitter may subtract, in percent.
pub const TRANSPORT_BACKOFF_JITTER_PERCENT: u32 = 25;

/// Longest server-requested delay that is worth waiting for.
///
/// Beyond this the request fails fast: a caller that hangs on a server's own
/// retry hint has turned a slow provider into an unresponsive product.
pub const SERVER_RETRY_DELAY_CAP_MILLIS: u64 = 60 * 1_000;

/// Attempts a semantic retry may make, counting the first.
pub const SEMANTIC_RETRY_ATTEMPTS: u32 = 3;

/// Most substitute models retained for one logical turn.
///
/// A turn may make at most [`SEMANTIC_RETRY_ATTEMPTS`] paid attempts total,
/// so retaining more than the remaining slots can never affect a verdict and
/// only copies endpoints, disclosures, and credential metadata.
pub const MAX_FAILOVER_CANDIDATES: usize = SEMANTIC_RETRY_ATTEMPTS as usize - 1;

/// First semantic retry delay.
pub const SEMANTIC_RETRY_BASE_MILLIS: u64 = 2 * 1_000;

/// Shortest interval between two remote catalog refreshes.
pub const CATALOG_REFRESH_INTERVAL_MILLIS: u64 = 24 * 60 * 60 * 1_000;
