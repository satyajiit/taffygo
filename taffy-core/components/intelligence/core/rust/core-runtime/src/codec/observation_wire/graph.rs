// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded parser for `EncodeGraphPayload` framing version 4.

use std::collections::BTreeSet;

use bip_types::action::ActionType;
use bip_types::snapshot::{
    ContentSignal, ContentTrust, NodeState, RelationshipKind, SemanticRole, Sensitivity,
    SourceKind, ValueKind,
};
use task_engine::ObservationGraphSummary;

use crate::context::{ArenaNode, ArenaTextRun, DestinationClass, PageArena};

const FRAMING_VERSION: u8 = 4;
const KNOWN_NODE_FLAGS: u8 = 0x3f;
const NAME_WITHHELD: u8 = 1 << 0;
const SECRET_WITHHELD: u8 = 1 << 3;
const VALUE_STATES_WITHHELD: u8 = 1 << 4;
const TEXT_WITHHELD: u8 = 1 << 5;
const MAX_IDENTIFIER_BYTES: usize = 128;
const MAX_NAME_BYTES: usize = 1_024;
const MAX_TEXT_RUNS_PER_NODE: u32 = 512;
const MAX_CONTENT_SIGNALS: u32 = 8;

/// Bounds on the text version 3 added, matching the encoder's own
/// (`kMaxBipNodeTextBytes`). Both sides are compiled-in literals in different
/// languages and nothing compares them, so a disagreement shows up as every
/// observation failing to decode rather than as a mismatch anybody is told
/// about.
const MAX_TEXT_BYTES_PER_NODE: usize = 8 * 1024;

/// Flags one text run may carry. Only the renderer's own truncation marker is
/// defined, so anything else is an encoder this build does not understand.
const KNOWN_TEXT_RUN_FLAGS: u8 = 0x01;
const MIN_EDGE_BYTES: usize = 9;

/// Both lists are canonicalised by the renderer's graph store — sorted and
/// made unique — so a list longer than its enumeration is a defect upstream
/// rather than an unusually rich node, and it is refused as one.
const MAX_ACTIONS_PER_NODE: u32 = 7;
const MAX_STATES_PER_NODE: u32 = 18;

/// Destination bits the browser writes. Bit 0 says a destination exists at
/// all; the other three describe the class of place it leads to. No URL
/// crosses this framing in any form, so these four bits are the entire
/// vocabulary a consumer has for "where does this go".
const DESTINATION_PRESENT: u8 = 1 << 0;
const KNOWN_DESTINATION_FLAGS: u8 = 0x0f;

/// True when a state says what a node's value currently is rather than what
/// kind of node it is. It mirrors the browser's own classification, and it is
/// repeated here rather than trusted because the point of checking is that the
/// sending side might not have done it.
const fn state_describes_the_value(state: NodeState) -> bool {
    matches!(
        state,
        NodeState::Checked | NodeState::Unchecked | NodeState::Mixed | NodeState::Selected
    )
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) struct GraphDecodeError;

pub(super) fn decode(
    payload: &[u8],
    expected_schema: &str,
    expected_frame: &str,
    mut arena: Option<&mut PageArena>,
) -> Result<ObservationGraphSummary, GraphDecodeError> {
    let mut reader = Reader::new(payload);
    if reader.u8()? != FRAMING_VERSION
        || reader.short(MAX_IDENTIFIER_BYTES, false)? != expected_schema
    {
        return Err(GraphDecodeError);
    }
    let node_count = reader.u32()?;
    if usize::try_from(node_count).map_or(true, |count| {
        count > core_service_types::MAX_TASK_OBSERVATION_NODES
    }) {
        return Err(GraphDecodeError);
    }
    let mut nodes = BTreeSet::new();
    let mut named_node_count = 0_u32;
    let mut text_run_count = 0_u64;
    let mut text_byte_count = 0_u64;
    for _ in 0..node_count {
        let row = decode_node_row(
            &mut reader,
            expected_frame,
            &mut nodes,
            arena.as_deref_mut(),
        )?;
        if row.named {
            named_node_count = named_node_count.checked_add(1).ok_or(GraphDecodeError)?;
        }
        text_run_count = text_run_count
            .checked_add(u64::from(row.text_runs))
            .ok_or(GraphDecodeError)?;
        text_byte_count = text_byte_count
            .checked_add(row.text_bytes)
            .ok_or(GraphDecodeError)?;
        if usize::try_from(text_byte_count).map_or(true, |count| {
            count > core_service_types::MAX_TASK_OBSERVATION_TEXT_BYTES
        }) {
            return Err(GraphDecodeError);
        }
    }
    let relationship_count = reader.u32()?;
    if usize::try_from(relationship_count)
        .map_or(true, |count| count > reader.remaining() / MIN_EDGE_BYTES)
    {
        return Err(GraphDecodeError);
    }
    let mut contains = Vec::new();
    for _ in 0..relationship_count {
        let from = reader.short(MAX_IDENTIFIER_BYTES, true)?;
        let to = reader.short(MAX_IDENTIFIER_BYTES, true)?;
        if !nodes.contains(from) || !nodes.contains(to) {
            return Err(GraphDecodeError);
        }
        let kind = closed(RelationshipKind::ALL, reader.u16()?)?;
        if reader.u8()? > 1 {
            return Err(GraphDecodeError);
        }
        if kind == RelationshipKind::Contains {
            contains.push((from, to));
        }
    }
    if reader.remaining() != 0 {
        return Err(GraphDecodeError);
    }
    if let Some(arena) = arena.as_mut() {
        arena.apply_contains(&contains);
    }
    Ok(ObservationGraphSummary {
        node_count,
        relationship_count,
        named_node_count,
        text_run_count,
        text_byte_count,
    })
}

/// What one node row contributes to the summary. Deliberately not the row
/// itself: this parser reads every field so that a malformed one is refused,
/// and keeps only the three numbers the summary is made of. Page text and node
/// names are read and dropped here, which is the point — the durable record
/// this feeds holds counts and nothing a page wrote.
struct NodeRow {
    named: bool,
    text_runs: u32,
    text_bytes: u64,
}

/// The text a node was allowed to carry, checked against what it claimed.
///
/// Split out of the row parser because it is the one part of a row that is a
/// list of its own with its own rules, and because the row parser is otherwise
/// a flat sequence of reads that is easier to audit without a nested loop in
/// the middle of it.
///
/// Every rule the encoder applied is checked here, because this is the boundary
/// the checking is for. The count is what the payload actually holds and cannot
/// exceed what the node claimed to have; a run may not arrive at all on a node
/// whose name was withheld, since a node too sensitive to label is too
/// sensitive to quote; a run's own sensitivity must say it was safe to carry;
/// and the total must stay inside the per-node bound. A payload that breaks any
/// of them did not come from the encoder this file is the other half of, and it
/// is refused whole.
fn decode_text_block(
    reader: &mut Reader<'_>,
    flags: u8,
    declared_runs: u32,
) -> Result<Vec<ArenaTextRun>, GraphDecodeError> {
    let emitted_runs = reader.u32()?;
    if emitted_runs > declared_runs {
        return Err(GraphDecodeError);
    }
    if emitted_runs > 0 && (flags & NAME_WITHHELD != 0 || flags & SECRET_WITHHELD != 0) {
        return Err(GraphDecodeError);
    }
    // Silence is the one thing this framing may not do. A node that had text and
    // sent less than all of it says so; a node that sent everything it had may
    // not claim otherwise. Without both directions a consumer cannot tell "the
    // page does not say" from "you were not told", and it goes looking again
    // for something it has already been refused.
    if (emitted_runs < declared_runs) != (flags & TEXT_WITHHELD != 0) {
        return Err(GraphDecodeError);
    }
    let mut emitted_text_bytes = 0_usize;
    let mut texts =
        Vec::with_capacity(usize::try_from(emitted_runs).map_err(|_| GraphDecodeError)?);
    for _ in 0..emitted_runs {
        let text = reader.short(MAX_TEXT_BYTES_PER_NODE, false)?;
        emitted_text_bytes = emitted_text_bytes
            .checked_add(text.len())
            .filter(|total| *total <= MAX_TEXT_BYTES_PER_NODE)
            .ok_or(GraphDecodeError)?;
        closed(SourceKind::ALL, u16::from(reader.u8()?))?;
        if closed(Sensitivity::ALL, u16::from(reader.u8()?))? != Sensitivity::NotSensitive {
            return Err(GraphDecodeError);
        }
        if reader.u8()? & !KNOWN_TEXT_RUN_FLAGS != 0 {
            return Err(GraphDecodeError);
        }
        let content_trust = decode_content_trust(reader)?;
        let content_signals = decode_content_signals(reader)?;
        texts.push(ArenaTextRun {
            text: text.to_owned(),
            content_trust,
            content_signals,
        });
    }
    Ok(texts)
}

/// Reads one browser-normalized authorship label.
///
/// The browser already replaces an absent renderer label with
/// `UnknownUntrusted` and refuses labels only trusted product layers may mint.
/// Repeating the refusal here is intentional: the isolated consumer does not
/// take the process that framed the payload at its word about the field that
/// determines how the bytes are presented to a model.
fn decode_content_trust(reader: &mut Reader<'_>) -> Result<ContentTrust, GraphDecodeError> {
    let trust = closed(ContentTrust::ALL, u16::from(reader.u8()?))?;
    if matches!(
        trust,
        ContentTrust::UserAuthored | ContentTrust::TaffyAuthored | ContentTrust::ModelAuthored
    ) {
        return Err(GraphDecodeError);
    }
    Ok(trust)
}

/// Reads a canonical set of signals: bounded, closed, strictly increasing.
///
/// Strict ordering makes duplicates and alternate spellings of the same set
/// malformed rather than giving one page several byte representations. That
/// keeps model context and any digest built over the framing deterministic.
fn decode_content_signals(reader: &mut Reader<'_>) -> Result<Vec<ContentSignal>, GraphDecodeError> {
    let count = reader.u32()?;
    if count > MAX_CONTENT_SIGNALS {
        return Err(GraphDecodeError);
    }
    let mut signals = Vec::with_capacity(usize::try_from(count).map_err(|_| GraphDecodeError)?);
    let mut previous = None;
    for _ in 0..count {
        let wire = reader.u8()?;
        if previous.is_some_and(|prior| prior >= wire) {
            return Err(GraphDecodeError);
        }
        signals.push(closed(ContentSignal::ALL, u16::from(wire))?);
        previous = Some(wire);
    }
    Ok(signals)
}

fn decode_node_row<'a>(
    reader: &mut Reader<'a>,
    expected_frame: &str,
    nodes: &mut BTreeSet<&'a str>,
    arena: Option<&mut PageArena>,
) -> Result<NodeRow, GraphDecodeError> {
    let node_id = reader.short(MAX_IDENTIFIER_BYTES, true)?;
    let frame_id = reader.short(MAX_IDENTIFIER_BYTES, true)?;
    // The frame every row must name is the one the envelope named, and the
    // envelope's is the browser's. This is what stops a row claiming to belong
    // to a frame the task was never granted, so it is never relaxed to make a
    // payload decode.
    if frame_id != expected_frame || !nodes.insert(node_id) {
        return Err(GraphDecodeError);
    }
    let role = closed(SemanticRole::ALL, reader.u16()?)?;
    let sensitivity = closed(Sensitivity::ALL, u16::from(reader.u8()?))?;
    let flags = reader.u8()?;
    if flags & !KNOWN_NODE_FLAGS != 0 {
        return Err(GraphDecodeError);
    }
    let value_kind = closed(ValueKind::ALL, u16::from(reader.u8()?))?;
    let safe_name = reader.short(MAX_NAME_BYTES, false)?;
    if (!safe_name.is_empty()
        && (sensitivity != Sensitivity::NotSensitive
            || flags & (NAME_WITHHELD | SECRET_WITHHELD) != 0))
        || (flags & NAME_WITHHELD != 0 && !safe_name.is_empty())
        || ((value_kind == ValueKind::SecretWithheld) != (flags & SECRET_WITHHELD != 0))
    {
        return Err(GraphDecodeError);
    }
    let text_runs = reader.u32()?;
    if text_runs > MAX_TEXT_RUNS_PER_NODE {
        return Err(GraphDecodeError);
    }
    let text_bytes = reader.u64()?;

    // --- framing version 2 ----------------------------------------------
    //
    // What the node can be asked to do. Every member is checked against the
    // closed enumeration, so a value this build does not know fails the whole
    // payload rather than being dropped from one node's list: a silently
    // shortened list of available actions is a node that looks less capable
    // than it is, and a consumer would then propose the wrong verb with no way
    // to tell it had been given a partial answer.
    let action_count = reader.u32()?;
    if action_count > MAX_ACTIONS_PER_NODE {
        return Err(GraphDecodeError);
    }
    let mut actions =
        Vec::with_capacity(usize::try_from(action_count).map_err(|_| GraphDecodeError)?);
    for _ in 0..action_count {
        actions.push(closed(ActionType::ALL, reader.u16()?)?);
    }

    // The states that were allowed to cross. A value-bearing state on a node
    // the browser called sensitive is one the browser must already have
    // withheld — on such a node the state IS the value — so meeting one here
    // means either the sending side skipped its own rule or the bytes did not
    // come from it. Either way this is the last place that can refuse.
    let state_count = reader.u32()?;
    if state_count > MAX_STATES_PER_NODE {
        return Err(GraphDecodeError);
    }
    let value_states_may_cross =
        sensitivity == Sensitivity::NotSensitive && flags & VALUE_STATES_WITHHELD == 0;
    let mut states =
        Vec::with_capacity(usize::try_from(state_count).map_err(|_| GraphDecodeError)?);
    for _ in 0..state_count {
        let state = closed(NodeState::ALL, u16::from(reader.u8()?))?;
        if state_describes_the_value(state) && !value_states_may_cross {
            return Err(GraphDecodeError);
        }
        states.push(state);
    }

    // Four bits and no address. An unknown bit is an encoder this build does
    // not understand, and a class bit with no destination beneath it describes
    // somewhere that does not exist.
    let destination = reader.u8()?;
    if destination & !KNOWN_DESTINATION_FLAGS != 0
        || (destination != 0 && destination & DESTINATION_PRESENT == 0)
    {
        return Err(GraphDecodeError);
    }

    // --- framing version 4 ----------------------------------------------
    // Authorship is normalized by the browser and signals are a canonical
    // closed set rather than a bit mask, so an unknown detector cannot be
    // silently ignored by an older consumer.
    let content_trust = decode_content_trust(reader)?;
    let content_signals = decode_content_signals(reader)?;

    let texts = decode_text_block(reader, flags, text_runs)?;

    // The arena is filled only after every check above has passed, so a node
    // that was refused for any reason contributes nothing to it. A refusal
    // fails the whole payload anyway; building the node first would leave the
    // ordering of those two facts as something a later edit could get wrong.
    if let Some(arena) = arena {
        let pushed = arena.push(ArenaNode {
            node_id: node_id.to_owned(),
            role,
            sensitivity,
            name: (!safe_name.is_empty()).then(|| safe_name.to_owned()),
            name_withheld: flags & NAME_WITHHELD != 0,
            actions,
            states,
            destination: DestinationClass::from_bits(destination),
            content_trust,
            content_signals,
            text: texts,
            text_withheld: flags & TEXT_WITHHELD != 0,
            declared_text_runs: text_runs,
            declared_text_bytes: text_bytes,
            container: None,
        });
        if !pushed {
            // The arena's bounds are the contract's own and this payload
            // already passed them one layer down, so a refusal here means the
            // two disagree. Failing is the only honest answer: an arena short
            // of the page it claims to hold would report a readable page as
            // unreadable, or an unreadable one as empty.
            return Err(GraphDecodeError);
        }
    }

    Ok(NodeRow {
        named: !safe_name.is_empty(),
        text_runs,
        text_bytes,
    })
}

fn closed<T: Copy>(values: &[T], wire: u16) -> Result<T, GraphDecodeError> {
    values
        .get(usize::from(wire))
        .copied()
        .ok_or(GraphDecodeError)
}

struct Reader<'a> {
    payload: &'a [u8],
    offset: usize,
}

impl<'a> Reader<'a> {
    const fn new(payload: &'a [u8]) -> Self {
        Self { payload, offset: 0 }
    }

    fn remaining(&self) -> usize {
        self.payload.len().saturating_sub(self.offset)
    }

    fn take(&mut self, count: usize) -> Result<&'a [u8], GraphDecodeError> {
        let end = self.offset.checked_add(count).ok_or(GraphDecodeError)?;
        let value = self.payload.get(self.offset..end).ok_or(GraphDecodeError)?;
        self.offset = end;
        Ok(value)
    }

    fn u8(&mut self) -> Result<u8, GraphDecodeError> {
        self.take(1)?.first().copied().ok_or(GraphDecodeError)
    }

    fn u16(&mut self) -> Result<u16, GraphDecodeError> {
        let bytes: [u8; 2] = self.take(2)?.try_into().map_err(|_| GraphDecodeError)?;
        Ok(u16::from_le_bytes(bytes))
    }

    fn u32(&mut self) -> Result<u32, GraphDecodeError> {
        let bytes: [u8; 4] = self.take(4)?.try_into().map_err(|_| GraphDecodeError)?;
        Ok(u32::from_le_bytes(bytes))
    }

    fn u64(&mut self) -> Result<u64, GraphDecodeError> {
        let bytes: [u8; 8] = self.take(8)?.try_into().map_err(|_| GraphDecodeError)?;
        Ok(u64::from_le_bytes(bytes))
    }

    fn short(&mut self, maximum: usize, required: bool) -> Result<&'a str, GraphDecodeError> {
        let length = usize::from(self.u16()?);
        if length > maximum || (required && length == 0) {
            return Err(GraphDecodeError);
        }
        core::str::from_utf8(self.take(length)?).map_err(|_| GraphDecodeError)
    }
}
