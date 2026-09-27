// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The rendered preview: handles, staged queries, and the research projection.

use super::{evidence, node, source_id};
use crate::context::arena::{ArenaNode, PageArena};
use crate::context::page::{DomQueryFilter, LivePage, MAX_RESEARCH_PROJECTION_BYTES};
use crate::context::vocabulary::EMPTY_PAGE_LINE;
use bip_types::action::ActionType;
use bip_types::identity::{PageEpoch, TabId};
use bip_types::snapshot::SemanticRole;
use task_engine::action::DomQueryRole;
use task_engine::{ObservationCompleteness, PageReadability, SourceId};

/// The one line of a projection that says which numbers take a person's
/// values (decision 0211), whichever of the two forms it took.
fn value_footer(text: &str) -> &str {
    text.lines()
        .find(|line| line.starts_with("values"))
        .unwrap_or_else(|| unreachable!("every rendered projection ends with this line: {text}"))
}

fn roled(node_id: &str, name: &str, role: SemanticRole, actions: &[ActionType]) -> ArenaNode {
    let mut built = node(node_id, name);
    built.role = role;
    built.actions = actions.to_vec();
    built
}

#[test]
fn a_refused_preview_does_not_spend_handles() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-1", "Download")));
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let preview = page.preview();
    assert!(preview.carries_page_content);
    assert_eq!(preview.handles.issued(), 1);
    assert_eq!(page.handles.issued(), 0);
    page.commit_handles(preview.handles.clone());
    assert_eq!(page.handles.issued(), 1);
}

#[test]
fn a_dom_query_is_one_accepted_projection_over_the_retained_full_arena() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-download", "Download report")));
    assert!(arena.push(node("n-cancel", "Cancel")));
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let filter = DomQueryFilter::new(
        None,
        Some(DomQueryRole::Button),
        Some("DOWNLOAD".to_owned()),
        8,
    )
    .unwrap_or_else(|| unreachable!("bounded query"));
    assert!(page.stage_dom_query(source_id(), &filter));

    let first = page.preview();
    let first_text = first.text.as_deref().unwrap_or("");
    assert!(first_text.contains("Query result: 1 matching nodes"));
    assert!(first_text.contains("Download report"));
    assert!(!first_text.contains("Cancel"));
    assert!(
        page.preview()
            .text
            .as_deref()
            .is_some_and(|text| text.contains("Query result: 1 matching nodes")),
        "a refused body does not consume the resident filter"
    );

    page.commit_handles(first.handles);
    let later = page.preview().text.unwrap_or_default();
    assert!(!later.contains("Query result:"));
    assert!(later.contains("Download report"));
    assert!(later.contains("Cancel"));
}

#[test]
fn a_zero_match_query_never_claims_the_document_is_blank() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-cancel", "Cancel")));
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let filter = DomQueryFilter::new(
        None,
        Some(DomQueryRole::Link),
        Some("download".to_owned()),
        8,
    )
    .unwrap_or_else(|| unreachable!("bounded query"));
    assert!(page.stage_dom_query(source_id(), &filter));

    let narrowed = page.preview();
    let text = narrowed.text.clone().unwrap_or_default();
    assert!(text.contains("Query result: 0 matching nodes"));
    assert!(text.contains("nothing on this page matched that query"));
    // The blank-page sentence itself, not a paraphrase of it. This assertion
    // read `"page is empty"` for as long as the test existed — a string no
    // projection has ever produced, because the constant is "the page is
    // blank" — so the half of the rule the test is named for was never
    // actually under test (decision 0210).
    assert!(!text.contains(EMPTY_PAGE_LINE), "{text}");
    assert!(!text.contains("Cancel"));
    // The sentence was only ever half of it. The verdict beneath the sentence
    // said `Empty` too, and that is what the model acts on.
    assert_eq!(narrowed.readability, PageReadability::Readable);
}

#[test]
fn a_query_footer_names_only_numbers_the_narrowed_view_issued() {
    // The path run 10's model was actually on. It had queried the page down to
    // its fields and then asked for the person's values, so the footer of
    // decision 0211 has to be right *here* and not only on a whole-page
    // render — which is the only path the six golden tests cover.
    let mut arena = PageArena::new();
    assert!(arena.push(roled(
        "form-1",
        "Download Aadhaar",
        SemanticRole::Region,
        &[]
    )));
    let mut aadhaar = roled(
        "n-aadhaar",
        "Enter Aadhaar Number",
        SemanticRole::TextField,
        &[ActionType::SetText],
    );
    aadhaar.container = Some("form-1".to_owned());
    assert!(arena.push(aadhaar));
    let mut otp = roled(
        "n-otp",
        "Send OTP",
        SemanticRole::Button,
        &[ActionType::Activate],
    );
    otp.container = Some("form-1".to_owned());
    assert!(arena.push(otp));

    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let filter = DomQueryFilter::new(None, Some(DomQueryRole::Field), None, 8)
        .unwrap_or_else(|| unreachable!("bounded query"));
    assert!(page.stage_dom_query(source_id(), &filter));

    let narrowed = page.preview();
    let text = narrowed.text.clone().unwrap_or_default();
    assert!(text.contains("Query result: 1 matching nodes"), "{text}");
    // One line in the view, so one number, and the footer names it. The
    // region qualifies on the whole page and holds no number here; naming it
    // would be naming a number this view never issued, which is decision
    // 0189's defect with the footer as its new mouth.
    assert_eq!(value_footer(&text), "values can go into: 0");
    assert!(!text.contains("Download Aadhaar"), "{text}");

    // The same three nodes rendered whole: fresh numbers, and now the region
    // is named too, because a field in view names it as its form.
    page.commit_handles(narrowed.handles);
    let whole = page.preview().text.unwrap_or_default();
    assert_eq!(value_footer(&whole), "values can go into: 1 2");
}

#[test]
fn narrowing_a_view_can_only_take_a_number_off_the_value_footer() {
    // `ValueTarget::Container` is the one target that depends on nodes *other
    // than* the one being marked — some shown node has to name it as its form
    // — so it is the one a narrowed view can answer differently. The
    // direction is what matters: a view that hides the field must lose the
    // container, never gain one, because `issue_marked` fixes a number's mark
    // for that number's life and `user.request_values` spends it later.
    let mut arena = PageArena::new();
    // Not a Region, so it qualifies only by being named as a form.
    assert!(arena.push(roled(
        "wrapper",
        "Aadhaar services",
        SemanticRole::Document,
        &[]
    )));
    let mut field = roled(
        "n-otp-code",
        "One time password",
        SemanticRole::TextField,
        &[ActionType::SetText],
    );
    field.container = Some("wrapper".to_owned());
    assert!(arena.push(field));

    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));

    // Whole page: the wrapper is a container because the field names it.
    let whole = page.preview();
    assert_eq!(
        value_footer(&whole.text.clone().unwrap_or_default()),
        "values can go into: 0 1"
    );
    page.commit_handles(whole.handles);

    // Narrowed to the wrapper alone by its own words. Nothing in view names
    // it as a form, so it is not one here.
    let filter = DomQueryFilter::new(None, None, Some("services".to_owned()), 8)
        .unwrap_or_else(|| unreachable!("bounded query"));
    assert!(page.stage_dom_query(source_id(), &filter));
    let narrowed = page.preview().text.unwrap_or_default();
    assert!(
        narrowed.contains("Query result: 1 matching nodes"),
        "{narrowed}"
    );
    assert_eq!(
        value_footer(&narrowed),
        "values: no line on this page takes one — a person cannot type here"
    );
}

#[test]
fn a_later_observation_keeps_earlier_numbers() {
    let mut first = PageArena::new();
    assert!(first.push(node("n-old", "Old")));
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), first)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    page.commit_handles(page.preview().handles);
    let first_issued = page.handles.issued();

    let mut second = PageArena::new();
    assert!(second.push(node("n-new", "New")));
    page.replace_source(source_id(), &evidence(), second)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let preview = page.preview();
    let text = preview.text.expect("a rendered page");
    assert!(text.contains(&format!("[{first_issued}]")));
    assert!(!text.contains("\n[0]"));
}

#[test]
fn research_projection_is_source_ordered_bounded_and_names_incomplete_evidence() {
    let mut later_arena = PageArena::new();
    assert!(later_arena.push(node("n-later", "Later source evidence")));
    let mut later = evidence();
    later.tab_id = TabId::new("tab-2");
    later.normalized_origin = "https://second.example".to_owned();
    later.page_epoch = PageEpoch::new("epoch-2");
    later.completeness = ObservationCompleteness::Incomplete;
    later.truncated = true;

    let mut first_arena = PageArena::new();
    assert!(first_arena.push(node("n-first", "First source evidence")));
    let first = evidence();

    let mut page = LivePage::new();
    page.replace_source(SourceId::from_bytes([4; 16]), &later, later_arena)
        .unwrap_or_else(|_| unreachable!("canonical later evidence"));
    page.replace_source(SourceId::from_bytes([3; 16]), &first, first_arena)
        .unwrap_or_else(|_| unreachable!("canonical first evidence"));

    assert_eq!(
        page.source_ids().collect::<Vec<_>>(),
        vec![SourceId::from_bytes([3; 16]), SourceId::from_bytes([4; 16])]
    );

    let preview = page.preview();
    let text = preview
        .text
        .unwrap_or_else(|| unreachable!("two observations render"));
    let first_position = text
        .find("First source evidence")
        .unwrap_or_else(|| unreachable!("first source is present"));
    let later_position = text
        .find("Later source evidence")
        .unwrap_or_else(|| unreachable!("later source is present"));
    assert!(first_position < later_position);
    assert!(text.contains(
        "Source 2:\nEvidence status: incomplete observation; evidence is missing or may be missing."
    ));
    assert!(text.contains("`Missing evidence:`"));
    assert!(text.contains("`Conflicting evidence:`"));
    assert!(text.contains("Never fill gaps or merge disagreements"));
    assert!(text.len() <= MAX_RESEARCH_PROJECTION_BYTES);
    assert_eq!(preview.handles.issued(), 2);
}

#[test]
fn a_turn_knows_which_document_its_tab_holds_now() {
    let mut first = PageArena::new();
    assert!(first.push(node("n-search", "Search")));
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), first)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let earlier = page.preview();
    page.commit_handles(earlier.handles);
    let left = page.handles.bindings().last().map_or_else(
        || unreachable!("the first reading issued a number"),
        |binding| binding.node().clone(),
    );

    // The tab's root frame now holds another document.
    let mut moved = evidence();
    moved.page_epoch = PageEpoch::new("epoch-2");
    let mut second = PageArena::new();
    assert!(second.push(node("n-download", "Download")));
    page.replace_source(source_id(), &moved, second)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let turn = page.preview().into_turn_page([0; 32]);
    let current = turn.handles().bindings().last().map_or_else(
        || unreachable!("the second reading issued a number"),
        |binding| binding.node().clone(),
    );

    assert!(turn.names_a_page_its_tab_left(&left));
    assert!(!turn.names_a_page_its_tab_left(&current));
}

/// A query that matches nothing does not make the page blank.
///
/// The two assertions are the decision. `carries_page_content` stays false —
/// no page-authored bytes leave, so the disclosure class and the sensitivity
/// classes decision 0070 routes on are unchanged, and a fix that widened
/// them would be a privacy regression wearing a readability fix's clothes.
/// `readability` becomes `Readable`, because the page is there.
///
/// It was `Empty`, and on 2026-09-19 a model narrowed a 350-node page to
/// nothing, was told the page was blank, stopped trusting the handles it had
/// just been given and reached back to a page its tab had left — four refused
/// calls before it handed the errand to the person (decision 0210).
#[test]
fn a_query_that_matches_nothing_does_not_widen_what_the_request_discloses() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-download", "Download report")));
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));

    let full = page.preview();
    assert_eq!(full.readability, PageReadability::Readable);
    assert!(full.carries_page_content);

    let filter = DomQueryFilter::new(
        None,
        Some(DomQueryRole::Button),
        Some("NOTHING ON THIS PAGE SAYS THIS".to_owned()),
        8,
    )
    .unwrap_or_else(|| unreachable!("bounded query"));
    assert!(page.stage_dom_query(source_id(), &filter));

    let narrowed = page.preview();
    assert_eq!(narrowed.offered, 0);
    assert!(
        !narrowed.carries_page_content,
        "no page-authored bytes leave, so the disclosure answer must not move"
    );
    assert_eq!(
        narrowed.readability,
        PageReadability::Readable,
        "the query matched nothing; the page is still there"
    );
    let text = narrowed.text.as_deref().unwrap_or("");
    assert!(
        text.contains("nothing on this page matched that query"),
        "{text}"
    );
    assert!(!text.contains("the page is blank"), "{text}");
}
