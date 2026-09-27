// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The model-turn and walk surface of one profile runtime.
//!
//! Every method here is glue between the service bridge and the loop kernel:
//! the kernel owns the decisions and the transient state's shape, the core
//! runtime owns the state map, and this file hands borrowed surfaces across.
//! Nothing transient lives on the profile runtime itself any more (decision
//! 0072) — it lives in [`loop_kernel::state::LoopState`], owned per task by
//! the core runtime, and dies with the generation.

use std::rc::Rc;

use bip_types::identity::TaskId;
use loop_kernel::state::{HeldModelTurn, LoopState};
use task_engine::{ModelAttemptKind, ModelCallId, TurnGap, TurnResidency};

use crate::{
    compose_configured_model_turn, model_request_id, read_model_reply, ComposedModelTurn,
    ModelReplyReading, ModelReplyStream,
};

use super::ProfileServiceRuntime;

mod failure;
mod observation;
mod walk;

/// What planning one model call produced.
#[derive(Debug)]
pub enum PlannedTurn {
    /// The plan the browser is to perform.
    Turn(Box<ComposedModelTurn>),
    /// No plan exists; the refusal is held on the loop state until the walk
    /// records it as a durable gap.
    Refused,
}

/// What reading one model completion produced.
#[derive(Debug)]
pub enum ModelCompletionOutcome {
    /// The completion does not belong to this call at all: no plan is held,
    /// or the identities disagree. Nothing was changed.
    Invalid,
    /// The call was paid for and produced nothing usable; the held plan is
    /// dropped and the caller records the gap as a durable fact.
    Gap(TurnGap),
    /// The reply was read; the residency now waits for the loop to read it.
    Read,
}

/// A router-authorized next attempt of one already-charged logical turn.
#[derive(Debug)]
pub struct ModelAttemptDispatch {
    /// Exact request produced from the original immutable route plan.
    pub request: core_service_types::ModelRequestEffect,
    /// Durable subidentity ordinal; zero belongs to the initial attempt.
    pub attempt_ordinal: u32,
    /// Candidate this durable attempt advances to or repeats.
    pub candidate_ordinal: u32,
    /// Durable sequencing rule the reducer must apply before dispatch.
    pub kind: ModelAttemptKind,
    /// Delay Rust authorized after applying the router's cap.
    pub delay_millis: u64,
}

/// What a definitive provider failure permits next.
#[derive(Debug)]
pub enum ModelFailureOutcome {
    /// No held exact call matched; state was not changed.
    Invalid,
    /// The typed retry table stopped and the logical turn must settle.
    Gap(TurnGap),
    /// Dispatch one distinct durable attempt and keep the logical turn held.
    Dispatch(Box<ModelAttemptDispatch>),
}

/// Appends one lent delta to a turn's remembered words, up to the bound
/// [`loop_kernel::context::RecentTurns`] keeps, cut at a character boundary.
fn keep_said(said: &mut String, text: &str) {
    let room = loop_kernel::context::MAX_RECENT_SAID_BYTES.saturating_sub(said.len());
    let mut end = room.min(text.len());
    while end > 0 && !text.is_char_boundary(end) {
        end -= 1;
    }
    said.push_str(text.get(..end).unwrap_or_default());
}

fn settle_composed_person_line(state: &mut LoopState, composed: bool, had_person_answer: bool) {
    if !composed {
        return;
    }
    state.person_answer = None;
    if !had_person_answer {
        state.nested_goal = None;
    }
}

impl ProfileServiceRuntime {
    /// Composes the model request for one call of `task_id`.
    ///
    /// The outer `None` means the task identity is not open in this profile.
    /// Nothing is applied and nothing is charged here: the reducer charged the
    /// budget when it asked for the turn, and this only builds what the
    /// browser is handed. The previous turn's still-resident reply is
    /// overlaid so turn *n + 1* sees what turn *n* was given.
    pub fn compose_model_turn(
        &mut self,
        task_id: &TaskId,
        call_id: &str,
    ) -> Option<Result<ComposedModelTurn, loop_kernel::turn::ModelTurnError>> {
        let digest = Rc::clone(&self.digest);
        let configured_tools = self.assistant_configuration.effective_tool_allowlist();
        let response_style = self.assistant_configuration.response_style();
        let (task, state, models) = self.core.compose_surfaces(task_id.as_str())?;
        let mut facts = task.model_turn_facts();
        facts.transcript.overlay_recent_turns(&state.recent_turns);
        if let Some(residency) = state.residency.as_ref() {
            facts.transcript.overlay_resident_calls(residency);
        }
        // After both overlays, because both put numbers in: one fills the turn
        // being answered and the other fills the sixteen behind it. A number
        // the live pages will not honour is one the model can only be refused
        // for naming, so it does not go back in front of it (decision 0222).
        facts
            .transcript
            .retire_unusable_handles(&|value| state.page.honours_handle(value));
        facts.activated.clone_from(&state.activated);
        if let Some(goal) = state.nested_goal.as_ref() {
            facts.nested_goal = Some(goal.clone());
        }
        // The rung is the person's, and it is read here rather than held on the
        // task: a preference changed between two turns applies to the next one,
        // which is what a control that says "how much thinking" has to mean.
        facts.thinking = self.providers.agreed_thinking();
        let had_person_answer = state.person_answer.is_some();
        let composed = compose_configured_model_turn(
            models,
            &self.providers,
            digest.as_ref(),
            &facts,
            call_id,
            &mut state.page,
            state.person_answer.as_deref(),
            &configured_tools,
            response_style,
        );
        settle_composed_person_line(state, composed.is_ok(), had_person_answer);
        Some(composed)
    }

    /// Plans one model call, holding a refusal on the loop state when no plan
    /// could be built. The outer `None` means the task is not open.
    pub fn plan_model_turn(&mut self, task_id: &TaskId, call_id: &str) -> Option<PlannedTurn> {
        let composed = self.compose_model_turn(task_id, call_id)?;
        Some(match composed {
            Ok(turn) => PlannedTurn::Turn(Box::new(turn)),
            Err(error) => {
                let state = self.core.loop_state_mut(task_id.as_str());
                state.refused = Some(loop_kernel::state::RefusedModelCall::new(
                    ModelCallId::new(call_id.to_owned()),
                    &error,
                ));
                PlannedTurn::Refused
            }
        })
    }

    /// Holds the plan for `call_id` until its terminal arrives.
    ///
    /// The previous turn's reply goes as this one is composed: a task asks for
    /// its next turn only after the last one settled, so a residency still
    /// sitting here answers a call that is over.
    pub fn hold_composed_turn(
        &mut self,
        task_id: &TaskId,
        call_id: &str,
        turn: Box<ComposedModelTurn>,
    ) {
        let stream = self
            .core
            .task(task_id)
            .map(|task| task.model_turn_facts().milestone)
            .and_then(|milestone| {
                model_request_id(self.digest.as_ref(), call_id)
                    .ok()
                    .map(|request_id| ModelReplyStream::for_turn(&turn, request_id, milestone))
            });
        let state = self.core.loop_state_mut(task_id.as_str());
        state.residency = None;
        state.held = Some(HeldModelTurn {
            call_id: ModelCallId::new(call_id.to_owned()),
            turn,
            candidate_attempt: 0,
            attempt_ordinal: 0,
            candidate_ordinal: 0,
            stream,
            said: String::new(),
        });
    }

    /// Consumes one exact next raw transport chunk inside the sandbox.
    ///
    /// The callback receives visible answer text only. A false result means
    /// the chunk does not belong to the held call or the provider stream is
    /// unreadable; callers must stop transport and still deliver one terminal.
    pub fn push_model_stream_chunk(
        &mut self,
        task_id: &TaskId,
        call_id: &str,
        chunk: &[u8],
        emit_text: &mut dyn FnMut(&str),
    ) -> bool {
        let Some(state) = self.core.loop_state_mut(task_id.as_str()).held.as_mut() else {
            return false;
        };
        if state.call_id.as_str() != call_id {
            return false;
        }
        let HeldModelTurn { stream, said, .. } = state;
        stream.as_mut().is_some_and(|stream| {
            stream
                .push(chunk, &mut |text| {
                    keep_said(said, text);
                    emit_text(text);
                })
                .is_ok()
        })
    }

    /// Settles the held incremental reader exactly once and installs the same
    /// transient residency used by the whole-body compatibility path.
    pub fn finish_model_stream(
        &mut self,
        task_id: &TaskId,
        call_id: &str,
        completion_model_id: &str,
        emit_text: &mut dyn FnMut(&str),
    ) -> ModelCompletionOutcome {
        let (page, nested_depth, stream, mut said) = {
            let Some(state) = self.core.loop_state_mut(task_id.as_str()).held.as_mut() else {
                return ModelCompletionOutcome::Invalid;
            };
            if state.call_id.as_str() != call_id
                || state.turn.request.model_id != completion_model_id
            {
                return ModelCompletionOutcome::Invalid;
            }
            (
                state.turn.page.clone(),
                state.turn.nested_depth,
                state.stream.take(),
                core::mem::take(&mut state.said),
            )
        };
        let Some(stream) = stream else {
            return ModelCompletionOutcome::Invalid;
        };
        let (reading, note) = stream.finish_noted(&mut |text| {
            keep_said(&mut said, text);
            emit_text(text);
        });
        self.last_model_reading = note.unwrap_or("read");
        self.settle_model_reading(task_id, call_id, page, nested_depth, reading, &said)
    }

    /// Why the last completion this runtime read settled the way it did, as
    /// a compiled-in name: `read`, or the reason a reply was a gap. Every
    /// such reason reaches the reducer as the same [`TurnGap::Unreadable`],
    /// so this is what a diagnostic line can say that the journal cannot.
    #[must_use]
    pub const fn last_model_reading(&self) -> &'static str {
        self.last_model_reading
    }

    /// The clause that refused each call of the last read reply on sight, in
    /// order, as compiled-in names. Empty when every call was attemptable.
    #[must_use]
    pub fn last_refusals_on_sight(&self) -> &[&'static str] {
        &self.last_refusals_on_sight
    }

    /// The registered name of each tool call the task's last read reply
    /// names, in order, for a diagnostic line.
    ///
    /// A name the registry does not know reads `unregistered` and a
    /// namespaced one reads as its namespace, so no byte the model wrote
    /// reaches the line — only a name compiled into this build.
    #[must_use]
    pub fn reply_tool_names(&self, task_id: &TaskId) -> Vec<&'static str> {
        let Some(residency) = self
            .core
            .loop_state(task_id.as_str())
            .and_then(|state| state.residency.as_ref())
        else {
            return Vec::new();
        };
        residency
            .reply()
            .tool_calls
            .iter()
            .map(|call| {
                // Availability is a milestone question and this is not one:
                // any member the registry holds has a compiled-in name.
                task_engine::tool::resolve(&call.tool_name, task_engine::Milestone::M0)
                    .entry()
                    .map_or("unregistered", |entry| entry.name)
            })
            .collect()
    }

    /// The held plan for one task, if a call is out with the browser.
    #[must_use]
    pub fn held_turn(&self, task_id: &TaskId) -> Option<&HeldModelTurn> {
        self.core.loop_state(task_id.as_str())?.held.as_ref()
    }

    /// Drops the held plan for a call whose terminal settled without a reply.
    pub fn drop_held_turn(&mut self, task_id: &TaskId) {
        self.core.loop_state_mut(task_id.as_str()).held = None;
    }

    /// Reads one provider completion against the plan its call was composed
    /// from, and settles the loop state accordingly.
    ///
    /// `Invalid` names a completion that does not belong to this call — no
    /// held plan, a different call, or a different model — and changes
    /// nothing, because a gap recorded for such a message would spend the
    /// reducer's one answer on a message that was never about it.
    pub fn read_model_completion(
        &mut self,
        task_id: &TaskId,
        call_id: &str,
        completion_model_id: &str,
        completion: &[u8],
    ) -> ModelCompletionOutcome {
        let Some(state) = self.core.loop_state(task_id.as_str()) else {
            return ModelCompletionOutcome::Invalid;
        };
        let Some(held) = state.held.as_ref() else {
            return ModelCompletionOutcome::Invalid;
        };
        if held.call_id.as_str() != call_id || held.turn.request.model_id != completion_model_id {
            return ModelCompletionOutcome::Invalid;
        }
        let page = held.turn.page.clone();
        let (wire, context_window, answer_tokens, nested_depth) = (
            held.turn.wire.clone(),
            held.turn.context_window,
            held.turn.answer_tokens,
            held.turn.nested_depth,
        );
        let Some(milestone) = self
            .core
            .task(task_id)
            .map(|task| task.model_turn_facts().milestone)
        else {
            return ModelCompletionOutcome::Invalid;
        };
        let reading = if let Ok(request_id) = model_request_id(self.digest.as_ref(), call_id) {
            let reading = read_model_reply(
                &wire,
                completion,
                request_id,
                context_window,
                answer_tokens,
                milestone,
            );
            self.last_model_reading = match reading {
                ModelReplyReading::Read(_) => "read",
                ModelReplyReading::Gap(_) => "body_unreadable",
            };
            reading
        } else {
            // Without the digest the reply cannot be attributed to the call
            // it answers, and a turn that cannot be attributed is a gap.
            self.last_model_reading = "digest_unavailable";
            ModelReplyReading::Gap(TurnGap::Unreadable)
        };
        // The whole-body path lends no text as it reads, so this turn is
        // remembered by its arguments alone.
        self.settle_model_reading(task_id, call_id, page, nested_depth, reading, "")
    }

    fn settle_model_reading(
        &mut self,
        task_id: &TaskId,
        call_id: &str,
        page: task_engine::TurnPage,
        nested_depth: u32,
        reading: ModelReplyReading,
        said: &str,
    ) -> ModelCompletionOutcome {
        let outcome = match reading {
            ModelReplyReading::Gap(gap) => ModelCompletionOutcome::Gap(gap),
            ModelReplyReading::Read(reply) => {
                if reply.tool_calls.iter().any(|call| {
                    !self
                        .assistant_configuration
                        .admits_tool_call(&call.tool_name)
                }) {
                    self.last_model_reading = "tool_not_admitted";
                    self.drop_held_turn(task_id);
                    return ModelCompletionOutcome::Gap(TurnGap::Unreadable);
                }
                match TurnResidency::read(ModelCallId::new(call_id.to_owned()), page, *reply)
                    .and_then(|residency| residency.with_nested_depth(nested_depth))
                {
                    // `read_model_reply` already refuses a reply past the
                    // ceiling, so this is the same refusal reached twice
                    // rather than a second policy.
                    None => {
                        self.last_model_reading = "residency_refused";
                        ModelCompletionOutcome::Gap(TurnGap::Unreadable)
                    }
                    Some(residency) => {
                        // Asked now, of the task as it stands when the walk
                        // next reads this reply, so the memory names the
                        // same refusals the digest is about to count.
                        let refused = self
                            .core
                            .task(task_id)
                            .map(|task| task.refused_on_sight(&residency))
                            .unwrap_or_default();
                        // Kept for the diagnostic line as well as for the
                        // memory. `refused_on_sight=1` says a call did not
                        // happen and not which clause said so, and the
                        // clauses mean opposite things to a reader: a number
                        // that resolves to nothing is a stale reading, while
                        // a field that cannot take text is the model naming
                        // the wrong one of two lines for the same box. Every
                        // label is compiled in.
                        self.last_refusals_on_sight =
                            refused.iter().map(|(_, reason)| reason.label()).collect();
                        let state = self.core.loop_state_mut(task_id.as_str());
                        state.recent_turns.remember(&residency, said, refused);
                        state.residency = Some(
                            residency
                                .with_task_tabs(state.task_tabs.clone())
                                .with_task_downloads(state.task_downloads.clone()),
                        );
                        state.held = None;
                        return ModelCompletionOutcome::Read;
                    }
                }
            }
        };
        self.drop_held_turn(task_id);
        outcome
    }

    /// Reads one typed model completion into the reducer's taxonomy.
    ///
    /// The outer `None` means the task identity is not open. There is no inner
    /// error: every way of failing to read a provider's answer is already a
    /// recorded gap, because the call was paid for either way.
    pub fn read_model_turn(
        &self,
        task_id: &TaskId,
        turn: &ComposedModelTurn,
        completion: &[u8],
        call_id: &str,
    ) -> Option<ModelReplyReading> {
        let facts = self.core.task(task_id)?.model_turn_facts();
        let Ok(request_id) = model_request_id(self.digest.as_ref(), call_id) else {
            // The digest adapter is the same one that derived the identity the
            // request was sent under. Without it the reply cannot be attributed
            // to the call it answers, and a turn that cannot be attributed is a
            // gap rather than a reading.
            return Some(ModelReplyReading::Gap(task_engine::TurnGap::Unreadable));
        };
        Some(read_model_reply(
            &turn.wire,
            completion,
            request_id,
            turn.context_window,
            turn.answer_tokens,
            facts.milestone,
        ))
    }

    /// The call the browser is currently holding for `task_id`, if any.
    pub fn model_turn_in_flight(&self, task_id: &TaskId) -> Option<String> {
        self.core.task(task_id)?.model_turn_in_flight()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn successful_compose_consumes_the_nested_pass_goal() {
        let mut state = LoopState {
            nested_goal: Some("inspect the narrowed evidence".to_owned()),
            ..LoopState::default()
        };
        settle_composed_person_line(&mut state, true, false);
        assert!(state.nested_goal.is_none());
    }

    #[test]
    fn a_person_answer_goes_first_and_leaves_the_nested_goal_for_the_next_compose() {
        let mut state = LoopState {
            person_answer: Some("yes".to_owned()),
            nested_goal: Some("inspect the narrowed evidence".to_owned()),
            ..LoopState::default()
        };
        settle_composed_person_line(&mut state, true, true);
        assert!(state.person_answer.is_none());
        assert_eq!(
            state.nested_goal.as_deref(),
            Some("inspect the narrowed evidence")
        );
    }

    #[test]
    fn refused_compose_retains_transient_prompt_inputs_for_retry() {
        let mut state = LoopState {
            person_answer: Some("yes".to_owned()),
            nested_goal: Some("inspect the narrowed evidence".to_owned()),
            ..LoopState::default()
        };
        settle_composed_person_line(&mut state, false, true);
        assert_eq!(state.person_answer.as_deref(), Some("yes"));
        assert_eq!(
            state.nested_goal.as_deref(),
            Some("inspect the narrowed evidence")
        );
    }
}
