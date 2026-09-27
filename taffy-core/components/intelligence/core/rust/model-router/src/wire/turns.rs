// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing the conversation: what was said, the tools the model asked for, and
//! the results it was given back.
//!
//! It is a module of its own rather than more of [`super::request`] because
//! this is where the four families stop agreeing. A conversation of text alone
//! is one shape spelled four ways; a conversation that replays an exchange is
//! four genuinely different documents — a block inside the turn that made it, a
//! list beside that turn, an item of the conversation with no role at all, a
//! part whose payload has to be an object. Every one of those differences is a
//! field of [`super::dialect`] read here, so the families stay four rows and
//! this file holds no per-family branch.
//!
//! Borrowed throughout, like the rest of `wire`. Nothing here allocates: every
//! piece of text goes from the caller's slice into the caller's buffer, and the
//! two families that want a call's arguments as a JSON document inside a string
//! get one escaped a character at a time rather than rendered into a value this
//! crate would have to own.

use super::dialect::{Dialect, MediaShape, SystemBlocks, SystemPlacement, TextPart};
use super::emit::Json;
use super::exchange::{write_called, write_returned};
use super::request::{Speaker, Turn, WireMediaAttachment, WireRequest};

/// Separator between two pieces of one turn on a family that has no blocks.
///
/// A blank line rather than nothing: run together, the end of one piece and
/// the start of the next read as a single sentence the model then answers as
/// one thought.
pub(super) const PLAIN_TEXT_JOIN: &str = "\n\n";

/// What a failed result is prefixed with on a family that cannot say a result
/// failed any other way.
///
/// Compiled in, so it cannot come from the tool that failed, and short enough
/// that it costs the answer nothing. Two of the four families have neither a
/// flag beside the payload nor a field of its own for a failure; dropping the
/// fact on those two would hand the model a failure shaped exactly like a
/// success, and the next thing it does is build on a result that never
/// happened.
pub(super) const FAILED_RESULT_MARKER: &str = "[the tool failed] ";

/// Writes the standing instruction wherever the family keeps it.
///
/// A subscription credential on one family requires a block ahead of the
/// instruction saying who is calling, and that family's instruction field
/// takes either a string or a list of blocks. The list form is written only
/// when there is a preamble to put in it, so a body written on a key
/// credential is byte for byte the string it always was.
pub(super) fn write_system(json: &mut Json<'_>, dialect: &Dialect, request: &WireRequest<'_>) {
    if let (Some(preamble), Some((key, blocks))) = (request.preamble(), dialect.preamble_shape()) {
        json.key(key);
        json.open_array();
        write_system_block(json, &blocks, preamble);
        if let Some(system) = request.system {
            write_system_block(json, &blocks, system);
        }
        json.close_array();
        return;
    }
    match dialect.system {
        SystemPlacement::TopLevelText { key } => {
            let Some(system) = request.system else { return };
            json.key(key);
            json.text(system);
        }
        SystemPlacement::TopLevelParts { key, role } => {
            // The row's own constant text counts as an instruction: the family
            // that declares one is read by its endpoint, so the object is
            // written for it even when the caller supplied nothing. A row that
            // declares none behaves exactly as it did — no prelude and no
            // instruction is no object.
            if dialect.system_prelude.is_empty() && request.system.is_none() {
                return;
            }
            json.key(key);
            json.open_object();
            if let Some(role) = role {
                json.key(dialect.role_key);
                json.text(role);
            }
            json.key(dialect.content_key);
            write_system_content(json, dialect, request.system);
            json.close_object();
        }
        // Written as the first element of the turn list instead, so that the
        // instruction and the conversation stay in the order the family reads
        // them.
        SystemPlacement::LeadingTurn { .. } => {}
    }
}

/// The instruction's content: the row's constant prelude, then the caller's.
///
/// Split from [`write_blocks`] rather than folded into it because the two
/// pieces come from different places and joining them into one slice would
/// mean this crate owning a value assembled from what the caller said.
fn write_system_content(json: &mut Json<'_>, dialect: &Dialect, system: Option<&str>) {
    let Some(part) = dialect.content.parts() else {
        // A family with no block shape has one string to put both in.
        json.open_text();
        for (index, piece) in dialect
            .system_prelude
            .iter()
            .copied()
            .chain(system)
            .enumerate()
        {
            if index > 0 {
                json.push_text(PLAIN_TEXT_JOIN);
            }
            json.push_text(piece);
        }
        json.close_text();
        return;
    };
    json.open_array();
    write_text_parts(json, &part, Speaker::User, dialect.system_prelude);
    if let Some(system) = system {
        write_text_parts(json, &part, Speaker::User, &[system]);
    }
    json.close_array();
}

/// Writes one block of a multi-piece standing instruction.
fn write_system_block(json: &mut Json<'_>, blocks: &SystemBlocks, text: &str) {
    json.open_object();
    json.key(blocks.type_key);
    json.text(blocks.tag);
    json.key(blocks.text_key);
    json.text(text);
    json.close_object();
}

/// Writes the conversation, oldest first.
pub(super) fn write_turns(
    json: &mut Json<'_>,
    dialect: &Dialect,
    request: &WireRequest<'_>,
    mut media: Option<WireMediaAttachment<'_>>,
) {
    json.key(dialect.turns_key);
    json.open_array();
    // How many of the model's own messages have been written. It is the whole
    // of the identity one family asks of a replayed message, and it is a
    // position inside this one body rather than an identity anything is keyed
    // on — the durable transcript never kept the item id the vendor minted,
    // and inventing a durable-looking one would be worse than counting.
    let mut said = 0u64;
    if let (SystemPlacement::LeadingTurn { role }, Some(system)) = (dialect.system, request.system)
    {
        write_said(
            json,
            dialect,
            role,
            Speaker::User,
            &[system],
            None,
            &mut said,
        );
    }
    for turn in request.turns {
        match turn {
            Turn::Said { speaker, text } => {
                let role = match speaker {
                    Speaker::User => dialect.user_role,
                    Speaker::Assistant => dialect.assistant_role,
                };
                let turn_media = if matches!(speaker, Speaker::User) {
                    media.take()
                } else {
                    None
                };
                write_said(json, dialect, role, *speaker, text, turn_media, &mut said);
            }
            Turn::Called {
                text,
                calls,
                reasoning,
            } => write_called(json, dialect, text, calls, reasoning, &mut said),
            Turn::Returned { results } => write_returned(json, dialect, results),
        }
    }
    json.close_array();
}

pub(super) fn write_said(
    json: &mut Json<'_>,
    dialect: &Dialect,
    role: &str,
    speaker: Speaker,
    text: &[&str],
    media: Option<WireMediaAttachment<'_>>,
    said: &mut u64,
) {
    // The envelope is the model's own, so it is written for the model's turns
    // and for nothing else. A person's turn is an input message on every
    // family, including this one.
    let envelope = match speaker {
        Speaker::Assistant => dialect.assistant_envelope,
        Speaker::User => None,
    };
    json.open_object();
    if let Some(envelope) = envelope {
        json.key(envelope.type_key);
        json.text(envelope.type_value);
    }
    json.key(dialect.role_key);
    json.text(role);
    json.key(dialect.content_key);
    if let Some(media) = media {
        write_mixed_media_blocks(json, dialect, text, media);
    } else {
        write_blocks(json, dialect, speaker, text);
    }
    if let Some(envelope) = envelope {
        json.key(envelope.status_key);
        json.text(envelope.status_value);
        json.key(envelope.id_key);
        json.open_text();
        json.push_text(envelope.id_prefix);
        json.push_text(&said.to_string());
        json.close_text();
        *said = said.saturating_add(1);
    }
    json.close_object();
}

/// Writes the first user turn as text parts followed by one visual part.
///
/// The handle is intentionally written verbatim as a JSON string value. It is
/// not base64 and it is never sent to the provider: the browser owns the
/// attachment store, requires this exact handle to occur once, and replaces
/// it with the claimed bytes immediately before network dispatch.
fn write_mixed_media_blocks(
    json: &mut Json<'_>,
    dialect: &Dialect,
    text: &[&str],
    media: WireMediaAttachment<'_>,
) {
    json.open_array();
    write_text_parts(json, &dialect.media.text_when_mixed, Speaker::User, text);
    write_media_part(json, dialect.media.shape, media);
    json.close_array();
}

fn write_media_part(json: &mut Json<'_>, shape: MediaShape, media: WireMediaAttachment<'_>) {
    json.open_object();
    match shape {
        MediaShape::Base64Source {
            type_key,
            type_value,
            source_key,
            source_type_key,
            source_type_value,
            mime_key,
            data_key,
        } => {
            json.key(type_key);
            json.text(type_value);
            json.key(source_key);
            json.open_object();
            json.key(source_type_key);
            json.text(source_type_value);
            json.key(mime_key);
            json.text(media.mime_type);
            json.key(data_key);
            json.text(media.handle);
            json.close_object();
        }
        MediaShape::DataUrl {
            type_key,
            type_value,
            url_key,
            nested_key,
        } => {
            json.key(type_key);
            json.text(type_value);
            if let Some(key) = nested_key {
                json.key(key);
                json.open_object();
            }
            json.key(url_key);
            json.open_text();
            json.push_text("data:");
            json.push_text(media.mime_type);
            json.push_text(";base64,");
            json.push_text(media.handle);
            json.close_text();
            if nested_key.is_some() {
                json.close_object();
            }
        }
        MediaShape::InlineBase64 {
            nested_key,
            mime_key,
            data_key,
        } => {
            json.key(nested_key);
            json.open_object();
            json.key(mime_key);
            json.text(media.mime_type);
            json.key(data_key);
            json.text(media.handle);
            json.close_object();
        }
    }
    json.close_object();
}

fn write_blocks(json: &mut Json<'_>, dialect: &Dialect, speaker: Speaker, text: &[&str]) {
    match dialect.content.parts() {
        Some(part) => {
            json.open_array();
            write_text_parts_with(
                json,
                &part,
                speaker,
                text,
                annotations_key(dialect, speaker),
            );
            json.close_array();
        }
        None => write_joined(json, text),
    }
}

/// The annotation key a block of this speaker's text carries, if any.
///
/// The model's own blocks only, and only on a family whose envelope names the
/// key: it is part of the same shape the envelope is, and a person's turn is
/// an input message everywhere.
const fn annotations_key(dialect: &Dialect, speaker: Speaker) -> Option<&'static str> {
    match (speaker, dialect.assistant_envelope) {
        (Speaker::Assistant, Some(envelope)) => Some(envelope.annotations_key),
        _ => None,
    }
}

/// Writes several pieces as one string, for a family that has no blocks.
pub(super) fn write_joined(json: &mut Json<'_>, text: &[&str]) {
    json.open_text();
    for (index, piece) in text.iter().enumerate() {
        if index > 0 {
            json.push_text(PLAIN_TEXT_JOIN);
        }
        json.push_text(piece);
    }
    json.close_text();
}

/// Writes the text elements of an already-open content array.
pub(super) fn write_text_parts(
    json: &mut Json<'_>,
    part: &TextPart,
    speaker: Speaker,
    text: &[&str],
) {
    write_text_parts_with(json, part, speaker, text, None);
}

/// The text elements, each optionally carrying an empty annotation list.
///
/// Empty because a replayed message carries no citations — what this process
/// kept is the text — and written all the same because the family that names
/// the key emits it, and a replay that omits it is a different shape from the
/// one it is replaying.
fn write_text_parts_with(
    json: &mut Json<'_>,
    part: &TextPart,
    speaker: Speaker,
    text: &[&str],
    annotations_key: Option<&'static str>,
) {
    let tag = match speaker {
        Speaker::User => part.user_tag,
        Speaker::Assistant => part.assistant_tag,
    };
    for piece in text {
        json.open_object();
        if let (Some(type_key), Some(tag)) = (part.type_key, tag) {
            json.key(type_key);
            json.text(tag);
        }
        json.key(part.text_key);
        json.text(piece);
        if let Some(key) = annotations_key {
            json.key(key);
            json.open_array();
            json.close_array();
        }
        json.close_object();
    }
}
