// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every way the body writer says no.
//!
//! Its own module for the reason [`super::request`] is small: the refusals are
//! a vocabulary rather than a step of writing, and each of them is a paragraph
//! about a defect somebody is going to hit. Kept beside the writer they were
//! the larger half of a file whose subject is placing fields.
//!
//! Nothing here is a repair. Every member is a body that was not written and a
//! buffer left as it was found, because each of the repairs the writer could
//! have made instead — synthesizing a missing result, dropping an unanswered
//! call, renaming an identity into a family's own spelling, quietly leaving
//! out a block a vendor requires — produces a request that looks reasonable
//! and is not, and the model reads the answer to one as a fact about the
//! world.

/// Why a body was not written.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WireRefusal {
    /// The call offers tools and the model cannot answer with a tool call.
    ToolsUnsupportedByModel,
    /// The conversation was empty. Every family requires at least one turn,
    /// and a body without one is refused by the provider after it has been
    /// sent, which costs a round trip to learn something knowable here.
    EmptyConversation,
    /// A visual attachment did not carry a non-empty browser handle and the
    /// one exact PNG media type this path supports.
    InvalidMediaAttachment,
    /// A visual attachment had no spoken user turn to attach to.
    MediaWithoutUserTurn,
    /// The browser handle occurred other than exactly once in the completed
    /// body, so replacing it would be ambiguous.
    AmbiguousMediaHandle {
        /// Number of exact occurrences found in the newly written body.
        occurrences: usize,
    },
    /// The body passed [`MAX_BODY_BYTES`].
    BodyTooLarge {
        /// What it came to.
        bytes: usize,
    },
    /// A tool's argument schema nested deeper than [`MAX_TOOL_SCHEMA_DEPTH`].
    ///
    /// The writer walks a schema recursively, so a schema deep enough to
    /// exhaust the stack is a schema that decides how much stack the process
    /// uses. Compiled-in tool definitions are nowhere near the bound; this
    /// exists so that the bound is set rather than assumed.
    ToolSchemaTooDeep {
        /// Position of the tool in the list it was passed in.
        tool_index: usize,
    },
    /// A turn that replays tool calls carried none, or a turn that replays
    /// their results carried none.
    ///
    /// Refused rather than written. On the two families that group results
    /// into one turn it would be a turn with an empty content array, which the
    /// provider rejects; on the other two it would vanish from the body
    /// entirely, leaving a call in the transcript with no answer and a model
    /// waiting for one. Either way it is a defect in whatever built the
    /// transcript, and this is the last place that can still say so.
    EmptyToolTurn {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
    },
    /// A replayed call's arguments nested deeper than
    /// [`MAX_TOOL_SCHEMA_DEPTH`].
    ///
    /// The same bound and the same reason as a tool's schema: the writer walks
    /// the value recursively, so how much stack that costs has to be somebody's
    /// decision rather than the argument's.
    ToolArgumentsTooDeep {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
        /// Position of the call within that turn.
        call_index: usize,
    },
    /// A turn replayed more calls than [`super::reply::MAX_TOOL_CALLS`].
    ///
    /// The reader's own ceiling, so a transcript the process accepted can be
    /// written back out: a writer stricter than the reader would refuse a
    /// conversation that had already happened.
    TooManyToolCalls {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
    },
    /// A replayed call that no later turn answers.
    ///
    /// Not answered on the caller's behalf. Synthesizing the missing result is
    /// the repair a normalization layer reaches for first, and what it writes
    /// into the body is a sentence saying a tool failed when nothing ran —
    /// which the model has no way to tell from a tool that really did.
    CallWithoutResult {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
        /// Position of the unanswered call within that turn.
        call_index: usize,
    },
    /// A replayed result answering a call no earlier turn made.
    ///
    /// The same defect from the other side, and the same refusal. A result
    /// whose call is absent is a result the model reads as an answer to
    /// something it never asked, which is the worst of the two directions.
    ResultWithoutCall {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
        /// Position of the unmatched result within that turn.
        result_index: usize,
    },
    /// The call carries a preamble and the family has nowhere to put one.
    ///
    /// Refused rather than dropped. A preamble is written because a vendor
    /// refuses the request without it, so writing the request without one is
    /// producing a body that is going to be refused anyway — and a caller told
    /// "the provider said no" learns nothing, where a caller told this learns
    /// that it paired a credential with a family that has no room for what
    /// that credential requires.
    PreambleUnsupportedByFamily,
    /// A replayed call or result identity is not a minted call identity.
    ///
    /// Decision 0069 sections 4–5: identities are minted, never renamed. A
    /// sanitizer that maps two ids onto one is a silent wrong result, so a
    /// string the core did not mint — a provider's `resp_…|tool_0`, a
    /// 65-character value, a missing sequence — is refused rather than
    /// rewritten to look like one.
    InvalidCallId {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
        /// Position of the call or result within that turn.
        index: usize,
    },
}

impl core::fmt::Display for WireRefusal {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::ToolsUnsupportedByModel => {
                f.write_str("the model cannot answer with a tool call")
            }
            Self::EmptyConversation => f.write_str("the conversation has no turns"),
            Self::InvalidMediaAttachment => {
                f.write_str("the visual attachment handle or media type is invalid")
            }
            Self::MediaWithoutUserTurn => {
                f.write_str("the visual attachment has no user turn to attach to")
            }
            Self::AmbiguousMediaHandle { occurrences } => write!(
                f,
                "the visual attachment handle occurs {occurrences} times instead of once"
            ),
            Self::BodyTooLarge { bytes } => {
                write!(f, "the body came to {bytes} bytes, over the limit")
            }
            Self::ToolSchemaTooDeep { tool_index } => {
                write!(f, "the schema of tool {tool_index} nests too deeply")
            }
            Self::EmptyToolTurn { turn_index } => {
                write!(f, "turn {turn_index} replays neither a call nor a result")
            }
            Self::ToolArgumentsTooDeep {
                turn_index,
                call_index,
            } => write!(
                f,
                "the arguments of call {call_index} of turn {turn_index} nest too deeply"
            ),
            Self::TooManyToolCalls { turn_index } => {
                write!(f, "turn {turn_index} replays too many calls")
            }
            Self::CallWithoutResult {
                turn_index,
                call_index,
            } => write!(f, "call {call_index} of turn {turn_index} is unanswered"),
            Self::ResultWithoutCall {
                turn_index,
                result_index,
            } => write!(
                f,
                "result {result_index} of turn {turn_index} answers no call"
            ),
            Self::PreambleUnsupportedByFamily => {
                f.write_str("the family has nowhere to carry a system preamble")
            }
            Self::InvalidCallId { turn_index, index } => write!(
                f,
                "identity {index} of turn {turn_index} is not a minted call identity"
            ),
        }
    }
}
