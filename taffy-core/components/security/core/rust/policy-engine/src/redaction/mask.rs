// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The value masker and the URL splitter.
//!
//! Field selection decides what may be carried. This decides what survives
//! inside what is carried, because a secret does not always land in a field
//! anybody classified — somebody pastes a token into a search box, or a session
//! value rides in a query string. Both layers run, and this one runs on every
//! string that leaves the local observation.
//!
//! Work is bounded at a trust boundary (threat model invariant I-12): text is
//! truncated to a fixed number of characters and a fixed number of tokens, and
//! anything past the bound is dropped rather than passed through unexamined.

use crate::origin::{normalize_serialization, NormalizedOrigin, OriginError};

/// How much text one projected field may carry.
///
/// A conservative in-crate bound, not a ratified protocol limit: the numeric
/// node, byte, and message limits are `[Open (OD-031)]` and are established on
/// the device and page corpus. Truncation is a redaction step of its own — an
/// unbounded string is an unbounded disclosure.
pub(crate) const MAX_PROJECTED_CHARS: usize = 512;

/// How many whitespace-separated tokens the masker inspects in one string.
///
/// Bounded work at a trust boundary (threat model invariant I-12). Anything
/// past the bound is dropped rather than passed through unexamined.
const MAX_MASKED_TOKENS: usize = 256;

/// The fixed marker a masked span is replaced with.
///
/// A compiled-in local template. It is the same length whatever it replaced, so
/// it carries no length-derived fingerprint of the value.
pub const MASK_MARKER: &str = "[redacted]";

/// Words that mark a value as a secret however it is shaped.
const SECRET_MARKERS: &[&str] = &[
    "password",
    "passcode",
    "passphrase",
    "secret",
    "token",
    "apikey",
    "api_key",
    "cvv",
    "cvc",
    "otp",
    "one-time",
    "onetime",
    "seedphrase",
    "privatekey",
    "private_key",
    "recoverycode",
    "sessionid",
    "bearer",
];

/// Whether one token looks like a secret.
///
/// Three independent shapes, because a secret does not have one shape: a long
/// run of digits, a long mixed-class string, or an explicit marker word. It is
/// deliberately conservative — masking an innocent token costs a little
/// context, and carrying a secret costs everything.
pub(crate) fn is_secret_shaped(token: &str) -> bool {
    if token.chars().filter(char::is_ascii_digit).count() >= 8 {
        return true;
    }
    let lowered = token.to_ascii_lowercase();
    if SECRET_MARKERS.iter().any(|marker| lowered.contains(marker)) {
        return true;
    }
    // A long token that mixes letters and digits. Case mixing is deliberately
    // not required: a recovery code, a licence key, and a one-time credential
    // are routinely printed in one case, and requiring two would let the
    // commonest shape of a printed secret through.
    let alphanumeric = token.chars().filter(char::is_ascii_alphanumeric).count();
    alphanumeric >= 16
        && token
            .chars()
            .any(|character| character.is_ascii_alphabetic())
        && token.chars().any(|character| character.is_ascii_digit())
}

/// The first `limit` characters of `text`.
pub(crate) fn truncate_chars(text: &str, limit: usize) -> &str {
    match text.char_indices().nth(limit) {
        None => text,
        Some((end, _)) => text.get(..end).unwrap_or(""),
    }
}

/// What a masking pass did to one string.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MaskedText {
    /// The text with every secret-shaped token replaced.
    pub text: String,
    /// How many tokens were replaced.
    pub masked_spans: u64,
    /// Whether the text was cut short by the bound.
    pub truncated: bool,
}

/// Replaces every secret-shaped token in `text`.
///
/// Whitespace is normalized, because the output is a redacted projection rather
/// than a transcript. Work is bounded: past [`MAX_MASKED_TOKENS`] the remainder
/// is dropped, never passed through unexamined.
pub fn mask_secret_shaped(text: &str) -> MaskedText {
    let bounded = truncate_chars(text, MAX_PROJECTED_CHARS);
    let mut masked_spans = 0;
    let mut kept: Vec<&str> = Vec::new();
    let mut numeric_run: Vec<&str> = Vec::new();
    let mut numeric_digits = 0;
    let mut dropped = bounded.len() < text.len();

    for (index, token) in bounded.split_whitespace().enumerate() {
        if index >= MAX_MASKED_TOKENS {
            dropped = true;
            break;
        }
        if let Some(digits) = numeric_fragment_digits(token) {
            numeric_run.push(token);
            numeric_digits += digits;
            continue;
        }
        flush_numeric_run(
            &mut numeric_run,
            &mut numeric_digits,
            &mut kept,
            &mut masked_spans,
        );
        if is_secret_shaped(token) {
            masked_spans += 1;
            kept.push(MASK_MARKER);
        } else {
            kept.push(token);
        }
    }
    flush_numeric_run(
        &mut numeric_run,
        &mut numeric_digits,
        &mut kept,
        &mut masked_spans,
    );

    MaskedText {
        text: kept.join(" "),
        masked_spans,
        truncated: dropped || bounded.len() < text.len(),
    }
}

/// The digit count when a token is one fragment of a grouped numeric value.
fn numeric_fragment_digits(token: &str) -> Option<usize> {
    let mut digits = 0;
    for character in token.chars() {
        if character.is_ascii_digit() {
            digits += 1;
        } else if !matches!(
            character,
            '+' | '-' | '(' | ')' | '[' | ']' | '.' | ',' | '/'
        ) {
            return None;
        }
    }
    (digits > 0).then_some(digits)
}

/// Emits or masks one whitespace-separated run of numeric fragments.
fn flush_numeric_run<'a>(
    run: &mut Vec<&'a str>,
    digits: &mut usize,
    kept: &mut Vec<&'a str>,
    masked_spans: &mut u64,
) {
    if *digits >= 8 {
        kept.push(MASK_MARKER);
        *masked_spans += 1;
        run.clear();
    } else {
        kept.append(run);
    }
    *digits = 0;
}

/// A URL split into the parts a destination policy can choose between.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UrlParts {
    /// The normalized origin.
    pub origin: NormalizedOrigin,
    /// The path, when there is one.
    pub path: Option<String>,
    /// Whether a query string was present. Reported even when withheld.
    pub has_query: bool,
    /// Whether a fragment was present. Reported even when withheld.
    pub has_fragment: bool,
}

/// Splits a committed URL into origin, path, and the presence of a query and a
/// fragment.
///
/// The query and the fragment are never returned. They routinely carry
/// searches, document identifiers, email addresses, and session tokens, and no
/// destination narrower than the local observation is allowed to see them, so
/// this function does not offer them at all.
pub fn split_url(url: &str) -> Result<UrlParts, OriginError> {
    let (scheme, rest) = url
        .split_once("://")
        .ok_or(OriginError::MalformedSerialization)?;
    let (authority, tail) = match rest.find(['/', '?', '#']) {
        // `find` with a character pattern returns a character boundary.
        Some(index) => rest.split_at(index),
        None => (rest, ""),
    };
    let origin = normalize_serialization(&format!("{scheme}://{authority}"))?;

    let has_fragment = tail.contains('#');
    let before_fragment = tail.split('#').next().unwrap_or("");
    let has_query = before_fragment.contains('?');
    let path = before_fragment.split('?').next().unwrap_or("");
    let path = if path.is_empty() || path == "/" {
        None
    } else {
        Some(truncate_chars(path, MAX_PROJECTED_CHARS).to_owned())
    };

    Ok(UrlParts {
        origin,
        path,
        has_query,
        has_fragment,
    })
}
