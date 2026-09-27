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
    out.push("# TaffyGo page snapshot\n\n")?;
    out.push("## Provenance\n\n")?;
    field(&mut out, "Origin", metadata.origin)?;
    out.push("- URL disclosure: origin only\n- Captured at (Unix epoch ms): ")?;
    out.push_u64(metadata.captured_at_epoch_ms)?;
    out.push("\n- Source query withheld: ")?;
    out.push(if metadata.source_query_withheld {
        "yes"
    } else {
        "no"
    })?;
    out.push("\n- Source fragment withheld: ")?;
    out.push(if metadata.source_fragment_withheld {
        "yes"
    } else {
        "no"
    })?;
    out.push("\n- Graph revision: ")?;
    out.push_u64(metadata.graph_revision)?;
    out.push("\n- Secure context: ")?;
    out.push(if metadata.secure_context { "yes" } else { "no" })?;
    out.push("\n- Nodes: ")?;
    out.push_u64(u64::from(metadata.node_count))?;
    out.push("\n- Redacted fields: ")?;
    out.push_u64(u64::from(metadata.redacted_field_count))?;
    out.push("\n- Suppressed secret values: ")?;
    out.push_u64(u64::from(metadata.suppressed_secret_value_count))?;
    out.push("\n- Withheld fields: ")?;
    out.push_u64(u64::from(metadata.withheld_field_count))?;
    out.push("\n\n## Page semantics\n")?;
    for (index, node) in arena.nodes().iter().enumerate() {
        node_section(&mut out, node, index)?;
    }
    Ok(out.finish())
}

fn node_section(out: &mut BoundedOutput, node: &ArenaNode, index: usize) -> Result<(), ()> {
    out.push("\n### Node ")?;
    out.push_u64(u64::try_from(index).map_err(|_| ())?.saturating_add(1))?;
    out.push("\n\n- Role: `")?;
    out.push(node.role.wire())?;
    out.push("`\n- Sensitivity: `")?;
    out.push(node.sensitivity.wire())?;
    out.push("`\n- Content trust: `")?;
    out.push(node.content_trust.wire())?;
    annotations(out, node)?;
    out.push("\n- Name: ")?;
    match (&node.name, node.name_withheld) {
        (_, true) => out.push("[withheld]"),
        (Some(name), false) => escaped(out, name),
        (None, false) => out.push("[none]"),
    }?;
    text(out, node)?;
    actions(out, node)?;
    out.push("\n")
}

fn annotations(out: &mut BoundedOutput, node: &ArenaNode) -> Result<(), ()> {
    out.push("`\n- Content signals: ")?;
    if node.content_signals.is_empty() {
        out.push("[none]")?;
    } else {
        for (index, signal) in node.content_signals.iter().enumerate() {
            if index > 0 {
                out.push(", ")?;
            }
            out.push("`")?;
            out.push(signal.wire())?;
            out.push("`")?;
        }
    }
    out.push("\n- States: ")?;
    if node.states.is_empty() {
        return out.push("[none]");
    }
    for (index, state) in node.states.iter().enumerate() {
        if index > 0 {
            out.push(", ")?;
        }
        out.push("`")?;
        out.push(state.wire())?;
        out.push("`")?;
    }
    Ok(())
}

fn text(out: &mut BoundedOutput, node: &ArenaNode) -> Result<(), ()> {
    out.push("\n- Text: ")?;
    if node.text_withheld {
        return out.push("[withheld]");
    }
    if node.text.is_empty() {
        return out.push("[none]");
    }
    for (index, run) in node.text.iter().enumerate() {
        if index > 0 {
            out.push(" ")?;
        }
        if run.content_trust != node.content_trust || run.content_signals != node.content_signals {
            out.push("[content trust `")?;
            out.push(run.content_trust.wire())?;
            out.push("`; signals ")?;
            if run.content_signals.is_empty() {
                out.push("none")?;
            } else {
                for (signal_index, signal) in run.content_signals.iter().enumerate() {
                    if signal_index > 0 {
                        out.push(", ")?;
                    }
                    out.push("`")?;
                    out.push(signal.wire())?;
                    out.push("`")?;
                }
            }
            out.push("] ")?;
        }
        escaped(out, &run.text)?;
    }
    Ok(())
}

fn actions(out: &mut BoundedOutput, node: &ArenaNode) -> Result<(), ()> {
    out.push("\n- Available actions: ")?;
    if node.actions.is_empty() {
        return out.push("[none]");
    }
    for (index, action) in node.actions.iter().enumerate() {
        if index > 0 {
            out.push(", ")?;
        }
        out.push("`")?;
        out.push(action.wire())?;
        out.push("`")?;
    }
    Ok(())
}

fn field(out: &mut BoundedOutput, label: &str, value: &str) -> Result<(), ()> {
    out.push("- ")?;
    out.push(label)?;
    out.push(": ")?;
    escaped(out, value)?;
    out.push("\n")
}

fn escaped(out: &mut BoundedOutput, value: &str) -> Result<(), ()> {
    let mut plain_start = 0;
    for (offset, character) in value.char_indices() {
        if !markdown_requires_escape(character) {
            continue;
        }
        out.push(value.get(plain_start..offset).ok_or(())?)?;
        match character {
            '\\' | '`' | '*' | '_' | '{' | '}' | '[' | ']' | '<' | '>' | '#' | '|' => {
                out.push("\\")?;
                out.push(character.encode_utf8(&mut [0; 4]))?;
            }
            '\n' => out.push("\\n")?,
            '\r' => out.push("\\r")?,
            '\t' => out.push("\\t")?,
            value if value.is_control() => out.push("�")?,
            _ => return Err(()),
        }
        plain_start = offset.checked_add(character.len_utf8()).ok_or(())?;
    }
    out.push(value.get(plain_start..).ok_or(())?)?;
    Ok(())
}

fn markdown_requires_escape(value: char) -> bool {
    matches!(
        value,
        '\\' | '`' | '*' | '_' | '{' | '}' | '[' | ']' | '<' | '>' | '#' | '|'
    ) || value.is_control()
}
