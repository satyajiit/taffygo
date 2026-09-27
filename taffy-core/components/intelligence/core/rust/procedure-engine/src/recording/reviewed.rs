// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The bounded automatic recording subset that a person can review in full.

use policy_engine::origin::NormalizedOrigin;
use task_engine::tool::ArgumentValue;

use crate::{Procedure, ProcedureStep, StepValue};

/// A public same-origin HTTPS address, without credentials or transient suffixes.
pub fn public_address_is_valid(address: &str, origin: &NormalizedOrigin) -> bool {
    if address.len() > task_engine::MAX_ARGUMENT_VALUE_BYTES
        || !address.starts_with("https://")
        || address
            .chars()
            .any(|ch| ch.is_whitespace() || ch.is_control())
        || address.contains(['?', '#', '\\'])
    {
        return false;
    }
    let canonical = origin.display();
    address
        .strip_prefix(&canonical)
        .is_some_and(|path| path.is_empty() || path.starts_with('/'))
}

pub(crate) fn replayable_recording(procedure: &Procedure) -> bool {
    procedure.steps.first().is_some_and(|step| {
        step.verb == "browser.navigate"
            && step.arguments.iter().any(|argument| {
                argument.name == "address"
                    && matches!(&argument.value, StepValue::Literal(ArgumentValue::Address(address))
                        if public_address_is_valid(address, procedure.scope.origin()))
            })
    }) && procedure.steps.iter().enumerate().all(|(index, step)| {
        supported_step(step)
            && step.arguments.iter().all(|argument| match &argument.value {
                StepValue::SemanticTarget { .. } => matches!(
                    step.verb.as_str(),
                    "browser.dom.click"
                        | "browser.dom.focus"
                        | "browser.link.open"
                        | "browser.download.from_link"
                ),
                StepValue::Literal(ArgumentValue::Address(address)) => {
                    index == 0
                        && step.verb == "browser.navigate"
                        && argument.name == "address"
                        && public_address_is_valid(address, procedure.scope.origin())
                }
                StepValue::Literal(
                    ArgumentValue::Choice(_) | ArgumentValue::Count(_) | ArgumentValue::Flag(_),
                ) => true,
                StepValue::Literal(
                    ArgumentValue::Handle(_)
                    | ArgumentValue::SuppliedValue(_)
                    | ArgumentValue::Text(_),
                )
                | StepValue::FromEarlierStep { .. }
                | StepValue::FromPerson { .. } => false,
            })
    })
}

fn supported_step(step: &ProcedureStep) -> bool {
    matches!(
        step.verb.as_str(),
        "browser.navigate"
            | "browser.dom.read"
            | "browser.dom.query"
            | "browser.dom.click"
            | "browser.dom.focus"
            | "browser.link.open"
            | "user.handover"
            | "browser.download.list"
            | "browser.download.from_link"
    )
}
