// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing a replayed tool exchange: the turn in which the model asked for
//! tools, and the turn in which it was given the answers.
//!
//! Split from [`super::turns`], which is now about what one turn *says* —
//! its role, its text blocks, and the envelope one family puts around the
//! model's own message. This file is about the other half: the four
//! genuinely different documents an exchange is. A call is a block inside
//! the turn that made it on one family, a list beside that turn on another,
//! an item of the conversation with no role at all on a third, and a part
//! whose payload has to be an object on the fourth. Every one of those
//! differences is a field of [`super::dialect`] read here, so the families
//! stay five rows and this file holds no per-family branch.
//!
//! Borrowed throughout, like the rest of `wire`. The two families that want
//! a call's arguments as a JSON document inside a string get one escaped a
//! character at a time rather than rendered into a value this crate would
//! have to own.

use super::dialect::{
    ArgumentsShape, CallPlacement, CallPosition, Dialect, FailureSignal, IdentityPosition,
    PartWrapping, ResultGrouping, ResultPayload, ResultPlacement,
};
use super::emit::Json;
use super::request::{OpaqueReasoning, Speaker, ToolCallReplay, ToolResultView};
use super::turns::{
    write_joined, write_said, write_text_parts, FAILED_RESULT_MARKER, PLAIN_TEXT_JOIN,
};

/// Writes the turn in which the model asked for tools.
///
/// Its text and its calls are one element of the conversation on three
/// families and two on the fourth, which is what
/// [`CallPosition::OwnElement`] says and why that case leaves before an
/// object is opened at all.
pub(super) fn write_called(
    json: &mut Json<'_>,
    dialect: &Dialect,
    text: &[&str],
    calls: &[ToolCallReplay<'_>],
    reasoning: &[OpaqueReasoning<'_>],
    said: &mut u64,
) {
    let placement = &dialect.calls;
    // Written first, and only on the row that declares a carriage. The family
    // that seals its reasoning produced it before it said anything and reads
    // the conversation back in the order it produced it; a row with no
    // carriage never writes one, so a caller that attached reasoning to the
    // wrong family cannot smuggle an item into a body that has no place for
    // it.
    if dialect.reasoning.is_some() {
        for item in reasoning {
            json.value(item.for_replay());
        }
    }
    if matches!(placement.position, CallPosition::OwnElement) {
        // The message element is written only when the turn said something. A
        // turn that only called a tool is the calls and nothing else on this
        // family; an element carrying an empty content array is a message the
        // model never produced.
        if !text.is_empty() {
            write_said(
                json,
                dialect,
                dialect.assistant_role,
                Speaker::Assistant,
                text,
                None,
                said,
            );
        }
        write_call_list(json, placement, calls);
        return;
    }
    json.open_object();
    json.key(dialect.role_key);
    json.text(dialect.assistant_role);
    json.key(dialect.content_key);
    match dialect.content.parts() {
        Some(part) => {
            json.open_array();
            write_text_parts(json, &part, Speaker::Assistant, text);
            if matches!(placement.position, CallPosition::InContent) {
                write_call_list(json, placement, calls);
            }
            json.close_array();
        }
        // A family with no blocks. `InContent` cannot reach this arm:
        // `super::content_holds_calls` asserts that pairing at compile time,
        // once per row. So the calls this turn made are the list written next.
        None => write_joined(json, text),
    }
    if let CallPosition::BesideContent { key } = placement.position {
        json.key(key);
        json.open_array();
        write_call_list(json, placement, calls);
        json.close_array();
    }
    json.close_object();
}

fn write_call_list(json: &mut Json<'_>, placement: &CallPlacement, calls: &[ToolCallReplay<'_>]) {
    for call in calls {
        write_call(json, placement, call);
    }
}

fn write_call(json: &mut Json<'_>, placement: &CallPlacement, call: &ToolCallReplay<'_>) {
    json.open_object();
    let nested = write_wrapping(json, placement.wrapping);
    write_identity(
        json,
        placement.call_id_key,
        placement.call_id_position,
        IdentityPosition::OnElement,
        call.call_id,
    );
    if let Some(key) = nested {
        json.key(key);
        json.open_object();
    }
    write_identity(
        json,
        placement.call_id_key,
        placement.call_id_position,
        IdentityPosition::InWrapper,
        call.call_id,
    );
    json.key(placement.name_key);
    json.text(call.tool);
    json.key(placement.arguments_key);
    match placement.arguments {
        ArgumentsShape::Value => json.value(call.arguments),
        ArgumentsShape::EmbeddedText => json.embedded_value(call.arguments),
    }
    if nested.is_some() {
        json.close_object();
    }
    json.close_object();
}

/// Writes the identity where the row says it goes, and nowhere else.
///
/// Called twice per object, once either side of the wrapper, so that "which
/// side" is the row's answer rather than two writers each holding half of it.
/// A family that mints no identity writes nothing at either call.
fn write_identity(
    json: &mut Json<'_>,
    key: Option<&'static str>,
    position: IdentityPosition,
    here: IdentityPosition,
    call_id: &str,
) {
    if position != here {
        return;
    }
    if let Some(key) = key {
        json.key(key);
        json.text(call_id);
    }
}

/// Writes whatever announces a call or a result, and answers with the key its
/// name and payload nest under when the family nests them.
fn write_wrapping(json: &mut Json<'_>, wrapping: PartWrapping) -> Option<&'static str> {
    match wrapping {
        PartWrapping::Direct => None,
        PartWrapping::Tagged { tag } => {
            json.key(tag.key);
            json.text(tag.value);
            None
        }
        PartWrapping::Nested { key } => Some(key),
        PartWrapping::TaggedAndNested { tag, key } => {
            json.key(tag.key);
            json.text(tag.value);
            Some(key)
        }
    }
}

pub(super) fn write_returned(
    json: &mut Json<'_>,
    dialect: &Dialect,
    results: &[ToolResultView<'_>],
) {
    let placement = &dialect.results;
    match placement.grouping {
        ResultGrouping::SharedTurn { role } => {
            json.open_object();
            json.key(dialect.role_key);
            json.text(role);
            json.key(dialect.content_key);
            json.open_array();
            for result in results {
                write_result(json, dialect, placement, None, result);
            }
            json.close_array();
            json.close_object();
        }
        ResultGrouping::OwnElement { role } => {
            for result in results {
                write_result(json, dialect, placement, role, result);
            }
        }
    }
}

fn write_result(
    json: &mut Json<'_>,
    dialect: &Dialect,
    placement: &ResultPlacement,
    role: Option<&str>,
    result: &ToolResultView<'_>,
) {
    json.open_object();
    if let Some(role) = role {
        json.key(dialect.role_key);
        json.text(role);
    }
    let nested = write_wrapping(json, placement.wrapping);
    write_identity(
        json,
        placement.call_id_key,
        placement.call_id_position,
        IdentityPosition::OnElement,
        result.call_id,
    );
    if let Some(key) = nested {
        json.key(key);
        json.open_object();
    }
    write_identity(
        json,
        placement.call_id_key,
        placement.call_id_position,
        IdentityPosition::InWrapper,
        result.call_id,
    );
    // Written on the two families whose shape pairs a result to a call by the
    // tool's name — one of which has nowhere to carry an identity at all, and
    // one of which carries both because the endpoint reading it needs the name
    // and the endpoint it forwards to needs the identity.
    if let Some(key) = placement.name_key {
        json.key(key);
        json.text(result.tool);
    }
    write_payload(json, placement, result);
    if nested.is_some() {
        json.close_object();
    }
    json.close_object();
}

fn write_payload(json: &mut Json<'_>, placement: &ResultPlacement, result: &ToolResultView<'_>) {
    match placement.payload {
        ResultPayload::Text { key } => {
            json.key(payload_key(placement.failure, result.is_error, key));
            write_result_text(json, placement.failure, result);
        }
        ResultPayload::Object { key, text_key } => {
            json.key(key);
            json.open_object();
            json.key(payload_key(placement.failure, result.is_error, text_key));
            write_result_text(json, placement.failure, result);
            json.close_object();
        }
    }
    if let (FailureSignal::Flag { key }, true) = (placement.failure, result.is_error) {
        json.key(key);
        json.boolean(true);
    }
}

/// The field the result text arrives under, which one family changes when the
/// call failed.
fn payload_key(failure: FailureSignal, is_error: bool, ok: &'static str) -> &'static str {
    match failure {
        FailureSignal::PayloadKey { key } if is_error => key,
        FailureSignal::Flag { .. }
        | FailureSignal::PayloadKey { .. }
        | FailureSignal::FoldedIntoText => ok,
    }
}

fn write_result_text(json: &mut Json<'_>, failure: FailureSignal, result: &ToolResultView<'_>) {
    json.open_text();
    if result.is_error && matches!(failure, FailureSignal::FoldedIntoText) {
        json.push_text(FAILED_RESULT_MARKER);
    }
    for (index, piece) in result.text.iter().enumerate() {
        if index > 0 {
            json.push_text(PLAIN_TEXT_JOIN);
        }
        json.push_text(piece);
    }
    json.close_text();
}
