// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The composer suggestion's four bridge legs (decision 0097).
//!
//! Submission composes one bounded model call from the person's own composer
//! text and hands it back for the browser to dispatch; delivery takes that
//! call's terminal and hands back the push that puts the suggestion in front
//! of the person; and the push's own terminal comes back here to close the
//! request. The first two are the key probe's shape — a model call that
//! belongs to no task — and differ from it in what they carry: a probe wants a
//! verdict, and this wants the words. The third exists because the push is a
//! browser effect like any other and every effect is answered, so the answer
//! has to be claimed by the plane that asked or it goes looking for an owner.
//!
//! The fourth is the withdrawal a surface states on its own: it dispatches
//! nothing and names the flight to stop. It answers with the *submission*
//! record rather than one of its own, because the browser already stops a
//! dispatch a newer request displaced and a withdrawal is that instruction
//! with nothing beside it.
//!
//! **No leg publishes, and that is not the delivery-bridge defect.**
//! That defect was a state change nobody could see: a pack downloaded,
//! verified and installed while every screen went on drawing the snapshot
//! taken at bootstrap, because `CoreStatus` reaches a surface only through
//! `response_after_change`. Nothing here changes published state at all. A
//! suggestion is offered rather than true (decision 0097 §6), it reaches its
//! surface as a `DELIVER_COMPOSER_COMPLETION` effect the browser carries out,
//! and the single-flight bookkeeping this plane does keep — which request is
//! running — is read by no status projection. Publishing here would put a
//! value that changes several times a second into every state generation, and
//! the sequence numbering the browser registers against is not free.
//!
//! All four entries are registered as publication-exempt in
//! `tools/check_publication_totality.py` with that argument, and that register
//! is checked in both directions: the day any leg does change published state,
//! the row has to go and the check fails until it does.

use core_runtime::composition::profile::completion::{
    CompletionCommandError, ComposerCancellation, ComposerCompletionPlan,
};
use core_runtime::wire as core_wire;

use crate::service_bridge_composer_ffi::ffi as composer_ffi;
use crate::service_bridge_runtime::ServiceBridge;

use self::wire::{
    cancel_to_wire, command_to_wire, operation_from_wire, operation_to_wire, validate_operation,
};

mod wire;

#[allow(non_snake_case)]
pub(crate) fn SubmitComposer(
    bridge: &mut ServiceBridge,
    command: composer_ffi::BridgeComposerCommand,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> composer_ffi::BridgeComposerSubmission {
    let operation_id = command.operation.operation_id.clone();
    if let Err(status) = validate_operation(
        &command.operation,
        bridge.generation.value(),
        now_monotonic_ms,
    ) {
        return refusal(&operation_id, status);
    }
    let Some(command) = command_to_wire(command) else {
        return refusal(&operation_id, invalid());
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return refusal(&operation_id, unavailable());
    };
    runtime.set_utc_millis(now_utc_millis);
    match runtime.submit_composer_completion_command(&command) {
        Ok(plan) => accepted(&operation_id, plan),
        Err(error) => refusal(&operation_id, command_error_status(error)),
    }
}

/// Withdraws one suggestion the surface has stopped waiting for.
///
/// The browser needs no new leg for this and must not be given one: a
/// withdrawal is the shape it already handles for a superseded request, so the
/// answer is one submission that dispatches nothing and names what to stop
/// (decision 0097 §3). Hence [`withdrawal`] rather than a response type of its
/// own — `has_effect` false with the superseded fields filled is the whole
/// instruction.
///
/// A withdrawal that stopped nothing is still accepted. The answer may already
/// have arrived, and refusing would make a surface that cancelled a moment too
/// late look like one that sent nonsense.
#[allow(non_snake_case)]
pub(crate) fn CancelComposer(
    bridge: &mut ServiceBridge,
    command: composer_ffi::BridgeComposerCancel,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> composer_ffi::BridgeComposerSubmission {
    let operation_id = command.operation.operation_id.clone();
    if let Err(status) = validate_operation(
        &command.operation,
        bridge.generation.value(),
        now_monotonic_ms,
    ) {
        return refusal(&operation_id, status);
    }
    let Some(command) = cancel_to_wire(command) else {
        return refusal(&operation_id, invalid());
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return refusal(&operation_id, unavailable());
    };
    runtime.set_utc_millis(now_utc_millis);
    match runtime.submit_composer_cancel_command(&command) {
        Ok(cancellation) => withdrawal(&operation_id, cancellation),
        Err(error) => refusal(&operation_id, command_error_status(error)),
    }
}

#[allow(non_snake_case)]
pub(crate) fn DeliverComposerCompletion(
    bridge: &mut ServiceBridge,
    completion: composer_ffi::BridgeComposerCompletion,
    now_utc_millis: u64,
) -> composer_ffi::BridgeComposerDelivery {
    let composer_ffi::BridgeComposerCompletion {
        operation,
        effect_id,
        status,
        completion,
    } = completion;
    let Some(status) = core_wire::EffectStatus::from_wire(u32::from(status)) else {
        return unclaimed();
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return unclaimed();
    };
    runtime.set_utc_millis(now_utc_millis);
    // A failed dispatch and an over-long reply both settle the flight with
    // nothing. Settling matters more than the reason: a flight left running
    // is one the composer would go on waiting for, and the person has no way
    // to ask again for the sentence they have already finished typing.
    let answered = status == core_wire::EffectStatus::Completed
        && completion.len() <= core_wire::MAX_COMPOSER_COMPLETION_BYTES;
    let body: &[u8] = if answered { &completion } else { &[] };
    let operation = operation_to_wire(operation);
    let Some(push) = runtime.deliver_composer_completion_result(&effect_id, body, &operation)
    else {
        return unclaimed();
    };
    let Some(suggestion) = push.composer_completion else {
        return unclaimed();
    };
    composer_ffi::BridgeComposerDelivery {
        claimed: true,
        operation: operation_from_wire(push.operation),
        effect_id: push.effect_id,
        retry_class: push.retry_class as u8,
        request_id: suggestion.request_id,
        has_text: suggestion.text.is_some(),
        text: suggestion.text.unwrap_or_default(),
    }
}

#[allow(non_snake_case)]
pub(crate) fn RecordComposerDelivery(
    bridge: &mut ServiceBridge,
    terminal: composer_ffi::BridgeComposerTerminal,
) {
    let composer_ffi::BridgeComposerTerminal {
        request_id,
        // Read and stopped here. The browser is required to say whether any
        // surface was still watching (decision 0097's consequences), and this
        // is where the answer arrives; it goes no further because a suggestion
        // is journalled nowhere and charged to nothing, so the core has no
        // record for it to enter and no balance for it to move. Dropping the
        // field from the record instead would leave the next reader unable to
        // tell a decision from an oversight.
        delivered: _,
    } = terminal;
    let Some(runtime) = bridge.runtime.as_mut() else {
        return;
    };
    // A push is the last thing the core hears about a request, so the flight
    // is settled here or by nothing. It has normally been settled already, by
    // the model terminal that composed this push; what this closes is the case
    // where it has not, which would be a composer waiting for an answer that
    // has already been given.
    runtime.record_composer_completion_delivery(&request_id);
}

/// Flattens the composed suggestion effect for the flat CXX record.
///
/// Field for field with the probe's flattening, and for the same reason: the
/// caller hands this the envelope the composition built, whose model request
/// is always present and always task-less. A malformed envelope flattens to
/// empty fields the browser's validation then refuses whole, rather than
/// panicking a process that carries other profiles' work.
fn effect_to_bridge(envelope: core_wire::EffectEnvelope) -> composer_ffi::BridgeComposerEffect {
    let request = envelope
        .model_request
        .unwrap_or(core_wire::ModelRequestEffect {
            route_id: String::new(),
            model_id: String::new(),
            disclosure: core_wire::DisclosureClass::ContentFree,
            request_body: Vec::new(),
            max_output_bytes: 0,
            task_id: String::new(),
            provider_id: String::new(),
            wire_api: core_wire::ProviderWireApi::AnthropicMessages,
            endpoint: String::new(),
            credential_handle: None,
            static_headers: Vec::new(),
            probe: false,
            endpoint_kind: core_wire::ModelEndpointKind::CatalogOrigin,
            media_attachment_handle: None,
            media_attachment_mime_type: None,
            not_before_monotonic_ms: 0,
        });
    composer_ffi::BridgeComposerEffect {
        operation: operation_from_wire(envelope.operation),
        effect_id: envelope.effect_id,
        retry_class: envelope.retry_class as u8,
        route_id: request.route_id,
        model_id: request.model_id,
        disclosure: request.disclosure as u8,
        request_body: request.request_body,
        max_output_bytes: request.max_output_bytes,
        provider_id: request.provider_id,
        wire_api: request.wire_api as u8,
        endpoint: request.endpoint,
        credential_handle: request.credential_handle.unwrap_or_default(),
        endpoint_kind: request.endpoint_kind as u8,
    }
}

fn accepted(
    operation_id: &str,
    plan: ComposerCompletionPlan,
) -> composer_ffi::BridgeComposerSubmission {
    composer_ffi::BridgeComposerSubmission {
        admission: composer_ffi::BridgeComposerAdmission {
            operation_id: operation_id.to_owned(),
            status: core_wire::AdmissionStatus::Accepted as u8,
        },
        has_effect: true,
        has_superseded: plan.superseded_effect_id.is_some(),
        superseded_effect_id: plan.superseded_effect_id.unwrap_or_default(),
        effect: effect_to_bridge(plan.effect),
    }
}

/// A submission that dispatches nothing and names what to stop.
///
/// The accepted half of a withdrawal, and deliberately the same record an
/// accepted submission answers with: the browser's instruction is already
/// "dispatch what is here, stop what is named", and a withdrawal is that
/// instruction with nothing to dispatch. Giving it a shape of its own would
/// give the browser a second way to stop an effect, which is how two ways come
/// to disagree.
///
/// `has_superseded` false means nothing was in flight for that identity — the
/// answer had already arrived, or the request was never running. That is an
/// accepted withdrawal and not a refusal.
fn withdrawal(
    operation_id: &str,
    cancellation: ComposerCancellation,
) -> composer_ffi::BridgeComposerSubmission {
    composer_ffi::BridgeComposerSubmission {
        admission: composer_ffi::BridgeComposerAdmission {
            operation_id: operation_id.to_owned(),
            status: core_wire::AdmissionStatus::Accepted as u8,
        },
        has_effect: false,
        effect: no_effect(),
        has_superseded: cancellation.withdrawn_effect_id.is_some(),
        superseded_effect_id: cancellation.withdrawn_effect_id.unwrap_or_default(),
    }
}

/// A submission that composed nothing.
fn refusal(operation_id: &str, status: u8) -> composer_ffi::BridgeComposerSubmission {
    composer_ffi::BridgeComposerSubmission {
        admission: composer_ffi::BridgeComposerAdmission {
            operation_id: operation_id.to_owned(),
            status,
        },
        has_effect: false,
        effect: no_effect(),
        has_superseded: false,
        superseded_effect_id: String::new(),
    }
}

/// A terminal the composer wants nothing dispatched for.
fn unclaimed() -> composer_ffi::BridgeComposerDelivery {
    composer_ffi::BridgeComposerDelivery {
        claimed: false,
        operation: empty_operation(),
        effect_id: String::new(),
        retry_class: 0,
        request_id: String::new(),
        has_text: false,
        text: String::new(),
    }
}

/// The no-effect answer. cxx structs have no optional, so the submission's
/// flag is what carries "nothing was composed" and these fields carry
/// nothing.
fn no_effect() -> composer_ffi::BridgeComposerEffect {
    composer_ffi::BridgeComposerEffect {
        operation: empty_operation(),
        effect_id: String::new(),
        retry_class: 0,
        route_id: String::new(),
        model_id: String::new(),
        disclosure: 0,
        request_body: Vec::new(),
        max_output_bytes: 0,
        provider_id: String::new(),
        wire_api: 0,
        endpoint: String::new(),
        credential_handle: String::new(),
        endpoint_kind: 0,
    }
}

fn empty_operation() -> composer_ffi::BridgeComposerOperation {
    composer_ffi::BridgeComposerOperation {
        operation_id: String::new(),
        service_generation: 0,
        task_revision: 0,
        deadline_monotonic_ms: 0,
        idempotency_key: String::new(),
    }
}

/// Which admission status one refusal is.
///
/// A profile with no usable credential is the ordinary case rather than a
/// failure — a person on the managed route alone reaches it on every pause in
/// typing, because a suggestion is never spent on a granted allowance
/// (decision 0097 §2). It is still a refusal, and it has to be one: the
/// admission is the only synchronous answer the composer gets, and an
/// `ACCEPTED` that dispatched nothing would leave a surface waiting for a
/// push that is never coming. The status enumeration cannot carry "there is
/// nothing here to spend", so this says only what is true of every arm — that
/// nothing was admitted — and the composer draws nothing, which is what
/// decision 0097 asks of it.
const fn command_error_status(error: CompletionCommandError) -> u8 {
    match error {
        CompletionCommandError::InvalidCommand | CompletionCommandError::NoUsableCredential => {
            invalid()
        }
        CompletionCommandError::IdentityExhausted => unavailable(),
    }
}

const fn invalid() -> u8 {
    core_wire::AdmissionStatus::InvalidCommand as u8
}

const fn unavailable() -> u8 {
    core_wire::AdmissionStatus::CoreUnavailable as u8
}
