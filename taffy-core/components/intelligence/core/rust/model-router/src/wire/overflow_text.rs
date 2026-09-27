// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reading a context overflow out of a provider's own sentence.
//!
//! A family's error vocabulary maps a *token* — `invalid_request_error`,
//! `context_length_exceeded` — onto a class, and where a provider sends one
//! that is the end of it. Many do not. The OpenAI-compatible shape is spoken by
//! aggregators and by servers a person runs themselves, and several of them
//! report a context overflow as an ordinary invalid request, or as no token at
//! all, with the actual reason only in the message.
//!
//! That difference decides whether a task survives. An overflow is the one
//! failure the loop can act on: [`crate::request::detect_overflow`] turns it
//! into a compaction and one more attempt. Left unclassified it becomes
//! [`ErrorClass::Unknown`], which is terminal by design — so a transcript that
//! grew one turn too long ends the task instead of being trimmed, and the
//! person is told nothing that explains it.
//!
//! The table is therefore matched against the provider's sentence, and only
//! ever to *raise* a verdict the token vocabulary did not reach. A provider
//! that named its failure is believed; this is for the ones that did not.

use crate::request::ErrorClass;

/// Sentences that mean the context did not fit.
///
/// Lower-cased substrings, because every one of these is quoted from a
/// different server and they agree on nothing but the words. Ordered roughly by
/// how often they are seen rather than alphabetically, since the first match
/// wins and the common ones should be cheap.
#[rustfmt::skip]
const OVERFLOW: &[&str] = &[
    // The OpenAI shape, and the aggregators that copy its wording.
    "context_length_exceeded",
    "maximum context length",
    "reduce the length of the messages",
    "reduce your prompt",
    // Anthropic and the gateways in front of it.
    "prompt is too long",
    "input length and `max_tokens` exceed context limit",
    // Google.
    "the input token count",
    "exceeds the maximum number of tokens allowed",
    // vLLM and SGLang, which a person may be running themselves.
    "maximum context length is",
    "please reduce the length",
    "longer than the maximum model length",
    // llama.cpp and its wrappers.
    "exceeds the available context size",
    "exceed context window of",
    "the request exceeds the available context size",
    // Ollama.
    "context window is full",
    "requested tokens exceed context window",
    // LM Studio.
    "the number of tokens to keep",
    "context length exceeded",
    "trying to keep the first",
    // Aggregators and hosted runtimes with their own phrasing.
    "input is too long",
    "input tokens exceed",
    "too many tokens",
    "token limit exceeded",
    "max_tokens is too large",
    "string too long",
];

/// Sentences that mean a quota or a rate limit, not a broken request.
///
/// Kept beside the overflow table for the same reason: a provider that answers
/// with prose and no token would otherwise have its rate limit read as
/// [`ErrorClass::Unknown`] and stop the task terminally, when waiting would
/// have worked. Quota itself is terminal — the point is to say the true reason
/// rather than to retry it.
#[rustfmt::skip]
const QUOTA: &[&str] = &[
    "rate limit",
    "rate_limit",
    "too many requests",
    "quota exceeded",
    "quota_exceeded",
    "insufficient_quota",
    "insufficient credit",
    "insufficient balance",
    "billing",
    "payment required",
    "exceeded your current quota",
    "spending limit",
    // A subscription's own window rather than a metered balance. These are
    // what a plan-backed endpoint says when the plan has run out, and the
    // metered vocabularies above do not contain a single one of them — so
    // every subscription that reached its ceiling ended the task terminally
    // instead of waiting.
    "usage limit",
    "usage_limit_reached",
    "plan limit",
    "monthly limit",
    "weekly limit",
    "you have reached your",
    "not included in your plan",
    "upgrade your plan",
    // The gRPC-shaped services and the aggregators in front of them.
    "resource_exhausted",
    "resource has been exhausted",
    "credit balance is too low",
    "out of credits",
    "requests per minute",
    "tokens per minute",
    "concurrency limit",
];

/// Sentences that mean the provider is temporarily unable, not wrong.
#[rustfmt::skip]
const OVERLOADED: &[&str] = &[
    "overloaded",
    "server is busy",
    "temporarily unavailable",
    "service unavailable",
    "capacity",
    "try again later",
    "model is currently loading",
    // What a server that fell over says, as opposed to one that read the
    // request and disagreed with it. Each of these was `Unknown` — terminal —
    // and a task that could have been retried ended instead.
    "internal server error",
    "internal error",
    "bad gateway",
    "gateway timeout",
    "upstream connect error",
    "connection reset",
    "no healthy upstream",
    "server had an error",
    "model is loading",
    "model is warming up",
    "queue is full",
    "server is starting",
];

fn matches(haystack: &str, needles: &[&str]) -> bool {
    needles.iter().any(|needle| haystack.contains(needle))
}

/// What a provider's sentence says, when its token said nothing.
///
/// Returns `None` when the message carries no recognizable reason, which leaves
/// the caller's own verdict standing.
pub fn classify(message: &str) -> Option<ErrorClass> {
    if message.is_empty() {
        return None;
    }
    let lowered = message.to_lowercase();
    if matches(&lowered, OVERFLOW) {
        return Some(ErrorClass::Overflow);
    }
    if matches(&lowered, QUOTA) {
        return Some(ErrorClass::Quota);
    }
    if matches(&lowered, OVERLOADED) {
        return Some(ErrorClass::Overloaded);
    }
    None
}

/// A delay the provider asked for in words rather than in a header.
///
/// Transport headers are the browser's to read; this is the other half, for the
/// providers that put the number in the sentence and nowhere else. Only whole
/// seconds and milliseconds are understood, because those are the two forms
/// seen in the wild and guessing at a third would produce a wait derived from
/// nothing.
pub fn retry_after_millis(message: &str) -> Option<u64> {
    let lowered = message.to_lowercase();
    let start = lowered.find("try again in")?;
    let rest = lowered.get(start.saturating_add("try again in".len())..)?;
    let trimmed = rest.trim_start();
    let digits: String = trimmed
        .chars()
        .take_while(|character| character.is_ascii_digit() || *character == '.')
        .collect();
    if digits.is_empty() {
        return None;
    }
    let unit = trimmed.get(digits.len()..)?.trim_start();
    // Whole and fractional parts are read separately and kept in integers. A
    // float would be the obvious way and would round a delay the provider
    // stated exactly, which is the one number here that should not drift.
    let (whole_text, fraction_text) = match digits.split_once('.') {
        Some((whole, fraction)) => (whole, fraction),
        None => (digits.as_str(), ""),
    };
    let whole: u64 = if whole_text.is_empty() {
        0
    } else {
        whole_text.parse().ok()?
    };
    let millis = if unit.starts_with("ms") {
        whole
    } else if unit.starts_with('s') {
        // Three digits is a millisecond; anything the provider wrote past that
        // is below the resolution the answer is carried in.
        let mut thousandths = 0u64;
        for (place, character) in fraction_text.chars().take(3).enumerate() {
            let digit = u64::from(character.to_digit(10)?);
            let scale = match place {
                0 => 100,
                1 => 10,
                _ => 1,
            };
            thousandths = thousandths.saturating_add(digit.saturating_mul(scale));
        }
        whole.saturating_mul(1000).saturating_add(thousandths)
    } else {
        return None;
    };
    // A provider asking for longer than an hour is asking for something no task
    // is going to wait through, and the retry ceiling would refuse it anyway.
    Some(millis.min(3_600_000))
}

#[cfg(test)]
mod tests {
    use super::{classify, retry_after_millis};
    use crate::request::ErrorClass;

    #[test]
    fn every_overflow_sentence_is_read_as_an_overflow() {
        for message in [
            "This model's maximum context length is 128000 tokens, however you requested 130000",
            "prompt is too long: 210000 tokens > 200000 maximum",
            "ValueError: This model's maximum context length is 32768 tokens",
            "the request exceeds the available context size, try increasing it",
            "Requested tokens exceed context window of 8192",
            "context length exceeded: trying to keep the first 4096 tokens",
            "The input token count exceeds the maximum number of tokens allowed",
        ] {
            assert_eq!(classify(message), Some(ErrorClass::Overflow), "{message}");
        }
    }

    #[test]
    fn a_quota_sentence_is_not_read_as_an_overflow() {
        assert_eq!(
            classify("Rate limit reached for gpt-4o in organization org-1"),
            Some(ErrorClass::Quota)
        );
        assert_eq!(
            classify("You exceeded your current quota, please check your plan"),
            Some(ErrorClass::Quota)
        );
    }

    #[test]
    fn a_plan_that_has_run_out_is_a_quota_rather_than_a_broken_request() {
        // Every one of these was terminal before its wording was listed: the
        // task ended, and what the person was told named nothing they could
        // act on.
        for message in [
            "You've hit your usage limit. Your limit resets at 4:00 PM.",
            "usage_limit_reached: plan quota for gpt-5.1-codex is spent",
            "You have reached your monthly limit for this model",
            "This model is not included in your plan; upgrade your plan to use it",
            "Your credit balance is too low to access the Anthropic API",
            "Requests per minute limit reached for this project",
            "Resource has been exhausted (e.g. check quota)",
        ] {
            assert_eq!(classify(message), Some(ErrorClass::Quota), "{message}");
        }
    }

    #[test]
    fn a_server_that_fell_over_is_told_apart_from_one_that_disagreed() {
        for message in [
            "Internal server error",
            "502 Bad Gateway",
            "upstream connect error or disconnect/reset before headers",
            "no healthy upstream",
            "The server had an error while processing your request",
            "model is loading, please wait",
            "the queue is full, try a smaller batch",
        ] {
            assert_eq!(classify(message), Some(ErrorClass::Overloaded), "{message}");
        }
    }

    #[test]
    fn an_overloaded_sentence_says_so() {
        assert_eq!(
            classify("The server is busy, please try again later"),
            // "try again later" is an overloaded sentence, but "server is busy"
            // is matched first and reaches the same class.
            Some(ErrorClass::Overloaded)
        );
    }

    #[test]
    fn an_ordinary_sentence_leaves_the_verdict_alone() {
        assert_eq!(classify("model not found"), None);
        assert_eq!(classify(""), None);
        assert_eq!(classify("tool call arguments were not valid json"), None);
    }

    #[test]
    fn a_delay_in_words_is_read_in_both_units() {
        assert_eq!(
            retry_after_millis("Rate limit reached. Please try again in 20s."),
            Some(20_000)
        );
        assert_eq!(retry_after_millis("Please try again in 480ms"), Some(480));
        assert_eq!(retry_after_millis("Please try again in 1.5s"), Some(1_500));
    }

    #[test]
    fn a_delay_that_is_not_there_is_not_invented() {
        assert_eq!(retry_after_millis("Rate limit reached."), None);
        assert_eq!(retry_after_millis("try again in a little while"), None);
        assert_eq!(retry_after_millis("try again in 30 minutes"), None);
    }

    #[test]
    fn an_absurd_delay_is_capped_rather_than_wrapped() {
        assert_eq!(
            retry_after_millis("try again in 99999999s"),
            Some(3_600_000)
        );
    }
}
