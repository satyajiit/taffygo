// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Composer-only CXX records kept outside the shared state bridge (decision
//! 0097).
//!
//! The operation record mirrors `BridgeOperation` field for field, the same
//! way the provider and account bridges mirror it: two cxx bridge modules
//! cannot share a by-value struct without an include cycle between their
//! generated headers, so the shape is duplicated under the plane's own name
//! and `service_bridge_composer.rs` converts it exactly once.
//!
//! Both directions live here, unlike every other plane, and that is decision
//! 0097 rather than a filing preference. A suggestion is offered, not true, so
//! it is never published state: nothing on this plane carries a `BridgeState`,
//! and so nothing on it needs the shared bridge's `BridgeResponse`. The plane
//! is therefore closed over its own module, and a later record that did need a
//! state would be a record that had stopped being a suggestion.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeComposerOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    /// The admission of one composer submission, mirroring `BridgeAdmission`
    /// for the same include-cycle reason the operation record is mirrored.
    struct BridgeComposerAdmission {
        operation_id: String,
        status: u8,
    }

    /// One request for a continuation of what a person is typing.
    ///
    /// Flat, like every other command record here, but with no `kind` field:
    /// this plane has exactly one command, so there is no discriminator for a
    /// body to disagree with. `has_suffix` is what keeps an absent suffix and
    /// an empty one apart across a bridge with no optional of its own — a
    /// caret at the end of the text is not a caret before an empty string, and
    /// the composition sends a second sentence only for the second.
    ///
    /// `prefix` and `suffix` are the person's own composer text and nothing
    /// else. No field here can carry a page, a history entry, a workspace fact
    /// or another conversation, which is a property of the record rather than
    /// a discipline of whoever fills it.
    struct BridgeComposerCommand {
        operation: BridgeComposerOperation,
        request_id: String,
        prefix: String,
        has_suffix: bool,
        suffix: String,
    }

    /// One request the surface has stopped waiting for (decision 0097 §3).
    ///
    /// A record of its own rather than the command record with its text
    /// fields left empty. A withdrawal carries no prefix, and a projection
    /// that had to tell "cancel this" from "continue nothing" by reading an
    /// empty string would be deciding what a person meant from an absence.
    ///
    /// It is answered with a `BridgeComposerSubmission`, which is the point:
    /// the browser already stops a dispatch a newer request displaced, and a
    /// withdrawal is that same instruction with nothing to dispatch beside it.
    /// A second answer shape would be a second way to stop an effect.
    struct BridgeComposerCancel {
        operation: BridgeComposerOperation,
        request_id: String,
    }

    /// One composer suggestion's model effect, flat (decision 0097).
    ///
    /// Field for field with `BridgeProbeEffect`, because both are one bounded
    /// model call that belongs to no task and cxx has no sum type; the browser
    /// rebuilds the typed `ModelRequestEffect` from exactly these fields. The
    /// one addition is `endpoint_kind`, which says which revalidation rule the
    /// core is claiming for the address. It is carried rather than assumed
    /// here because the claim is the core's: a suggestion may be spent on a
    /// provider a person defined themselves, and a bridge that stated the
    /// catalog rule on the core's behalf would be making a claim nobody made.
    struct BridgeComposerEffect {
        operation: BridgeComposerOperation,
        effect_id: String,
        retry_class: u8,
        route_id: String,
        model_id: String,
        disclosure: u8,
        request_body: Vec<u8>,
        max_output_bytes: u32,
        provider_id: String,
        wire_api: u8,
        endpoint: String,
        credential_handle: String,
        endpoint_kind: u8,
    }

    /// The answer to one submission: what to dispatch, and what to stop.
    ///
    /// `superseded_effect_id` names the dispatch a newer request displaced.
    /// It is named rather than forgotten because an effect nobody stops is an
    /// effect that still bills, and the core is the only party that knows
    /// which flight the new one replaced.
    struct BridgeComposerSubmission {
        admission: BridgeComposerAdmission,
        has_effect: bool,
        effect: BridgeComposerEffect,
        has_superseded: bool,
        superseded_effect_id: String,
    }

    /// The terminal of one suggestion dispatch, as the browser observed it.
    ///
    /// Unlike the probe's terminal this carries the reply body: a probe needs
    /// only a verdict, and this is the suggestion itself. The operation
    /// travels with it because the push the core composes is a new effect and
    /// an effect belongs to an operation.
    struct BridgeComposerCompletion {
        operation: BridgeComposerOperation,
        effect_id: String,
        status: u8,
        completion: Vec<u8>,
    }

    /// One suggestion on its way to a surface.
    ///
    /// `claimed` false means the composer wants nothing dispatched for this
    /// terminal: it answered a request the person has already typed past, it
    /// carried nothing usable, or it was never the composer's at all. The
    /// three are one answer here on purpose — none of them puts anything in
    /// front of a person — and the caller may hand an unclaimed terminal to
    /// another plane's delivery leg.
    ///
    /// `has_text` false is the ordinary answer when the model offered nothing
    /// usable, said rather than dropped so a surface can stop waiting. The
    /// composition claims a terminal only when it has text to hand over, so
    /// the pair does not vary today; it is kept apart because the contract
    /// record it becomes keeps it apart, and an empty string is not an absent
    /// suggestion.
    struct BridgeComposerDelivery {
        claimed: bool,
        operation: BridgeComposerOperation,
        effect_id: String,
        retry_class: u8,
        request_id: String,
        has_text: bool,
        text: String,
    }

    /// What became of one suggestion push, as the browser carried it out.
    ///
    /// `delivered` is the browser's answer to the one question the push asks —
    /// was any surface still watching this profile — and decision 0097's
    /// consequences require it to be asked so that a suggestion which was
    /// computed and never shown is distinguishable from one that was never
    /// computed. It crosses so that the answer reaches the party that asked;
    /// it goes no further, because a suggestion is journalled nowhere and
    /// charged to nothing, so there is no record for it to enter and no
    /// balance for it to move.
    ///
    /// The request identity is what the terminal is for. This is the last
    /// thing the core hears about a request, and the flight has to be settled
    /// by it or by nothing.
    struct BridgeComposerTerminal {
        request_id: String,
        delivered: bool,
    }
}
