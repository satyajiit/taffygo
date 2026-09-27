// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Composition of the only task-owned Python job body.

use core_runtime::ports::ActionEffectFacts;
use core_runtime::{ActionIntent, TaskId};

use crate::service_bridge_runtime::ServiceBridge;

pub(super) struct PythonJobBody {
    pub(super) entrypoint: String,
    pub(super) input: Vec<u8>,
}

pub(super) fn compose(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    facts: &ActionEffectFacts,
) -> Result<PythonJobBody, ()> {
    let ActionIntent::ToolJob(intent) = facts.proposal.intent() else {
        return Err(());
    };
    if intent.tool_name != "python.execute" || intent.runtime != core_runtime::ToolRuntime::Python {
        return Err(());
    }
    let (title, content) = bridge
        .runtime
        .as_ref()
        .and_then(|runtime| {
            runtime
                .core()
                .transient_python_arguments(task_id.as_str(), &facts.proposal)
        })
        .ok_or(())?;
    let mut input = String::new();
    match intent.entrypoint.label() {
        "document.build" => {
            input.push_str("{\"document\":{\"title\":");
            json_string(&mut input, title);
            input.push_str(",\"sections\":[{\"heading\":");
            json_string(&mut input, title);
            input.push_str(",\"paragraphs\":[");
            json_string(&mut input, content);
            input.push_str("]}]}}");
        }
        "spreadsheet.build" => {
            input.push_str("{\"sheet_name\":");
            json_string(&mut input, title);
            input.push_str(",\"csv\":");
            json_string(&mut input, content);
            input.push('}');
        }
        _ => return Err(()),
    }
    Ok(PythonJobBody {
        entrypoint: intent.entrypoint.label().to_owned(),
        input: input.into_bytes(),
    })
}

fn json_string(out: &mut String, value: &str) {
    out.push('"');
    for character in value.chars() {
        match character {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\u{08}' => out.push_str("\\b"),
            '\u{0c}' => out.push_str("\\f"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            value if value.is_control() => {
                use std::fmt::Write as _;
                let _ = write!(out, "\\u{:04x}", u32::from(value));
            }
            value => out.push(value),
        }
    }
    out.push('"');
}

#[cfg(test)]
mod tests {
    use super::json_string;

    #[test]
    fn json_text_cannot_change_the_composed_shape() {
        let mut encoded = String::new();
        json_string(&mut encoded, "x\"},\"source\":\"print(1)\n");
        assert_eq!(encoded, "\"x\\\"},\\\"source\\\":\\\"print(1)\\n\"");
    }
}
