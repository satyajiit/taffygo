// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reference-only descriptors derived from the exact admitted intent.

use bip_types::action::PostconditionKind;
use bip_types::snapshot::Sensitivity;
use procedure_engine::{ArgumentDescriptor, MatchClause, RecordedValue, StepDescriptor};
use task_engine::action::{ActionIntent, BrowserIntent, DisclosureState, ObservedNodeHandle};
use task_engine::{ActionProposal, Milestone};

use crate::context::LivePage;

pub(super) fn describe(
    proposal: &ActionProposal,
    page: &LivePage,
) -> Option<(StepDescriptor, Option<MatchClause>)> {
    let ActionIntent::Browser(intent) = proposal.intent() else {
        return None;
    };
    let mut arguments = Vec::new();
    let mut clause = None;
    let postcondition = match intent {
        BrowserIntent::Navigate {
            address,
            new_tab: false,
            ..
        } => {
            arguments.push(argument(
                proposal,
                "address",
                RecordedValue::PublicAddress(address.clone()),
            )?);
            PostconditionKind::CommittedNavigation
        }
        BrowserIntent::DomRead { target: None, .. } | BrowserIntent::DownloadList { .. } => {
            PostconditionKind::NoMutation
        }
        BrowserIntent::DownloadFromLink { target, .. } => {
            let (value, matched) = semantic_target(page, target)?;
            arguments.push(argument(proposal, "node", value)?);
            clause = Some(matched);
            PostconditionKind::BrowserFlowStarted
        }
        BrowserIntent::LinkOpen { target } | BrowserIntent::DomFocus { target } => {
            let (value, matched) = semantic_target(page, target)?;
            arguments.push(argument(proposal, "node", value)?);
            clause = Some(matched);
            if matches!(intent, BrowserIntent::LinkOpen { .. }) {
                PostconditionKind::CommittedNavigation
            } else {
                PostconditionKind::NodeStateChanged
            }
        }
        BrowserIntent::DomClick {
            target,
            expected_state,
        } => {
            let (value, matched) = semantic_target(page, target)?;
            arguments.push(argument(proposal, "node", value)?);
            // A press with no disclosure claim records no `expected_state`
            // argument at all, so replaying it proposes the same ordinary
            // press rather than one that demands a state the recorded page
            // happened to be in.
            if let Some(state) = expected_state {
                arguments.push(argument(
                    proposal,
                    "expected_state",
                    RecordedValue::Choice {
                        index: match state {
                            DisclosureState::Expanded => 0,
                            DisclosureState::Collapsed => 1,
                        },
                    },
                )?);
            }
            clause = Some(matched);
            if expected_state.is_some() {
                PostconditionKind::NodeStateChanged
            } else {
                PostconditionKind::DocumentAdvanced
            }
        }
        _ => return None,
    };
    Some((
        StepDescriptor::new(proposal.tool_name(), postcondition).taking(arguments),
        clause,
    ))
}

fn semantic_target(
    page: &LivePage,
    target: &ObservedNodeHandle,
) -> Option<(RecordedValue, MatchClause)> {
    let node = page.observed_node(target)?;
    if node.sensitivity != Sensitivity::NotSensitive || node.name_withheld {
        return None;
    }
    let phrase = procedure_engine::classify_phrase(node.name.as_deref()?)?;
    Some((
        RecordedValue::SemanticTarget {
            role: node.role,
            phrase,
        },
        MatchClause::PhraseAt {
            role: node.role,
            phrase,
        },
    ))
}

fn argument(
    proposal: &ActionProposal,
    name: &str,
    value: RecordedValue,
) -> Option<ArgumentDescriptor> {
    let entry = task_engine::tool::resolve(proposal.tool_name(), Milestone::M7).entry()?;
    let parameter = entry
        .parameters
        .iter()
        .position(|parameter| parameter.name == name)?;
    Some(ArgumentDescriptor::new(parameter, value))
}
