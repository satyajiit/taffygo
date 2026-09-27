// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which numbers `user.request_values` may name, as the snapshot issues them.
//!
//! A heading or a link can hold no value, and the browser gives up on one
//! without drawing a sheet. A region may be a form the arena cannot recognise:
//! the myAadhaar download form has no address to go to, so no field names it
//! as its form, and naming that region was what drew the sheet on a phone
//! (decision 0192). A fill may name only the field itself (decision 0195).

use bip_types::action::ActionType;
use bip_types::snapshot::{SemanticRole, Sensitivity};
use task_engine::handle::HandleTable;

use super::{node, page, rendered};
use crate::context::arena::{ArenaNode, DestinationClass, PageArena};
use crate::context::render::{render, PageProjection, RenderBudget};

/// Each line's number, in order: whether `user.request_values` may name it,
/// and whether `browser.form.fill` may.
fn marked(nodes: Vec<ArenaNode>) -> Vec<(bool, bool)> {
    let mut arena = PageArena::new();
    for pushed in nodes {
        assert!(arena.push(pushed));
    }
    let mut handles = HandleTable::new();
    let offered = match render(&arena, &page(), RenderBudget::new(8192), &mut handles) {
        PageProjection::Rendered(page) => page.offered,
        other => panic!("expected a rendered page, got {other:?}"),
    };
    (0..u32::try_from(offered).unwrap_or(u32::MAX))
        .map(|value| (handles.takes_values(value), handles.sets_text(value)))
        .collect()
}

fn with_actions(mut node: ArenaNode, actions: &[ActionType]) -> ArenaNode {
    node.actions = actions.to_vec();
    node
}

#[test]
fn a_region_with_no_address_may_be_named_as_the_form() {
    let form = node("form", SemanticRole::Region, Some("private"), &[]);
    let field = with_actions(
        node(
            "aadhaar",
            SemanticRole::TextField,
            Some("Enter Aadhaar Number"),
            &[],
        ),
        &[ActionType::SetText],
    );
    // The form may be asked about and never filled; the field may be both.
    assert_eq!(marked(vec![form, field]), vec![(true, false), (true, true)]);
}

#[test]
fn a_line_no_value_can_go_into_may_not_be_named() {
    let heading = node(
        "heading",
        SemanticRole::Heading,
        Some("Download Aadhaar"),
        &[],
    );
    let button = with_actions(
        node("send", SemanticRole::Button, Some("Send OTP"), &[]),
        &[ActionType::Activate],
    );
    let link = with_actions(
        node("help", SemanticRole::Link, Some("Help"), &[]),
        &[ActionType::Activate],
    );
    // A field the page will not let anyone type into is no place for a value.
    let read_only = node(
        "shown",
        SemanticRole::TextField,
        Some("Masked Aadhaar"),
        &[],
    );
    assert_eq!(
        marked(vec![heading, button, link, read_only]),
        vec![(false, false); 4]
    );
}

fn field(node_id: &str, sensitivity: Sensitivity) -> ArenaNode {
    let mut field = node(node_id, SemanticRole::TextField, Some("Field"), &[]);
    field.actions = vec![ActionType::SetText];
    field.sensitivity = sensitivity;
    field
}

/// The myAadhaar form's three fields are the three classes only the person
/// can supply, and the snapshot says so beside the numbers and names the ask
/// (decision 0234's runs, where the model reached the form twice and never
/// asked).
#[test]
fn an_id_a_captcha_and_a_code_are_named_as_the_persons_to_supply() {
    let mut arena = PageArena::new();
    assert!(arena.push(field("id", Sensitivity::Identity)));
    assert!(arena.push(field("captcha", Sensitivity::ChallengeResponse)));
    assert!(arena.push(field("otp", Sensitivity::OneTimeCode)));
    let (text, _, _) = rendered(&arena, 4096);
    assert!(
        text.ends_with(
            "values can go into: 0 1 2\n\
             only the person can supply: 0 1 2 — ask for them with user.request_values, naming \
             their form, or one of them when they are in no form: the sheet asks for them all at \
             once\n"
        ),
        "{text}"
    );
}

/// The footer and the request read one definition: the numbers the footer
/// calls the person's are the ones a request naming one of them asks about
/// beside it, in page order, and nothing else on the page (decision 0238).
#[test]
fn the_lines_the_footer_names_are_the_ones_one_ask_carries() {
    let mut arena = PageArena::new();
    assert!(arena.push(field("id", Sensitivity::Identity)));
    assert!(arena.push(field("name", Sensitivity::NotSensitive)));
    assert!(arena.push(field("captcha", Sensitivity::ChallengeResponse)));
    assert!(arena.push(field("password", Sensitivity::Credential)));
    assert!(arena.push(field("otp", Sensitivity::OneTimeCode)));
    let mut handles = HandleTable::new();
    let text = match render(&arena, &page(), RenderBudget::new(8192), &mut handles) {
        PageProjection::Rendered(page) => page.text,
        other => panic!("expected a rendered page, got {other:?}"),
    };
    assert!(
        text.contains("only the person can supply: 0 2 4 "),
        "{text}"
    );
    let ids = |value| {
        handles
            .person_only_companions(value, 7)
            .into_iter()
            .map(|id| id.0)
            .collect::<Vec<_>>()
    };
    assert_eq!(ids(0), ["captcha", "otp"]);
    assert_eq!(ids(4), ["id", "captcha"]);
    // A line the footer does not name brings nobody with it.
    assert!(ids(1).is_empty());
    assert!(ids(3).is_empty());
}

/// A search box takes a value and has no class, so a results page is not told
/// to ask the person for anything; a password is handed over, not asked for;
/// and a card number is never Taffy's to supply.
#[test]
fn a_search_box_a_password_and_a_card_number_are_not() {
    for sensitivity in [
        Sensitivity::NotSensitive,
        Sensitivity::Credential,
        Sensitivity::Payment,
    ] {
        let mut arena = PageArena::new();
        assert!(arena.push(field("box", sensitivity)));
        let (text, _, _) = rendered(&arena, 4096);
        assert!(
            text.contains("values can go into: 0\n"),
            "{sensitivity:?}: {text}"
        );
        assert!(
            !text.contains("only the person can supply"),
            "{sensitivity:?}: {text}"
        );
    }
}

/// Whether each line's number may be opened with `browser.link.open`: only a
/// link the reading printed as leading somewhere (decision 0241).
#[test]
fn only_a_link_that_leads_somewhere_may_be_opened() {
    let mut leads = with_actions(
        node(
            "download",
            SemanticRole::Link,
            Some("Download Aadhaar"),
            &[],
        ),
        &[ActionType::Activate],
    );
    leads.destination = DestinationClass::from_bits(1);
    // A link with no address, and a button with one: neither is a link the
    // browser read an address for.
    let nowhere = with_actions(
        node("script", SemanticRole::Link, Some("My Aadhaar"), &[]),
        &[ActionType::Activate],
    );
    let mut button = with_actions(
        node("go", SemanticRole::Button, Some("Go"), &[]),
        &[ActionType::Activate],
    );
    button.destination = DestinationClass::from_bits(1);

    let mut arena = PageArena::new();
    for pushed in [leads, nowhere, button] {
        assert!(arena.push(pushed));
    }
    let mut handles = HandleTable::new();
    let offered = match render(&arena, &page(), RenderBudget::new(8192), &mut handles) {
        PageProjection::Rendered(page) => page.offered,
        other => panic!("expected a rendered page, got {other:?}"),
    };
    let opens: Vec<bool> = (0..u32::try_from(offered).unwrap_or(u32::MAX))
        .map(|value| handles.opens_as_link(value))
        .collect();
    assert_eq!(opens, vec![true, false, false]);
}
