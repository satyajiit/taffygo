// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One call and its result, and one turn of such calls: the unit the
//! eviction ladder drops. The parent module says why the pair is one value.

use model_router::json::JsonValue;
use model_router::wire::reply::MAX_TOOL_CALLS;
use task_engine::{ActionState, NotAttempted};

use crate::context::refusal::refusal_sentence_for;
use crate::context::vocabulary::{not_attempted_word, outcome_is_failure, outcome_word};

/// One tool call the model already made, together with what it answered.
///
/// The two halves are one value because the wire refuses them apart. See the
/// module documentation: this is the pairing invariant, and it is a property
/// of the type rather than of the code that walks it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RecordedCall {
    pub(super) call_id: String,
    pub(super) tool: String,
    pub(super) arguments: JsonValue,
    kind: RecordedKind,
    pub(super) result: Vec<String>,
}

/// What a recorded call was, when it was not only an action.
///
/// What a call is told when the number it named has been taken out of it.
///
/// Compiled in, closed, and the same shape as every other result sentence: it
/// says what happened and the one move that makes a usable number exist, which
/// is the wording decision 0208 settled for the refusal this prevents.
pub(super) const RETIRED_HANDLE_SENTENCE: &str =
    "the number this named is gone; read the page you are on and use a number from that reading";

/// Truncated calls never became actions, so they cannot share
/// [`ActionState`]. A private enumeration keeps that fact in the type rather
/// than as a `Failed` action that never existed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum RecordedKind {
    Action(ActionState),
    Loop { is_error: bool },
    NotAttempted,
}

impl RecordedCall {
    /// The call `call_id` made on `tool` with `arguments`, which ended in
    /// `outcome`.
    ///
    /// `call_id` is the core's own identity — `turn-{ordinal}-call-{sequence}`
    /// — on every protocol family, because a provider's own identity does not
    /// survive a restart or a model substitution (decision 0069 section 3).
    /// `tool` is the registry's canonical name and never a model's spelling of
    /// it.
    pub fn new(call_id: String, tool: String, arguments: JsonValue, outcome: ActionState) -> Self {
        Self {
            call_id,
            tool,
            arguments,
            kind: RecordedKind::Action(outcome),
            // Derived here and set nowhere else. It is stored rather than
            // computed at the point of use because a borrowed `ToolResultView`
            // needs an address to point at, not because it is a second fact
            // about the call: `outcome` remains the only one.
            result: vec![outcome_word(outcome).to_owned()],
        }
    }

    /// A call that was proposed and then refused or failed with a named
    /// result code, carrying the outcome word and the compiled-in sentence for
    /// that code.
    ///
    /// Two pieces, not one: the word is what every recorded call carries and
    /// what the eviction ladder and the tests reason about; the sentence is
    /// what makes the refusal something the model can act on (decision 0136).
    /// Neither piece is read from a page or a reply.
    pub fn refused(
        call_id: String,
        tool: String,
        arguments: JsonValue,
        outcome: ActionState,
        code: bip_types::ActionResultCode,
    ) -> Self {
        let sentence = refusal_sentence_for(&tool, code);
        Self {
            call_id,
            tool,
            arguments,
            kind: RecordedKind::Action(outcome),
            result: vec![outcome_word(outcome).to_owned(), sentence.to_owned()],
        }
    }

    /// A call the reducer refused on sight, with the compiled-in sentence the
    /// model reads back.
    ///
    /// Not an action: nothing was proposed. The wire still needs a paired
    /// result so the next request is a well-formed `Called` + `Returned`
    /// rather than a fresh turn that forgot the truncated `tool_use`.
    pub fn not_attempted(
        call_id: String,
        tool: String,
        arguments: JsonValue,
        reason: NotAttempted,
    ) -> Self {
        Self {
            call_id,
            tool,
            arguments,
            kind: RecordedKind::NotAttempted,
            result: vec![not_attempted_word(reason).to_owned()],
        }
    }

    /// A settled loop-local call and its compiled-in result.
    ///
    /// Loop tools never become browser actions and therefore leave no durable
    /// action record. While the turn residency is still alive, keeping the
    /// result beside the call is what lets the next model request observe the
    /// search, activation, or nested-pass outcome it just produced. Neither
    /// the arguments nor the sentence are journaled.
    pub fn loop_outcome(
        call_id: String,
        tool: String,
        arguments: JsonValue,
        outcome: task_engine::LoopOutcome,
    ) -> Self {
        let is_error = matches!(
            outcome,
            task_engine::LoopOutcome::Activated { name_known: false }
                | task_engine::LoopOutcome::TableRefused
                | task_engine::LoopOutcome::Refused
        );
        Self {
            call_id,
            tool,
            arguments,
            kind: RecordedKind::Loop { is_error },
            result: vec![outcome.sentence().to_owned()],
        }
    }

    /// Drops every handle argument the live pages will not honour.
    ///
    /// Returns whether anything was dropped, so a caller can say so.
    pub(super) fn drop_unusable_handles(&mut self, honours: &dyn Fn(u32) -> bool) -> bool {
        // By exact name rather than the registry's own matcher: `tool` is
        // documented as the canonical name the registry gave, never a model's
        // spelling of it, so a prefix or alias rule has nothing to do here.
        let Some(entry) = task_engine::REGISTRY
            .iter()
            .find(|entry| entry.name == self.tool)
        else {
            return false;
        };
        let Some(object) = self.arguments.as_object() else {
            return false;
        };
        let dead: Vec<String> = entry
            .parameters
            .iter()
            .filter(|parameter| parameter.value_type == task_engine::ParameterType::Handle)
            .filter_map(|parameter| {
                let value = object.get(parameter.name)?.as_i64()?;
                let value = u32::try_from(value).ok()?;
                (!honours(value)).then(|| parameter.name.to_owned())
            })
            .collect();
        if dead.is_empty() {
            return false;
        }
        let mut kept = object.clone();
        for name in &dead {
            kept.remove(name);
        }
        self.arguments = JsonValue::Object(kept);
        true
    }

    /// The identity this call is known by.
    pub fn call_id(&self) -> &str {
        &self.call_id
    }

    /// The registry's name for the tool.
    pub fn tool(&self) -> &str {
        &self.tool
    }

    /// The arguments this call carries, which are `{}` until a residency fills them.
    pub const fn arguments(&self) -> &JsonValue {
        &self.arguments
    }

    /// What became of it, when this call was an action.
    ///
    /// A refusal on sight was never an action, so this answers `Failed`: the
    /// call did not happen. Prefer [`Self::is_error`] when the wire flag is
    /// what the caller needs.
    pub const fn outcome(&self) -> ActionState {
        match self.kind {
            RecordedKind::Action(state) => state,
            RecordedKind::Loop { is_error: false } => ActionState::Verified,
            RecordedKind::Loop { is_error: true } | RecordedKind::NotAttempted => {
                ActionState::Failed
            }
        }
    }

    /// Whether the model should read this result as a failure.
    pub(super) const fn is_error(&self) -> bool {
        match self.kind {
            RecordedKind::Action(state) => outcome_is_failure(state),
            RecordedKind::Loop { is_error } => is_error,
            RecordedKind::NotAttempted => true,
        }
    }

    /// The material this call would put into a request body.
    pub(super) fn text_bytes(&self) -> usize {
        // The identity is written twice — once on the call, once on the result
        // that answers it — so it is counted twice.
        self.call_id
            .len()
            .saturating_mul(2)
            .saturating_add(self.tool.len().saturating_mul(2))
            .saturating_add(
                self.result
                    .iter()
                    .map(String::len)
                    .fold(0, usize::saturating_add),
            )
            .saturating_add(value_bytes(&self.arguments))
    }
}

/// One turn of the conversation: every call it made, each carrying its result.
///
/// Constructed through [`Self::new`] only, so a turn that made no call cannot
/// exist — `WireRefusal::EmptyToolTurn` is the wire's answer to one, and this
/// removes the shape rather than reporting it later.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TurnExchange {
    pub(super) ordinal: u64,
    pub(super) calls: Vec<RecordedCall>,
    /// What the model said beside these calls, while this generation still
    /// remembers it ([`TaskTranscript::overlay_recent_turns`]). Empty after a
    /// restart: the journal never held it.
    ///
    /// [`TaskTranscript::overlay_recent_turns`]: super::TaskTranscript::overlay_recent_turns
    pub(super) said: Vec<String>,
}

impl TurnExchange {
    /// The `ordinal`-th turn of a task, or `None` when it is not a turn a body
    /// can be written from.
    ///
    /// `None` for a turn with no calls, and for one with more than the wire
    /// will write. The second bound is the reader's own ceiling
    /// (`MAX_TOOL_CALLS`), matched here for the reason the writer already
    /// matches it: a transcript the process accepted must be writable, and
    /// trimming the excess instead would break the pairing this type exists to
    /// hold.
    pub fn new(ordinal: u64, calls: Vec<RecordedCall>) -> Option<Self> {
        if calls.is_empty() || calls.len() > MAX_TOOL_CALLS {
            return None;
        }
        Some(Self {
            ordinal,
            calls,
            said: Vec::new(),
        })
    }

    /// Which turn of the task this was, counting from zero.
    pub const fn ordinal(&self) -> u64 {
        self.ordinal
    }

    /// The calls, in the order the reply declared them.
    pub fn calls(&self) -> &[RecordedCall] {
        &self.calls
    }

    pub(super) fn text_bytes(&self) -> usize {
        self.calls
            .iter()
            .map(RecordedCall::text_bytes)
            .fold(0, usize::saturating_add)
            .saturating_add(
                self.said
                    .iter()
                    .map(String::len)
                    .fold(0, usize::saturating_add),
            )
    }
}

/// The bytes one decoded value costs, counted rather than rendered.
///
/// Iterative on an explicit stack rather than recursive. The value reaches
/// here from a provider's reply by way of the router's reader, so how much
/// stack measuring it costs has to be this function's decision rather than the
/// document's — the same reason `within_depth` exists on the writing side.
fn value_bytes(value: &JsonValue) -> usize {
    let mut total = 0usize;
    let mut stack = vec![value];
    while let Some(current) = stack.pop() {
        total = total.saturating_add(match current {
            // `null`, `true`/`false`, and the widest `i64` written out.
            JsonValue::Null | JsonValue::Bool(_) => 5,
            JsonValue::Integer(_) => 20,
            JsonValue::Decimal(spelling) => spelling.len(),
            JsonValue::Text(text) => text.len().saturating_add(2),
            JsonValue::Array(items) => {
                stack.extend(items.iter());
                items.len().saturating_add(2)
            }
            JsonValue::Object(map) => {
                stack.extend(map.values());
                map.keys()
                    .map(|key| key.len().saturating_add(4))
                    .fold(2, usize::saturating_add)
            }
        });
    }
    total
}
