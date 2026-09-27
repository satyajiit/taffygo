// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One composed model turn, projected onto the closed task-effect binding.
//!
//! The plan itself is built by `core_runtime::compose_model_turn`, which is
//! the only party that may: it reads the task's disclosed route, asks
//! `model-router` to choose, and finds the credential *handle* for the chosen
//! provider. This module carries what that produced and adds nothing — in
//! particular it never repairs a plan, because a repaired plan is a request
//! nobody proposed, sent on a person's credential.
//!
//! Two properties have to survive the crossing. An address never travels
//! alone: it is carried with the authority that named it, because the browser
//! revalidates under a different rule for each (decision 0096). A catalog
//! origin is *judged* — https, a host, and no path, which is what stops a core
//! from naming any route on a host a person had already trusted with a key. A
//! person's own base URL is *recognized* instead, matched against the register
//! the browser itself holds, path and all. Neither rule is read off the
//! address's shape, so the kind is copied across here rather than decided
//! here, and a plan that named the wrong one is refused by the rule it named
//! rather than answered by the other. And the only credential-shaped value in
//! the whole binding is an opaque secure-store handle: there is no field on
//! `BridgeTaskEffect` a key could sit in, and the browser is the party that
//! resolves the handle as it sends.
//!
//! What a call leaves behind between its moments — refused, held, read — lives
//! on the task's loop state inside the runtime (decision 0072), never on this
//! bridge. This module asks the runtime and carries the answers.

use core_runtime::{
    wire, Command, ComposedModelTurn, Effect, ModelCallId, ModelCompletionOutcome, PlannedTurn,
    TaskId, TurnGap,
};

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_trace as trace;
use crate::service_bridge_workflow::continue_reviewed_workflow;

use super::{submit_completion_command, PendingTaskEffect};

mod failure;
mod stream;

use failure::complete_definitive_failure;
use stream::bounded_visible_deltas;
pub(crate) use stream::DeliverModelStreamChunk;

/// What composing one effect's model turn produced.
pub(super) enum ComposedTurn {
    /// The effect is not a model call.
    None,
    /// The plan the browser is to perform.
    Turn(Box<ComposedModelTurn>),
    /// No plan exists and the call is recorded as a gap instead.
    Refused,
}

/// Composes the plan for `effect`, when `effect` is a model call.
///
/// This is the seam. It is here rather than inside the per-effect projection
/// because composing needs the profile itself: the disclosed route lives on
/// the task, choosing the model is `model-router`'s job and takes `&mut`, and
/// the credential handle comes from the profile's provider plane. A refusal
/// is held on the task's loop state by the runtime until the walk records it
/// as a durable gap.
pub(super) fn compose_model_turn(
    bridge: &mut ServiceBridge,
    task_id: &TaskId,
    effect: &Effect,
) -> Result<ComposedTurn, ()> {
    let Effect::CallModel { call_id } = effect else {
        return Ok(ComposedTurn::None);
    };
    let runtime = bridge.runtime.as_mut().ok_or(())?;
    // The outer `None` means the task is not open in this profile, which
    // cannot be true of a task whose own reducer just produced this effect.
    match runtime
        .plan_model_turn(task_id, call_id.as_str())
        .ok_or(())?
    {
        PlannedTurn::Turn(turn) => Ok(ComposedTurn::Turn(turn)),
        PlannedTurn::Refused => Ok(ComposedTurn::Refused),
    }
}

/// Fills the model half of one binding from the plan that was composed for it.
///
/// The body and the headers are copied here, and only here. The plan is
/// retained by `hold_composed_turn` for exactly as long as the call is in
/// flight — the reply is read against it, and a retry of the same candidate
/// re-dispatches `held.turn.request` — so the held copy has to keep its
/// bytes. A sub-attempt's request, by contrast, is minted for one dispatch
/// and is moved into its binding by `project_model_request` below.
pub(super) fn project_model(
    out: &mut ffi::BridgeTaskEffect,
    call_id: &ModelCallId,
    turn: &ComposedModelTurn,
) -> Result<(), ()> {
    project_model_request_facts(out, call_id, &turn.request)?;
    for header in &turn.request.static_headers {
        out.static_header_names.push(header.name.clone());
        out.static_header_values.push(header.value.clone());
    }
    out.request_body = turn.request.request_body.clone();
    Ok(())
}

/// Fills the model half of one binding from a request this bridge owns.
///
/// By value, because the request is the whole prompt — page content and any
/// base64 image — and it is never read again after the binding leaves: the
/// committed sub-attempt it came from was cloned out of the held plan by the
/// runtime, so the copy here is the only one and is moved rather than copied
/// a second time.
pub(super) fn project_model_request(
    out: &mut ffi::BridgeTaskEffect,
    call_id: &ModelCallId,
    request: wire::ModelRequestEffect,
) -> Result<(), ()> {
    project_model_request_facts(out, call_id, &request)?;
    for header in request.static_headers {
        out.static_header_names.push(header.name);
        out.static_header_values.push(header.value);
    }
    out.request_body = request.request_body;
    Ok(())
}

/// Every field of the model half but the two that carry the bytes.
///
/// Borrows the request so both entry points above share one projection of
/// the facts and differ only in whether they copy or move the body and the
/// header lists. The lists are cleared here so that whichever caller fills
/// them appends to an empty pair, and the pair is left empty on `Err`.
fn project_model_request_facts(
    out: &mut ffi::BridgeTaskEffect,
    call_id: &ModelCallId,
    request: &wire::ModelRequestEffect,
) -> Result<(), ()> {
    // The binding already names the task, and the request names it too. They
    // are two fields rather than one because the request is also dispatched on
    // its own, where the binding is not there to read — and a pair that
    // disagreed would be a paid call that `CoreEffectBroker::CancelTask` looks
    // for under one task while a person cancelled the other. The browser
    // refuses the pair as well; this refuses it before it is built.
    if request.task_id != out.task_id {
        return Err(());
    }
    // Carried as two parallel lists, in the plan's own order, filled by the
    // caller. Dropping one silently would send a request that differs from
    // the plan that was reviewed, and inventing one on the far side would
    // send a header the plan never named; both are refusals rather than
    // repairs, and the far side refuses a pair whose lengths disagree.
    out.static_header_names.clear();
    out.static_header_values.clear();
    out.call_id = call_id.as_str().to_owned();
    out.route_id = request.route_id.clone();
    out.model_id = request.model_id.clone();
    // Both are closed generated enumerations already — the composer chose
    // members of the same contract this binding is written against, so there
    // is nothing to map and a mapping table would be a second opinion.
    out.disclosure = request.disclosure as u8;
    out.wire_api = request.wire_api as u8;
    out.provider_id = request.provider_id.clone();
    out.endpoint = request.endpoint.clone();
    // Copied beside the address it belongs to, and from the same plan, because
    // the composer decided the two in one expression: whichever layer of the
    // merged catalog supplied the candidate named both. Choosing it here would
    // be this seam claiming a rule on the composer's behalf, and it would claim
    // it in the direction that sends a person's own address under the catalog
    // check.
    out.endpoint_kind = request.endpoint_kind as u8;
    out.max_output_bytes = request.max_output_bytes;
    out.not_before_monotonic_ms = request.not_before_monotonic_ms;
    if let Some(handle) = request.credential_handle.as_ref() {
        out.has_credential_handle = true;
        out.credential_handle = handle.clone();
    }
    match (
        request.media_attachment_handle.as_ref(),
        request.media_attachment_mime_type.as_ref(),
    ) {
        (None, None) => {}
        (Some(handle), Some(mime_type))
            if request.disclosure == wire::DisclosureClass::PageContent
                && !handle.is_empty()
                && handle.len() <= wire::MAX_MEDIA_ATTACHMENT_HANDLE_BYTES
                && mime_type == "image/png" =>
        {
            out.has_media_attachment = true;
            out.media_attachment_handle = handle.clone();
            out.media_attachment_mime_type = mime_type.clone();
        }
        _ => return Err(()),
    }
    Ok(())
}

/// Holds the plan for `call_id` until its terminal arrives.
///
/// The runtime supersedes the previous turn's residency as it holds this one:
/// a task asks for its next turn only after the last one is settled, so a
/// residency still sitting there answers a call that is over.
pub(super) fn hold_composed_turn(
    bridge: &mut ServiceBridge,
    task_id: &TaskId,
    call_id: &str,
    turn: Box<ComposedModelTurn>,
) {
    if let Some(runtime) = bridge.runtime.as_mut() {
        runtime.hold_composed_turn(task_id, call_id, turn);
    }
}

/// Records what became of one model call the browser was handed.
///
/// Two outcomes, and the split between them is the whole of this function. A
/// terminal carrying the provider's bytes is a reply, and it is read here — in
/// the sandbox, against the plan retained when the call was composed, because
/// the browser holds every capability there is and parsing a provider's JSON
/// is parsing untrusted structure. A terminal carrying none is a gap: a
/// positive fact about a call that was paid for and produced nothing usable.
/// The reducer keeps its budget charge either way, so a task that asked for a
/// turn is answered rather than left waiting on a call nobody is going to
/// speak about again.
pub(super) fn complete_model(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let status = wire::TaskEffectCompletionStatus::from_wire(u32::from(terminal.status));
    // An observation on a model terminal is a completion for another effect
    // family wearing this one's identity.
    let Some(status) = status.filter(|_| !pending.call_id.is_empty() && !terminal.has_observation)
    else {
        return invalid(&pending);
    };
    let succeeded = matches!(status, wire::TaskEffectCompletionStatus::Succeeded);
    let streamed = if succeeded {
        terminal.has_model_completion && terminal.model_completion_streamed
    } else {
        pending.stream_sequence != 0
    };
    if succeeded
        && ((terminal.model_completion_streamed
            && (!terminal.has_model_completion
                || !terminal.model_completion.is_empty()
                || pending.stream_sequence == 0))
            || (pending.stream_sequence != 0 && !streamed))
    {
        return invalid(&pending);
    }
    if succeeded && terminal.has_model_completion {
        if streamed {
            return read_stream_completion(bridge, pending, terminal, now_monotonic_ms);
        }
        return read_completion(bridge, pending, terminal, now_monotonic_ms);
    }
    if terminal.has_model_failure {
        return complete_definitive_failure(bridge, pending, terminal, now_monotonic_ms);
    }
    if terminal.has_model_retry_after
        || terminal.model_retry_after_millis != 0
        || terminal.model_failure_class != 0
    {
        return invalid(&pending);
    }
    let gap = match status {
        // A provider answered and the browser brought nothing back. That is
        // exactly `Unreadable`: a reply arrived and could not be read into the
        // taxonomy, and the money for it was spent either way. Success now has
        // a second shape — the one `read_completion` above takes — so this arm
        // is the empty case rather than every case.
        wire::TaskEffectCompletionStatus::Succeeded => TurnGap::Unreadable,
        wire::TaskEffectCompletionStatus::Refused => TurnGap::Refused,
        // The browser could not perform the call. The broker answers this way
        // when the adapter is missing, the intent could not be journalled, or
        // the generation has already been torn down.
        wire::TaskEffectCompletionStatus::Unavailable => TurnGap::Unavailable,
        // What a person cancelling the task will produce once that executor
        // exists: `CoreEffectBroker::CancelTask` claims a dispatched model
        // request by the task its plan names, and its terminal arrives here.
        wire::TaskEffectCompletionStatus::Cancelled => TurnGap::Cancelled,
        wire::TaskEffectCompletionStatus::OutcomeUnknown => TurnGap::OutcomeUnknown,
        // This terminal belongs only to form actions. Accepting it as a model
        // gap would let one effect family settle another's identity.
        wire::TaskEffectCompletionStatus::ValueReferenceUnknown => return invalid(&pending),
    };
    let answer_sequence = pending.answer_sequence;
    let task_id = pending.task_id.clone();
    let call_id = pending.call_id.clone();
    let mut response = record_gap(bridge, pending, gap, now_monotonic_ms);
    if streamed {
        response
            .task_answer_events
            .push(ffi::BridgeTaskAnswerEvent {
                task_id,
                call_id,
                sequence: answer_sequence,
                has_text: false,
                text: String::new(),
                terminal: true,
                complete: false,
            });
    }
    response
}

fn read_stream_completion(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let Some(runtime) = bridge.runtime.as_mut() else {
        return invalid(&pending);
    };
    let mut final_text = String::new();
    let task_id = TaskId::new(pending.task_id.clone());
    let outcome = runtime.finish_model_stream(
        &task_id,
        &pending.call_id,
        &terminal.model_completion_model_id,
        &mut |text| final_text.push_str(text),
    );
    trace_reading(runtime, &task_id, "stream", &outcome);
    let Some(deltas) = bounded_visible_deltas(
        &final_text,
        wire::MAX_TASK_ANSWER_EVENTS_PER_BATCH.saturating_sub(1),
    ) else {
        return invalid(&pending);
    };
    let Ok(delta_count) = u32::try_from(deltas.len()) else {
        return invalid(&pending);
    };
    let Some(terminal_sequence) = pending.answer_sequence.checked_add(delta_count) else {
        return invalid(&pending);
    };
    let complete = matches!(outcome, ModelCompletionOutcome::Read);
    let mut response = match outcome {
        ModelCompletionOutcome::Invalid => return invalid(&pending),
        ModelCompletionOutcome::Gap(gap) => {
            record_gap(bridge, pending.clone(), gap, now_monotonic_ms)
        }
        ModelCompletionOutcome::Read => {
            bridge.pending_task_effects.remove(&pending.effect_id);
            continue_after_model_turn(bridge, &pending, now_monotonic_ms)
        }
    };
    response.task_answer_events.extend(
        (pending.answer_sequence..terminal_sequence)
            .zip(deltas)
            .map(|(sequence, text)| ffi::BridgeTaskAnswerEvent {
                task_id: pending.task_id.clone(),
                call_id: pending.call_id.clone(),
                sequence,
                has_text: true,
                text,
                terminal: false,
                complete: false,
            }),
    );
    response
        .task_answer_events
        .push(ffi::BridgeTaskAnswerEvent {
            task_id: pending.task_id,
            call_id: pending.call_id,
            sequence: terminal_sequence,
            has_text: false,
            text: String::new(),
            terminal: true,
            complete,
        });
    response
}

/// Reads one provider's bytes against the plan its call was composed from.
///
/// Every `Invalid` here is `InvalidCommand` rather than a gap, and
/// deliberately: each names a completion that does not belong to this call at
/// all — a reply with no plan behind it, a reply to a different call of the
/// same task, or a reply from a model this plan never chose. A gap would
/// record such a thing as *this* turn's outcome, spending the reducer's one
/// answer on a message that was never about it.
fn read_completion(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let Some(runtime) = bridge.runtime.as_mut() else {
        return invalid(&pending);
    };
    let task_id = TaskId::new(pending.task_id.clone());
    let outcome = runtime.read_model_completion(
        &task_id,
        &pending.call_id,
        &terminal.model_completion_model_id,
        &terminal.model_completion,
    );
    trace_reading(runtime, &task_id, "body", &outcome);
    match outcome {
        ModelCompletionOutcome::Invalid => invalid(&pending),
        ModelCompletionOutcome::Gap(gap) => record_gap(bridge, pending, gap, now_monotonic_ms),
        ModelCompletionOutcome::Read => {
            // Read and held by the runtime. What a reply *means* is the
            // assistant loop's decision — the walk reads the decision table
            // against the residency — so this completion drives that walk
            // rather than applying a command this bridge chose. The residency
            // stays: row 21 still needs it after `RecordModelTurn`, and the
            // next compose is what supersedes it.
            bridge.pending_task_effects.remove(&pending.effect_id);
            continue_after_model_turn(bridge, &pending, now_monotonic_ms)
        }
    }
}

/// Names how one reply was read, and every call a read one made.
fn trace_reading(
    runtime: &core_runtime::ProfileServiceRuntime,
    task_id: &TaskId,
    via: &str,
    outcome: &ModelCompletionOutcome,
) {
    let tools = match outcome {
        ModelCompletionOutcome::Read => runtime.reply_tool_names(task_id),
        ModelCompletionOutcome::Gap(_) | ModelCompletionOutcome::Invalid => Vec::new(),
    };
    let reading = match outcome {
        ModelCompletionOutcome::Invalid => "invalid",
        ModelCompletionOutcome::Gap(_) | ModelCompletionOutcome::Read => {
            runtime.last_model_reading()
        }
    };
    trace::reply_read(task_id, via, reading, &tools, runtime.last_refusals_on_sight());
}

/// Drives the assistant table now that this call has a residency to read.
///
/// The pending effect is already dropped, so the driver will not see this
/// completion as still in flight and skip the next command.
fn continue_after_model_turn(
    bridge: &mut ServiceBridge,
    pending: &PendingTaskEffect,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let operation_id = pending.operation.operation_id.clone();
    match continue_reviewed_workflow(
        bridge,
        &TaskId::new(pending.task_id.clone()),
        now_monotonic_ms,
    ) {
        Ok(Some(next)) => next,
        Ok(None) => response(
            &operation_id,
            wire::AdmissionStatus::Accepted as u8,
            Vec::new(),
        ),
        Err(()) => {
            bridge.runtime = None;
            response(
                &operation_id,
                wire::AdmissionStatus::CoreUnavailable as u8,
                Vec::new(),
            )
        }
    }
}

/// Records that one call produced no readable reply, and settles the effect.
///
/// The plan goes with the pending effect rather than outliving it. A plan kept
/// past its terminal would be there to be matched against the *next* call of
/// the same task, and a reply read under a request nobody made is worse than
/// no reply at all.
fn record_gap(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    gap: TurnGap,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let command = Command::RecordModelTurnGap {
        call_id: ModelCallId::new(pending.call_id.clone()),
        gap,
    };
    if let Some(runtime) = bridge.runtime.as_mut() {
        runtime.drop_held_turn(&TaskId::new(pending.task_id.clone()));
    }
    bridge.pending_task_effects.remove(&pending.effect_id);
    submit_completion_command(bridge, pending, command, now_monotonic_ms)
}

fn invalid(pending: &PendingTaskEffect) -> ffi::BridgeResponse {
    response(
        &pending.operation.operation_id,
        wire::AdmissionStatus::InvalidCommand as u8,
        Vec::new(),
    )
}
