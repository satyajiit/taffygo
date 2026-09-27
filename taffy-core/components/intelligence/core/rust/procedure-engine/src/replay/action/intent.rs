// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed operation projection for one replayed procedure step.
//!
//! A procedure may name more verbs than replay can safely bind. This module
//! constructs only operations whose operands the durable record carries and
//! whose target needs no model-turn handle table. Text that would require an
//! opaque turn residency and node handles that need a live binding both fail
//! closed in their own vocabulary.

use bip_types::identity::TabId;
use task_engine::action::{
    ActionIntent, BrowserIntent, DisclosureState, DomQueryRole, ObservedNodeHandle, ScrollDirection,
};
use task_engine::tool::ArgumentValue;

use super::super::{ReplayPage, ReplayRefusal};
use crate::step::{ProcedureStep, StepValue};

pub(super) fn for_step(
    step: &ProcedureStep,
    tab: &TabId,
    page: Option<ReplayPage<'_>>,
) -> Result<ActionIntent, ReplayRefusal> {
    let tab = tab.clone();
    let intent = match step.verb.as_str() {
        "browser.navigate" => BrowserIntent::Navigate {
            tab,
            address: address(step, "address")?,
            new_tab: flag(step, "new_tab")?.unwrap_or(false),
        },
        "browser.search" => return Err(ReplayRefusal::TransientOperandUnavailable),
        "browser.back" => BrowserIntent::HistoryBack { tab },
        "browser.forward" => BrowserIntent::HistoryForward { tab },
        "browser.reload" => BrowserIntent::Reload { tab },
        "browser.stop_loading" => BrowserIntent::StopLoading { tab },
        "browser.tabs.open" => BrowserIntent::TabsOpen {
            context: tab,
            address: optional_address(step, "address")?,
        },
        "browser.dom.query" => BrowserIntent::DomQuery {
            tab,
            within: no_node_argument(step, "within")?,
            role: optional_choice(step, "role")?
                .map(|role| {
                    DomQueryRole::from_choice(role).ok_or(ReplayRefusal::UnexpectedActionState)
                })
                .transpose()?,
            text: if argument(step, "text").is_some() {
                return Err(ReplayRefusal::TransientOperandUnavailable);
            } else {
                None
            },
            limit: optional_count(step, "limit")?,
        },
        "browser.dom.read" => BrowserIntent::DomRead {
            tab,
            target: no_node_argument(step, "node")?,
        },
        "browser.dom.scroll" => BrowserIntent::DomScroll {
            tab,
            direction: ScrollDirection::from_choice(choice(step, "direction")?)
                .ok_or(ReplayRefusal::UnexpectedActionState)?,
            target: no_node_argument(step, "node")?,
        },
        "browser.selection.read" => BrowserIntent::SelectionRead { tab },
        "page.pdf.inspect" => BrowserIntent::PdfInspect { tab },
        "page.screenshot.inspect" => BrowserIntent::PageScreenshotInspect { tab },
        "browser.dom.click" => BrowserIntent::DomClick {
            target: target(step, "node", page)?,
            expected_state: match optional_choice(step, "expected_state")? {
                None => None,
                Some(value) => Some(
                    DisclosureState::from_choice(value)
                        .ok_or(ReplayRefusal::UnexpectedActionState)?,
                ),
            },
        },
        "browser.dom.focus" => BrowserIntent::DomFocus {
            target: target(step, "node", page)?,
        },
        "browser.link.open" => BrowserIntent::LinkOpen {
            target: target(step, "node", page)?,
        },
        "browser.download.from_link" => BrowserIntent::DownloadFromLink {
            target: target(step, "node", page)?,
            browser_session_id: page
                .and_then(|page| page.browser_session_id)
                .ok_or(ReplayRefusal::HandleBindingUnavailable)?
                .clone(),
        },
        "browser.download.list" => BrowserIntent::DownloadList {
            tab,
            browser_session_id: page
                .and_then(|page| page.browser_session_id)
                .ok_or(ReplayRefusal::HandleBindingUnavailable)?
                .clone(),
        },
        "browser.form.inspect"
        | "browser.form.fill"
        | "browser.form.submit"
        | "page.images.describe"
        | "page.images.read_text"
        | "page.video.inspect" => return Err(ReplayRefusal::HandleBindingUnavailable),
        "browser.tabs.list"
        | "browser.tabs.activate"
        | "browser.tabs.close"
        | "browser.download.start"
        | "browser.download.cancel" => {
            return Err(ReplayRefusal::UnservedVerb);
        }
        _ => return Err(ReplayRefusal::UnservedVerb),
    };
    Ok(ActionIntent::Browser(intent))
}

fn argument<'a>(step: &'a ProcedureStep, name: &str) -> Option<&'a StepValue> {
    step.arguments
        .iter()
        .find(|argument| argument.name == name)
        .map(|argument| &argument.value)
}

fn literal<'a>(step: &'a ProcedureStep, name: &str) -> Result<&'a ArgumentValue, ReplayRefusal> {
    match argument(step, name) {
        Some(StepValue::Literal(value)) => Ok(value),
        Some(StepValue::FromEarlierStep { .. } | StepValue::SemanticTarget { .. }) => {
            Err(ReplayRefusal::HandleBindingUnavailable)
        }
        Some(StepValue::FromPerson { .. }) => Err(ReplayRefusal::PersonValueIsNotAPosition),
        None => Err(ReplayRefusal::UnexpectedActionState),
    }
}

fn target(
    step: &ProcedureStep,
    name: &str,
    page: Option<ReplayPage<'_>>,
) -> Result<ObservedNodeHandle, ReplayRefusal> {
    let Some(StepValue::SemanticTarget { role, phrase }) = argument(step, name) else {
        return Err(ReplayRefusal::HandleBindingUnavailable);
    };
    page.ok_or(ReplayRefusal::HandleBindingUnavailable)?
        .target(*role, *phrase)
}

fn address(step: &ProcedureStep, name: &str) -> Result<String, ReplayRefusal> {
    match literal(step, name)? {
        ArgumentValue::Address(value) => Ok(value.clone()),
        _ => Err(ReplayRefusal::UnexpectedActionState),
    }
}

fn optional_address(step: &ProcedureStep, name: &str) -> Result<Option<String>, ReplayRefusal> {
    argument(step, name).map_or(Ok(None), |_| address(step, name).map(Some))
}

fn flag(step: &ProcedureStep, name: &str) -> Result<Option<bool>, ReplayRefusal> {
    let Some(_) = argument(step, name) else {
        return Ok(None);
    };
    match literal(step, name)? {
        ArgumentValue::Flag(value) => Ok(Some(*value)),
        _ => Err(ReplayRefusal::UnexpectedActionState),
    }
}

fn optional_count(step: &ProcedureStep, name: &str) -> Result<Option<u64>, ReplayRefusal> {
    let Some(_) = argument(step, name) else {
        return Ok(None);
    };
    match literal(step, name)? {
        ArgumentValue::Count(value) => Ok(Some(*value)),
        _ => Err(ReplayRefusal::UnexpectedActionState),
    }
}

fn choice<'a>(step: &'a ProcedureStep, name: &str) -> Result<&'a str, ReplayRefusal> {
    match literal(step, name)? {
        ArgumentValue::Choice(value) => Ok(value),
        _ => Err(ReplayRefusal::UnexpectedActionState),
    }
}

fn optional_choice<'a>(
    step: &'a ProcedureStep,
    name: &str,
) -> Result<Option<&'a str>, ReplayRefusal> {
    argument(step, name).map_or(Ok(None), |_| choice(step, name).map(Some))
}

fn no_node_argument(
    step: &ProcedureStep,
    name: &str,
) -> Result<Option<bip_types::identity::SemanticNodeId>, ReplayRefusal> {
    if argument(step, name).is_some() {
        Err(ReplayRefusal::HandleBindingUnavailable)
    } else {
        Ok(None)
    }
}
