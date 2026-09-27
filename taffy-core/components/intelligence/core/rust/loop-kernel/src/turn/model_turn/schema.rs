// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The compiled-in tool table, as the argument schema a model is shown.
//!
//! A projection of [`task_engine::ToolDefinition`] and never a second
//! vocabulary: every name, sentence and parameter below is read off the
//! registry row, so a tool this build does not have cannot be described to a
//! model, and a parameter the validator does not know cannot be offered.
//! Decision 0054 keeps the schema a Rust structure rather than a document for
//! exactly this reason — a document is something that could be handed to the
//! product, and a tool name joins at dispatch to the `ActionClass` that
//! `policy-engine` reads.

use std::collections::BTreeMap;
use std::sync::OnceLock;

use model_router::json::JsonValue;
use model_router::wire::ToolDeclaration;
use task_engine::{EffectiveToolSet, Parameter, ParameterType};

/// Every tool `tools` admits, in the registry's own order.
///
/// Return the router's borrowed wire view directly. Building an intermediate
/// declaration vector and immediately copying it into this identical shape
/// cost a second allocation on every model turn.
pub(super) fn declarations(tools: &EffectiveToolSet) -> Option<Vec<ToolDeclaration<'static>>> {
    let schemas = schemas();
    tools
        .immediate()
        .flat_map(task_engine::ToolEntry::definitions)
        .map(|definition| {
            Some(ToolDeclaration {
                name: definition.name,
                description: definition.description,
                parameters: schemas.get(definition.name)?,
            })
        })
        .collect()
}

/// Build each immutable registry schema once per sandbox generation.
///
/// A task changes which names are offered, never what a registered name's
/// parameters mean. Rebuilding the same nested maps on every model turn was
/// therefore pure allocation on the hottest assistant path.
fn schemas() -> &'static BTreeMap<&'static str, JsonValue> {
    static SCHEMAS: OnceLock<BTreeMap<&'static str, JsonValue>> = OnceLock::new();
    SCHEMAS.get_or_init(|| {
        let mut schemas = BTreeMap::new();
        for entry in task_engine::tool::REGISTRY {
            for definition in entry.definitions() {
                schemas.insert(definition.name, object_schema(definition.parameters));
            }
        }
        schemas
    })
}

fn object_schema(parameters: &'static [Parameter]) -> JsonValue {
    let mut properties = BTreeMap::new();
    let mut required = Vec::new();
    for parameter in parameters {
        properties.insert(parameter.name.to_owned(), parameter_schema(parameter));
        if parameter.required {
            required.push(JsonValue::Text(parameter.name.to_owned()));
        }
    }
    let mut schema = BTreeMap::new();
    schema.insert("type".to_owned(), JsonValue::Text("object".to_owned()));
    schema.insert("properties".to_owned(), JsonValue::Object(properties));
    schema.insert("required".to_owned(), JsonValue::Array(required));
    // Declared rather than left open. A model that invents an argument gets a
    // refusal from the provider's own validator instead of a call that reaches
    // `tool::validate` carrying a name the row does not declare.
    schema.insert("additionalProperties".to_owned(), JsonValue::Bool(false));
    JsonValue::Object(schema)
}

fn parameter_schema(parameter: &Parameter) -> JsonValue {
    let mut field = BTreeMap::new();
    field.insert(
        "description".to_owned(),
        JsonValue::Text(parameter.description.to_owned()),
    );
    match parameter.value_type {
        // A handle is a number this task issued, so it is described as an
        // integer with a floor and never as a free-form string. The model is
        // told nothing about what it names; resolving it is the handle table's
        // answer and a number that names nothing is a refusal, not a near miss.
        // A supplied value is described the same way and for the same reason:
        // it is a position in what the person answered, so the model is told
        // it is a non-negative integer and nothing else. It learns no length,
        // no shape and no hint about the value behind it — describing one
        // would be describing a value the model is not allowed to hold.
        ParameterType::Handle | ParameterType::Count | ParameterType::SuppliedValue => {
            field.insert("type".to_owned(), JsonValue::Text("integer".to_owned()));
            field.insert("minimum".to_owned(), JsonValue::Integer(0));
        }
        ParameterType::Text | ParameterType::Address => {
            field.insert("type".to_owned(), JsonValue::Text("string".to_owned()));
        }
        ParameterType::Flag => {
            field.insert("type".to_owned(), JsonValue::Text("boolean".to_owned()));
        }
        ParameterType::Choice(names) => {
            field.insert("type".to_owned(), JsonValue::Text("string".to_owned()));
            field.insert(
                "enum".to_owned(),
                JsonValue::Array(
                    names
                        .iter()
                        .map(|name| JsonValue::Text((*name).to_owned()))
                        .collect(),
                ),
            );
        }
    }
    JsonValue::Object(field)
}

#[cfg(test)]
mod tests {
    use super::{declarations, JsonValue};
    use task_engine::{EffectiveToolSet, Milestone};

    #[test]
    fn every_declared_tool_carries_an_object_schema() {
        let tools = EffectiveToolSet::for_task(Milestone::M8, &[]);
        let declared = declarations(&tools).unwrap_or_default();
        assert!(!declared.is_empty());
        for tool in declared {
            let Some(object) = tool.parameters.as_object() else {
                panic!("{} declared a non-object schema", tool.name);
            };
            assert_eq!(
                object.get("type"),
                Some(&JsonValue::Text("object".to_owned())),
                "{}",
                tool.name
            );
            assert!(object.contains_key("properties"), "{}", tool.name);
            assert!(object.contains_key("required"), "{}", tool.name);
        }
    }

    #[test]
    fn a_narrowed_allowlist_declares_nothing_it_excludes() {
        let tools = EffectiveToolSet::for_task(Milestone::M8, &["user.ask".to_owned()]);
        let names = declarations(&tools)
            .unwrap_or_default()
            .into_iter()
            .map(|tool| tool.name)
            .collect::<Vec<_>>();
        assert!(!names.contains(&"browser.dom.read"));
        // Naming what survived, not only what did not. An absence alone passes
        // just as well on an empty projection, and empty is the one outcome
        // that would be worst here: a model shown no tools cannot act, and a
        // model shown no `user.handover` cannot give the page back -- the
        // escape a narrowing is never allowed to take away.
        assert!(names.contains(&"user.ask"));
        assert!(names.contains(&task_engine::tool::HANDOVER_TOOL));
    }

    #[test]
    fn an_errand_declares_its_download_and_form_rows_on_the_first_turn() {
        let errand = EffectiveToolSet::for_template(
            task_engine::TaskTemplateId::WebErrand,
            Milestone::M8,
            &[],
        );
        let names = declarations(&errand)
            .unwrap_or_default()
            .into_iter()
            .map(|tool| tool.name)
            .collect::<Vec<_>>();
        assert!(names.contains(&"browser.download.start"));
        assert!(names.contains(&"browser.form.fill"));
        assert!(names.contains(&"browser.tabs.list"));
        let research = EffectiveToolSet::for_task(Milestone::M8, &[]);
        let names = declarations(&research)
            .unwrap_or_default()
            .into_iter()
            .map(|tool| tool.name)
            .collect::<Vec<_>>();
        assert!(!names.contains(&"browser.download.start"));
    }

    #[test]
    fn immutable_parameter_schemas_are_reused_across_turns() {
        let tools = EffectiveToolSet::for_task(Milestone::M8, &[]);
        let first = declarations(&tools).unwrap_or_default();
        let second = declarations(&tools).unwrap_or_default();

        assert!(!first.is_empty());
        assert_eq!(first.len(), second.len());
        for (before, after) in first.iter().zip(&second) {
            assert_eq!(before.name, after.name);
            assert!(std::ptr::eq(before.parameters, after.parameters));
        }
    }
}
