// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider-neutral request and result contracts.
//!
//! One request shape covers every provider, and one result taxonomy covers
//! every failure. Adapters translate at the edge; nothing above the adapter
//! reads a provider's own error string to make a decision.
//!
//! Three properties are built into the types rather than left to discipline:
//!
//! - **A stream ends exactly once.** [`TerminalSlot`] accepts one terminal
//!   result and refuses a second, so a transport failure after a provider error
//!   cannot produce two endings, and a request cannot end zero times.
//! - **Errors are classified, not quoted.** [`ErrorClass`] is exhaustive and
//!   [`ErrorClass::disposition`] answers retryability, so retry, routing, and
//!   surface decisions consume the taxonomy. The provider's own words survive
//!   in a size-bounded field for diagnosis only.
//! - **Context is described, not carried.** [`ContextManifest`] holds
//!   identifiers, classes, and counts. This crate never holds page content, so
//!   it cannot leak any.

use crate::catalog::ModelRole;
use crate::cost::TokenUsage;
use crate::ids::{ModelKey, RequestId, SourceId, TaskId};
use crate::thinking::ThinkingPlan;
use crate::time::MonotonicMillis;

/// Longest provider detail retained for diagnosis, in bytes.
pub const MAX_PROVIDER_DETAIL_LEN: usize = 512;

/// A size-bounded string.
///
/// Provider payloads are attacker-influenced and unbounded; a diagnostic field
/// that stores them verbatim is a memory and a redaction problem at once.
#[derive(
    Clone,
    Debug,
    Default,
    PartialEq,
    Eq,
    PartialOrd,
    Ord,
    Hash,
    serde::Serialize,
    serde::Deserialize,
)]
#[serde(transparent)]
pub struct BoundedText(String);

impl BoundedText {
    /// Truncates to the bound on a character boundary.
    pub fn new(raw: &str) -> Self {
        if raw.len() <= MAX_PROVIDER_DETAIL_LEN {
            return Self(raw.to_owned());
        }
        let mut end = MAX_PROVIDER_DETAIL_LEN;
        while end > 0 && !raw.is_char_boundary(end) {
            end -= 1;
        }
        Self(raw.get(..end).unwrap_or_default().to_owned())
    }

    /// The text.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// How sensitive the material behind a request is.
///
/// The policy engine owns classification and every decision that follows from
/// it. This enumeration exists so the router can enforce exactly one rule it
/// must never delegate: prohibited material is not transmittable to any model,
/// on any route, ever.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DataSensitivity {
    /// Public material.
    Public,
    /// Material about a person.
    Personal,
    /// Material whose exposure could cause meaningful harm.
    Sensitive,
    /// Material whose exposure could cause serious harm.
    HighlySensitive,
    /// Material that is neither model-transmittable nor persistable.
    Prohibited,
}

/// What a request is for.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum RequestPurpose {
    /// Producing or revising a plan.
    Planning,
    /// Pulling structure out of an observation.
    Extraction,
    /// Turning accepted facts into a result.
    Synthesis,
    /// Checking a result against its support.
    Verification,
    /// Producing a retrieval vector.
    Embedding,
}

/// What the request is about, without any of it.
#[derive(Clone, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct ContextManifest {
    /// Which sources contributed.
    pub source_ids: Vec<SourceId>,
    /// The classes present.
    pub classes: Vec<DataSensitivity>,
    /// How many items were assembled.
    pub item_count: u32,
    /// Expected input size, for estimating and for overflow checks.
    pub estimated_input_tokens: u64,
}

impl ContextManifest {
    /// The strictest class present.
    pub fn highest_class(&self) -> Option<DataSensitivity> {
        self.classes.iter().copied().max()
    }

    /// Whether any material is prohibited from leaving the device.
    pub fn contains_prohibited(&self) -> bool {
        self.classes.contains(&DataSensitivity::Prohibited)
    }
}

/// How long a provider may retain a prompt cache entry.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum CacheRetention {
    /// No cache markers.
    None,
    /// The provider's short retention class.
    Short,
    /// The provider's long retention class, which is priced differently.
    Long,
}

/// One request, as the model router builds it.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct ModelRequest {
    /// Identifies this request across retry, cancellation, and audit.
    pub request_id: RequestId,
    /// The task it belongs to.
    pub task_id: TaskId,
    /// What it is for.
    pub purpose: RequestPurpose,
    /// The role it was routed as.
    pub role: ModelRole,
    /// The model it targets.
    pub model: ModelKey,
    /// The thinking control, already clamped to what the model supports.
    pub thinking: ThinkingPlan,
    /// Tokens the answer may use.
    pub answer_tokens: u64,
    /// The cache class requested.
    pub cache_retention: CacheRetention,
    /// What the request is about.
    pub context: ContextManifest,
    /// When the request stops being worth finishing.
    pub deadline: MonotonicMillis,
}

/// The exhaustive failure taxonomy.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ErrorClass {
    /// The credential was rejected.
    Auth,
    /// A quota, rate limit, or billing state stopped the request.
    Quota,
    /// The provider is temporarily unable to serve it.
    Overloaded,
    /// The request itself is wrong and will stay wrong.
    InvalidRequest,
    /// The request did not reach the provider, or the response did not return.
    Network,
    /// The context did not fit.
    Overflow,
    /// The caller cancelled.
    Canceled,
    /// Nothing above matched.
    Unknown,
}

/// Whether a class is worth another attempt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RetryDisposition {
    /// Retrying cannot help.
    Terminal,
    /// Retrying may help, inside the task's budgets.
    Retryable,
}

impl ErrorClass {
    /// What retry should do about this class.
    ///
    /// Non-retryable classes are answered first and unconditionally. A quota or
    /// billing failure that is retried is a failure that spends money to fail
    /// again, and cancelled work that is retried is work the user stopped.
    pub fn disposition(self) -> RetryDisposition {
        match self {
            Self::Auth
            | Self::Quota
            | Self::InvalidRequest
            | Self::Overflow
            | Self::Canceled
            | Self::Unknown => RetryDisposition::Terminal,
            Self::Overloaded | Self::Network => RetryDisposition::Retryable,
        }
    }

    /// Whether this class means the next candidate should be tried.
    ///
    /// Failover answers a provider that cannot serve the request at all. It
    /// does not answer a malformed request, a cancelled one, or one the user
    /// has to fix, and it never crosses a disclosure class.
    pub fn permits_failover(self) -> bool {
        matches!(
            self,
            Self::Auth | Self::Quota | Self::Overloaded | Self::Network
        )
    }
}

/// A normalized provider failure.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct ProviderError {
    /// The class every decision is made from.
    pub class: ErrorClass,
    /// A delay the provider asked for.
    pub retry_after_millis: Option<u64>,
    /// The provider's own words, bounded, for diagnosis only.
    pub detail: BoundedText,
}

/// Why generation stopped.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum StopReason {
    /// The model finished.
    Complete,
    /// The model asked to call a tool.
    ToolCall,
    /// The output allowance ran out.
    Length,
    /// The provider stopped for a policy of its own.
    ProviderStop,
    /// The request failed; see the error.
    Error,
}

/// The single ending of one request.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct TerminalResult {
    /// Which request ended.
    pub request_id: RequestId,
    /// Why it ended.
    pub stop: StopReason,
    /// What it consumed, whether it succeeded or not.
    pub usage: TokenUsage,
    /// The provider's own stop token, preserved for diagnosis.
    pub provider_stop_reason: Option<BoundedText>,
    /// The failure, when there was one.
    pub error: Option<ProviderError>,
}

impl TerminalResult {
    /// Whether the request produced a usable result.
    pub fn is_success(&self) -> bool {
        self.error.is_none() && self.stop != StopReason::Error
    }
}

/// Attempted to end a request that had already ended.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AlreadyTerminated;

impl core::fmt::Display for AlreadyTerminated {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str("the request already produced a terminal result")
    }
}

/// Holds the one ending a request is allowed.
///
/// Request, transport, and provider failures after invocation all arrive here,
/// and the first one wins. A second is refused rather than overwriting the
/// first, because the first is what the caller already acted on.
#[derive(Debug, Default)]
pub struct TerminalSlot {
    result: Option<TerminalResult>,
}

impl TerminalSlot {
    /// An empty slot.
    pub fn new() -> Self {
        Self { result: None }
    }

    /// Records the ending, or refuses if one is already recorded.
    pub fn complete(&mut self, result: TerminalResult) -> Result<(), AlreadyTerminated> {
        if self.result.is_some() {
            return Err(AlreadyTerminated);
        }
        self.result = Some(result);
        Ok(())
    }

    /// Whether the request has ended.
    pub fn is_terminated(&self) -> bool {
        self.result.is_some()
    }

    /// The ending, if there is one.
    pub fn result(&self) -> Option<&TerminalResult> {
        self.result.as_ref()
    }

    /// Takes the ending out.
    pub fn take(self) -> Option<TerminalResult> {
        self.result
    }
}

/// How an overflow was noticed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OverflowKind {
    /// The provider said so.
    ProviderReported,
    /// The call succeeded but reported more input than the window holds.
    Silent,
    /// The call stopped on length with no output at a full window.
    Truncation,
}

/// Notices the three ways a context overflow shows up.
///
/// Only the first is an error the provider volunteered. The other two are
/// successes that are not: a request that reports impossible input, and one
/// that fills the window and returns nothing. Both would otherwise read as a
/// model that simply had nothing to say.
pub fn detect_overflow(
    result: &TerminalResult,
    context_window: u64,
    reported_input_tokens: u64,
) -> Option<OverflowKind> {
    if result
        .error
        .as_ref()
        .is_some_and(|error| error.class == ErrorClass::Overflow)
    {
        return Some(OverflowKind::ProviderReported);
    }
    if reported_input_tokens > context_window {
        return Some(OverflowKind::Silent);
    }
    if result.stop == StopReason::Length
        && result.usage.output == 0
        && reported_input_tokens >= context_window
    {
        return Some(OverflowKind::Truncation);
    }
    None
}

/// A stop the caller may recover from once, inside its budget.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RecoverableSignal {
    /// The context did not fit; compacting and retrying once is permitted.
    CompactAndRetry(OverflowKind),
    /// The answer was cut short below the caller's own limit.
    AnswerTruncated,
}

/// Reads a result for a recoverable signal.
pub fn recoverable_signal(
    result: &TerminalResult,
    context_window: u64,
    reported_input_tokens: u64,
    requested_answer_tokens: u64,
) -> Option<RecoverableSignal> {
    if let Some(kind) = detect_overflow(result, context_window, reported_input_tokens) {
        return Some(RecoverableSignal::CompactAndRetry(kind));
    }
    if result.stop == StopReason::Length && result.usage.output < requested_answer_tokens {
        return Some(RecoverableSignal::AnswerTruncated);
    }
    None
}
