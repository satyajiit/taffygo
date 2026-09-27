// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::context::{ArenaNode, PageArena};

use super::{BoundedOutput, ExportMetadata};

pub(super) fn render(
    arena: &PageArena,
    metadata: &ExportMetadata<'_>,
    limit: usize,
) -> Result<Vec<u8>, ()> {
    let mut out = BoundedOutput::new(limit);
    out.push("{\"format\":\"taffy-page-snapshot-v1\",\"nodes\":[")?;
    for (index, node) in arena.nodes().iter().enumerate() {
        if index > 0 {
            out.push(",")?;
        }
        render_node(&mut out, node, index)?;
    }
    out.push("],\"provenance\":{")?;
    render_provenance(&mut out, metadata)?;
    out.push("}}")?;
    Ok(out.finish())
}

fn render_node(out: &mut BoundedOutput, node: &ArenaNode, index: usize) -> Result<(), ()> {
    out.push("{\"actions\":[")?;
    for (action_index, action) in node.actions.iter().enumerate() {
        if action_index > 0 {
            out.push(",")?;
        }
        string(out, action.wire())?;
    }
    out.push("],\"content_signals\":[")?;
    for (signal_index, signal) in node.content_signals.iter().enumerate() {
        if signal_index > 0 {
            out.push(",")?;
        }
        string(out, signal.wire())?;
    }
    out.push("],\"content_trust\":")?;
    string(out, node.content_trust.wire())?;
    out.push(",\"display_id\":\"node-")?;
    out.push_u64(u64::try_from(index).map_err(|_| ())?.saturating_add(1))?;
    out.push("\"")?;
    out.push(",\"name\":")?;
    if node.name_withheld {
        out.push("{\"withheld\":true}")?;
    } else if let Some(name) = &node.name {
        out.push("{\"text\":")?;
        string(out, name)?;
        out.push(",\"withheld\":false}")?;
    } else {
        out.push("{\"withheld\":false}")?;
    }
    out.push(",\"role\":")?;
    string(out, node.role.wire())?;
    out.push(",\"sensitivity\":")?;
    string(out, node.sensitivity.wire())?;
    out.push(",\"states\":[")?;
    for (state_index, state) in node.states.iter().enumerate() {
        if state_index > 0 {
            out.push(",")?;
        }
        string(out, state.wire())?;
    }
    out.push("],\"text\":{")?;
    out.push("\"declared_bytes\":")?;
    out.push_u64(node.declared_text_bytes)?;
    out.push(",\"declared_runs\":")?;
    out.push_u64(u64::from(node.declared_text_runs))?;
    out.push(",\"run_metadata\":[")?;
    for (run_index, run) in node.text.iter().enumerate() {
        if run_index > 0 {
            out.push(",")?;
        }
        out.push("{\"content_signals\":[")?;
        for (signal_index, signal) in run.content_signals.iter().enumerate() {
            if signal_index > 0 {
                out.push(",")?;
            }
            string(out, signal.wire())?;
        }
        out.push("],\"content_trust\":")?;
        string(out, run.content_trust.wire())?;
        out.push("}")?;
    }
    out.push("],\"runs\":[")?;
    for (run_index, run) in node.text.iter().enumerate() {
        if run_index > 0 {
            out.push(",")?;
        }
        string(out, &run.text)?;
    }
    out.push("],\"withheld\":")?;
    boolean(out, node.text_withheld)?;
    out.push("}}")
}

fn render_provenance(out: &mut BoundedOutput, metadata: &ExportMetadata<'_>) -> Result<(), ()> {
    out.push("\"captured_at_epoch_ms\":")?;
    out.push_u64(metadata.captured_at_epoch_ms)?;
    out.push(",\"graph_revision\":")?;
    out.push_u64(metadata.graph_revision)?;
    out.push(",\"node_count\":")?;
    out.push_u64(u64::from(metadata.node_count))?;
    out.push(",\"origin\":")?;
    string(out, metadata.origin)?;
    out.push(",\"redacted_field_count\":")?;
    out.push_u64(u64::from(metadata.redacted_field_count))?;
    out.push(",\"secure_context\":")?;
    boolean(out, metadata.secure_context)?;
    out.push(",\"source_fragment_withheld\":")?;
    boolean(out, metadata.source_fragment_withheld)?;
    out.push(",\"source_query_withheld\":")?;
    boolean(out, metadata.source_query_withheld)?;
    out.push(",\"source_url_disclosure\":\"origin_only\"")?;
    out.push(",\"suppressed_secret_value_count\":")?;
    out.push_u64(u64::from(metadata.suppressed_secret_value_count))?;
    out.push(",\"withheld_field_count\":")?;
    out.push_u64(u64::from(metadata.withheld_field_count))?;
    Ok(())
}

fn boolean(out: &mut BoundedOutput, value: bool) -> Result<(), ()> {
    out.push(if value { "true" } else { "false" })
}

fn string(out: &mut BoundedOutput, value: &str) -> Result<(), ()> {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    out.push("\"")?;
    let mut plain_start = 0;
    for (offset, character) in value.char_indices() {
        if !json_requires_escape(character) {
            continue;
        }
        out.push(value.get(plain_start..offset).ok_or(())?)?;
        match character {
            '"' => out.push("\\\"")?,
            '\\' => out.push("\\\\")?,
            '\u{08}' => out.push("\\b")?,
            '\u{0c}' => out.push("\\f")?,
            '\n' => out.push("\\n")?,
            '\r' => out.push("\\r")?,
            '\t' => out.push("\\t")?,
            value if value <= '\u{1f}' => {
                let code = u32::from(value);
                out.push("\\u00")?;
                out.push(
                    char::from(*HEX.get((code >> 4) as usize).ok_or(())?).encode_utf8(&mut [0; 4]),
                )?;
                out.push(
                    char::from(*HEX.get((code & 0x0f) as usize).ok_or(())?)
                        .encode_utf8(&mut [0; 4]),
                )?;
            }
            _ => return Err(()),
        }
        plain_start = offset.checked_add(character.len_utf8()).ok_or(())?;
    }
    out.push(value.get(plain_start..).ok_or(())?)?;
    out.push("\"")
}

const fn json_requires_escape(value: char) -> bool {
    value == '"' || value == '\\' || value <= '\u{1f}'
}
