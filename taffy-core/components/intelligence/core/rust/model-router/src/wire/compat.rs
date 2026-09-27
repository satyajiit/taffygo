// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a server a person runs themselves needs said differently.
//!
//! The completions dialect is one shape with a dozen small disagreements, and
//! almost all of them are between the platform that defined it and the servers
//! that implement it. A server answers what it is when it is first reached,
//! and that answer — one of five — is the whole input to this module: a pure
//! function from a detected kind to the overrides that kind needs, with no
//! host name, no URL, and no clock anywhere in it.
//!
//! It is deliberately not the last word. [`ServerCompat::with_catalog`] lays a
//! model's own `compat` map over the detected base, so a server that disagrees
//! in a way nobody has met yet is a catalog edit rather than a release — which
//! is what [`Model::compat`][crate::catalog::Model::compat] is for. The
//! detected base is what a device has before any catalog says anything, and it
//! is what keeps a first request from being a 400 nobody can read.

use std::collections::BTreeMap;

/// What a person's own endpoint turned out to be.
///
/// The same five members the core-service contract's `ServerKind` carries,
/// spelled here because this crate reads no contract: the detection is the
/// browser's, and what arrives is one of these or nothing. Closed, with no
/// catch-all — a sixth server is one this build has no overrides for, and
/// guessing at a nearest match is how a body reaches a server that refuses it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum ServerKind {
    /// A server that speaks the dialect as the platform defined it.
    OpenAiCompatible,
    /// Ollama's compatibility endpoint.
    Ollama,
    /// LM Studio's compatibility endpoint.
    LmStudio,
    /// vLLM's compatibility endpoint.
    Vllm,
    /// llama.cpp's compatibility endpoint.
    LlamaCpp,
}

/// The overrides one server needs.
///
/// Borrowed rather than owned, like everything else in `wire`: the two lists
/// are slices the caller holds, so a compat table assembled from a catalog map
/// never becomes a value this crate owns.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ServerCompat<'a> {
    /// The field this server takes the answer allowance in.
    ///
    /// The platform renamed it and the servers that implement the dialect did
    /// not follow. Sent under the name a server does not know, the allowance
    /// is ignored — so the answer runs to whatever the server's own default
    /// is, which is a truncation nothing in the reply explains.
    pub answer_tokens_key: &'a str,
    /// Whether the server says why the answer stopped.
    ///
    /// A reply with no stop word is refused rather than read, because a
    /// truncated answer read as a finished one is a wrong result nobody sees.
    /// On a server that never sends one that refusal is every reply, so the
    /// fact has to be knowable before the first one arrives.
    pub reports_finish_reason: bool,
    /// Whether the server understands a named reasoning effort.
    ///
    /// A server that does not may reject the whole request for the unknown
    /// field rather than ignoring it.
    pub accepts_reasoning_effort: bool,
    /// Extra arguments this server's chat template needs, in order.
    pub template_kwargs: &'a [(&'a str, &'a str)],
    /// Sampling parameters written at the top of the body, in order.
    pub sampling: &'a [(&'a str, &'a str)],
}

/// The dialect as the platform defines it, which is the base every row moves
/// from.
const PLATFORM: ServerCompat<'static> = ServerCompat {
    answer_tokens_key: "max_completion_tokens",
    reports_finish_reason: true,
    accepts_reasoning_effort: true,
    template_kwargs: &[],
    sampling: &[],
};

/// What one detected server needs.
///
/// A total function over the closed set, matched exhaustively with no
/// catch-all so that a server added to [`ServerKind`] without a row here fails
/// to compile rather than inheriting whichever row was written first.
///
/// The four self-hosted rows differ from the platform in the same two ways and
/// for the same reason: they implement the dialect as it was when they adopted
/// it. They take the allowance under its original name, and they do not
/// understand a named reasoning effort — a field they may refuse the whole
/// request over rather than ignore. Nothing here claims a server fails to
/// report a stop word; that column is `true` for all five and is moved by a
/// catalog entry, because it is the one fact that would otherwise have to be
/// learned from a reply that has already been paid for.
pub const fn detect(kind: ServerKind) -> ServerCompat<'static> {
    match kind {
        ServerKind::OpenAiCompatible => PLATFORM,
        ServerKind::Ollama | ServerKind::LmStudio | ServerKind::Vllm | ServerKind::LlamaCpp => {
            ServerCompat {
                answer_tokens_key: "max_tokens",
                accepts_reasoning_effort: false,
                ..PLATFORM
            }
        }
    }
}

/// The catalog key each scalar override is read from.
const ANSWER_TOKENS_FIELD: &str = "answer_tokens_field";
const REPORTS_FINISH_REASON: &str = "reports_finish_reason";
const ACCEPTS_REASONING_EFFORT: &str = "accepts_reasoning_effort";

impl<'a> ServerCompat<'a> {
    /// The detected base with a model's own `compat` map laid over it.
    ///
    /// Only the three scalars, and only where the map says something this
    /// module knows: an unrecognized key is left to whoever else reads the
    /// map, and a value that is neither `true` nor `false` leaves the detected
    /// answer standing rather than being read as one of them. The two lists
    /// stay the caller's, because a slice cannot be built from a map without
    /// owning what it points at.
    #[must_use]
    pub fn with_catalog(self, overrides: &'a BTreeMap<String, String>) -> Self {
        Self {
            answer_tokens_key: overrides
                .get(ANSWER_TOKENS_FIELD)
                .map_or(self.answer_tokens_key, String::as_str),
            reports_finish_reason: flag(overrides, REPORTS_FINISH_REASON)
                .unwrap_or(self.reports_finish_reason),
            accepts_reasoning_effort: flag(overrides, ACCEPTS_REASONING_EFFORT)
                .unwrap_or(self.accepts_reasoning_effort),
            ..self
        }
    }
}

fn flag(overrides: &BTreeMap<String, String>, key: &str) -> Option<bool> {
    match overrides.get(key).map(String::as_str) {
        Some("true") => Some(true),
        Some("false") => Some(false),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::{detect, ServerCompat, ServerKind};
    use std::collections::BTreeMap;

    const KINDS: [ServerKind; 5] = [
        ServerKind::OpenAiCompatible,
        ServerKind::Ollama,
        ServerKind::LmStudio,
        ServerKind::Vllm,
        ServerKind::LlamaCpp,
    ];

    #[test]
    fn a_server_that_implements_the_older_dialect_is_told_the_older_name() {
        assert_eq!(
            detect(ServerKind::OpenAiCompatible).answer_tokens_key,
            "max_completion_tokens"
        );
        for kind in [
            ServerKind::Ollama,
            ServerKind::LmStudio,
            ServerKind::Vllm,
            ServerKind::LlamaCpp,
        ] {
            let compat = detect(kind);
            assert_eq!(compat.answer_tokens_key, "max_tokens", "{kind:?}");
            assert!(!compat.accepts_reasoning_effort, "{kind:?}");
        }
    }

    #[test]
    fn detection_is_a_function_of_the_kind_and_nothing_else() {
        for kind in KINDS {
            assert_eq!(detect(kind), detect(kind), "{kind:?}");
        }
    }

    #[test]
    fn the_catalog_moves_what_detection_could_not_know() {
        let mut overrides = BTreeMap::new();
        overrides.insert("reports_finish_reason".to_owned(), "false".to_owned());
        overrides.insert("answer_tokens_field".to_owned(), "max_tokens".to_owned());
        let compat = detect(ServerKind::OpenAiCompatible).with_catalog(&overrides);
        assert!(!compat.reports_finish_reason);
        assert_eq!(compat.answer_tokens_key, "max_tokens");
    }

    #[test]
    fn a_value_that_is_neither_answer_leaves_the_detected_one_standing() {
        let mut overrides = BTreeMap::new();
        overrides.insert("reports_finish_reason".to_owned(), "maybe".to_owned());
        let compat: ServerCompat<'_> = detect(ServerKind::Ollama).with_catalog(&overrides);
        assert!(
            compat.reports_finish_reason,
            "an unreadable value is not a second way of saying no"
        );
    }
}
