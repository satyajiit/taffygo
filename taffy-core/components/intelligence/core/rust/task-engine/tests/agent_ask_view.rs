// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A challenge is brought into view before the sheet that asks for its
//! answer (decision 0240).
//!
//! On 2026-09-24 the ask naming the identity number on the myAadhaar form
//! carried the CAPTCHA's answer as a companion, and the sheet asked for the
//! identity number alone: the browser copies a challenge's picture from the
//! part of the page in view, and the CAPTCHA was below it. The task knows
//! which line is the challenge's answer, so it scrolls that line into view
//! through policy, once, and only then asks.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{ActionId, DispatchId, MonotonicMillis, SemanticNodeId, TabId};
use bip_types::ActionResultCode;
use task_engine::action::{ActionIntent, ActionOutcome, BrowserIntent, ScrollDirection};
use task_engine::agent::{ModelToolCall, TurnResidency};
use task_engine::authority::{Authorization, CapabilityId, Denial, ProposalDecision};
use task_engine::{ArgumentValue, Command, Effect, ModelHandle, ModelStopReason, SuppliedArgument};

use common::agent::record_turn_in;
use common::errand::{prepared_errand_admitting_each, DISCOVERY_TAB};
use common::value_sheet::{aadhaar_page, companions_of, next, request_of};
use common::Fixture;

const TOOLS: [&str; 3] = [
    "user.request_values",
    "browser.form.fill",
    "browser.dom.scroll",
];

fn errand_that_may_scroll() -> Fixture {
    prepared_errand_admitting_each(4, &TOOLS)
}

/// The model's one `user.request_values` naming `handle`, read and recorded,
/// and the first command the walk makes of it.
fn ask_naming(fixture: &mut Fixture, handle: ModelHandle) -> (Command, TurnResidency) {
    let ask = ModelToolCall::new(
        "user.request_values",
        vec![SuppliedArgument::new(
            "form",
            ArgumentValue::Handle(handle.value()),
        )],
    );
    let residency = record_turn_in(
        fixture,
        aadhaar_page().page,
        ModelStopReason::ToolCall,
        vec![ask],
    );
    let command = next(fixture, &residency).expect("the walk makes a move of the ask");
    (command, residency)
}

/// Whether `command` proposes exactly the scroll a model's
/// `browser.dom.scroll { node, direction: "to_node" }` would have produced,
/// under the task's own key for call 0 and drawing on no budget.
fn is_view_of(command: &Command, node: &str) -> bool {
    let Command::ProposeAction(proposal) = command else {
        return false;
    };
    let key = proposal.idempotency_key.as_str();
    key.starts_with("ask-view-")
        && key.ends_with("-0")
        && proposal.budget_draw.is_none()
        && proposal.intent()
            == &ActionIntent::Browser(BrowserIntent::DomScroll {
                tab: TabId::new(DISCOVERY_TAB),
                direction: ScrollDirection::ToNode,
                target: Some(SemanticNodeId::new(node)),
            })
}

fn apply_view(fixture: &mut Fixture, command: Command) -> ActionId {
    let Command::ProposeAction(proposal) = &command else {
        unreachable!("expected a proposal, got {command:?}");
    };
    let key = proposal.idempotency_key.clone();
    fixture.must_apply(command);
    fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key == key)
        .map_or_else(
            || unreachable!("the proposal minted one action"),
            |action| action.action_id().clone(),
        )
}

fn settle_view(fixture: &mut Fixture, action_id: ActionId, code: ActionResultCode) {
    let _ = settle_view_with_effects(fixture, action_id, code);
}

fn settle_view_with_effects(
    fixture: &mut Fixture,
    action_id: ActionId,
    code: ActionResultCode,
) -> Vec<Effect> {
    let dispatch = DispatchId::new("dispatch-view");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new("capability-view"),
        })),
        dispatch_id: Some(dispatch.clone()),
    });
    fixture
        .apply(Command::RecordActionOutcome {
            action_id,
            outcome: Box::new(ActionOutcome {
                code,
                dispatch_id: Some(dispatch),
                observed_at: MonotonicMillis(5_000),
                observation: None,
                discovered_source: None,
            }),
        })
        .expect("the outcome is the scroll's own")
        .effects
}

/// The whole of the change, in order: the scroll to the CAPTCHA's answer,
/// nothing while it is in flight, and then the ask with the CAPTCHA on it.
#[test]
fn a_challenge_on_the_sheet_is_scrolled_into_view_before_the_ask() {
    let mut fixture = errand_that_may_scroll();
    let (first, residency) = ask_naming(&mut fixture, aadhaar_page().id);
    let turns_before = fixture.reducer.next_model_call_id();
    assert!(
        is_view_of(&first, "captcha-answer"),
        "expected the scroll to the CAPTCHA, got {first:?}"
    );
    let action_id = apply_view(&mut fixture, first);
    assert_eq!(next(&fixture, &residency), None, "nothing while it scrolls");
    settle_view(&mut fixture, action_id, ActionResultCode::Verified);

    let ask = next(&fixture, &residency).expect("the ask goes out");
    assert_eq!(companions_of(&ask), ["captcha-answer"]);
    let _ = request_of(&ask);
    assert_eq!(
        fixture.reducer.next_model_call_id(),
        turns_before,
        "the scroll asked the model for nothing"
    );
}

#[test]
fn naming_the_challenge_itself_scrolls_to_it() {
    let mut fixture = errand_that_may_scroll();
    let (first, _) = ask_naming(&mut fixture, aadhaar_page().captcha);
    assert!(is_view_of(&first, "captcha-answer"), "got {first:?}");
}

/// A scroll that did not happen does not hold the ask back. The sheet leaves
/// off what it cannot show, as it did before this record, and the task never
/// scrolls for that ask again.
#[test]
fn a_scroll_that_does_not_happen_still_lets_the_ask_go_out() {
    for refused in [true, false] {
        let mut fixture = errand_that_may_scroll();
        let (first, residency) = ask_naming(&mut fixture, aadhaar_page().id);
        let action_id = apply_view(&mut fixture, first);
        if refused {
            fixture.must_apply(Command::RecordPolicyDecision {
                action_id,
                decision: Box::new(ProposalDecision::Deny(Denial::new(
                    ActionResultCode::DeniedByPolicy,
                ))),
                dispatch_id: None,
            });
        } else {
            settle_view(&mut fixture, action_id, ActionResultCode::NodeGone);
        }
        let ask = next(&fixture, &residency).expect("the ask goes out");
        assert!(
            matches!(ask, Command::RequestFieldValues { .. }),
            "refused={refused}: expected the ask, got {ask:?}"
        );
        let scrolls = fixture
            .reducer
            .actions()
            .filter(|action| {
                action
                    .proposal()
                    .idempotency_key
                    .as_str()
                    .starts_with("ask-view-")
            })
            .count();
        assert_eq!(scrolls, 1, "refused={refused}");
    }
}

/// A scroll whose outcome the browser could not confirm is not reconciled
/// with the person: that would hand the errand over before the ask it was
/// made for. On a phone the CAPTCHA was already in view, the scroll moved
/// nothing, its check timed out, and the errand waited on the person with no
/// sheet in front of them (decision 0245).
#[test]
fn a_scroll_of_unknown_outcome_is_not_reconciled_and_the_ask_goes_out() {
    let mut fixture = errand_that_may_scroll();
    let (first, residency) = ask_naming(&mut fixture, aadhaar_page().id);
    let action_id = apply_view(&mut fixture, first);
    let effects =
        settle_view_with_effects(&mut fixture, action_id, ActionResultCode::OutcomeUnknown);
    assert!(
        !effects
            .iter()
            .any(|effect| matches!(effect, Effect::ReconcileAction { .. })),
        "got {effects:?}"
    );
    let ask = next(&fixture, &residency).expect("the ask goes out");
    assert_eq!(companions_of(&ask), ["captcha-answer"]);
}

/// Nothing settles that scroll's unknown outcome, so it cannot hold the
/// task's ending back either (decision 0248). On a phone the errand it
/// belonged to was restored on the next start, gave up, and its ending was
/// refused, which took the core down with every task in it.
#[test]
fn a_scroll_of_unknown_outcome_does_not_hold_the_ending_back() {
    let mut fixture = errand_that_may_scroll();
    let (first, _) = ask_naming(&mut fixture, aadhaar_page().id);
    let action_id = apply_view(&mut fixture, first);
    settle_view(&mut fixture, action_id, ActionResultCode::OutcomeUnknown);
    fixture.must_apply(Command::ResultCandidateReady);
    fixture.must_apply(Command::PartialResultValidated(common::partial_result()));
    assert_eq!(fixture.state(), task_engine::task::TaskState::Partial);
}

#[test]
fn an_ask_with_no_challenge_on_it_goes_out_at_once() {
    // A field anybody could fill brings no companions, and is not a
    // challenge's answer itself.
    let mut fixture = errand_that_may_scroll();
    let (first, _) = ask_naming(&mut fixture, aadhaar_page().name);
    assert!(
        matches!(first, Command::RequestFieldValues { .. }),
        "expected the ask, got {first:?}"
    );
}

/// A block brings the page's fields only the person can supply (decision
/// 0243), so an ask naming the myAadhaar region scrolls to the CAPTCHA first
/// and then carries both fields for the browser to ask about if the block
/// turns out to have none of its own.
#[test]
fn an_ask_naming_a_block_brings_the_fields_only_the_person_can_supply() {
    let mut fixture = errand_that_may_scroll();
    let (first, residency) = ask_naming(&mut fixture, aadhaar_page().region);
    assert!(is_view_of(&first, "captcha-answer"), "got {first:?}");
    let action_id = apply_view(&mut fixture, first);
    settle_view(&mut fixture, action_id, ActionResultCode::Verified);

    let ask = next(&fixture, &residency).expect("the ask goes out");
    assert_eq!(companions_of(&ask), ["aadhaar-number", "captcha-answer"]);
}

#[test]
fn a_task_that_may_not_scroll_asks_at_once() {
    let mut fixture = prepared_errand_admitting_each(4, &TOOLS[..2]);
    let (first, _) = ask_naming(&mut fixture, aadhaar_page().id);
    assert_eq!(companions_of(&first), ["captcha-answer"]);
}
