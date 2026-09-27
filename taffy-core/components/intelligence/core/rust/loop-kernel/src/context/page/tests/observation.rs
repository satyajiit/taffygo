// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which observation a task holds, and when it may become workspace content.

use super::{evidence, node, source, source_id};
use crate::context::arena::PageArena;
use crate::context::page::{
    LivePage, LivePageObservationState, MAX_WORKSPACE_PAGE_FACTS, MAX_WORKSPACE_PAGE_FACT_BYTES,
};
use bip_types::identity::PageEpoch;
use task_engine::ObservationCompleteness;

#[test]
fn a_task_that_has_observed_nothing_offers_no_page() {
    let page = LivePage::new();
    assert_eq!(
        page.observation_state_for(&source("tab-1", "https://example.test")),
        LivePageObservationState::Empty
    );
    let preview = page.preview();
    assert!(preview.text.is_none());
    assert!(!preview.carries_page_content);
    assert_eq!(preview.handles.issued(), 0);
}

#[test]
fn invalidating_a_handover_page_never_reuses_its_old_control_numbers() {
    let mut page = LivePage::new();
    let mut arena = PageArena::new();
    assert!(arena.push(node("before-handover", "Continue")));
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!());
    let before = page.preview();
    assert_eq!(before.handles.issued(), 1);
    page.commit_handles(before.handles);
    page.invalidate_observations();
    assert!(page.preview().text.is_none());
    let mut fresh = PageArena::new();
    assert!(fresh.push(node("after-handover", "Download")));
    let mut observed = evidence();
    observed.page_epoch = PageEpoch::new("after-handover");
    page.replace_source(source_id(), &observed, fresh)
        .unwrap_or_else(|_| unreachable!());
    let after = page.preview();
    assert_eq!(after.handles.issued(), 2);
    assert_eq!(
        after
            .handles
            .resolve_value(0)
            .map(|node| node.node_id.as_str()),
        Some("before-handover")
    );
    assert_eq!(
        after
            .handles
            .resolve_value(1)
            .map(|node| node.node_id.as_str()),
        Some("after-handover")
    );
}

#[test]
fn navigation_keeps_other_tabs_and_never_reuses_retired_handles() {
    let mut page = LivePage::new();
    let mut first = PageArena::new();
    assert!(first.push(node("before-navigation", "Continue")));
    page.replace_source(source_id(), &evidence(), first)
        .unwrap_or_else(|_| unreachable!());
    let mut second = evidence();
    second.tab_id = bip_types::identity::TabId::new("tab-other");
    let second_source = task_engine::SourceId::from_bytes([8; 16]);
    let mut other = PageArena::new();
    assert!(other.push(node("other-page", "Keep this page")));
    page.replace_source(second_source, &second, other)
        .unwrap_or_else(|_| unreachable!());
    let before = page.preview();
    assert_eq!(before.handles.issued(), 2);
    page.commit_handles(before.handles);

    page.invalidate_tab_observation(&evidence().tab_id);
    assert_eq!(page.source_ids().collect::<Vec<_>>(), vec![second_source]);
    let retained = page.preview();
    assert!(retained
        .text
        .as_deref()
        .is_some_and(|text| text.contains("Keep this page")));

    let mut fresh = PageArena::new();
    assert!(fresh.push(node("after-navigation", "Download")));
    let mut next = evidence();
    next.page_epoch = PageEpoch::new("new-document");
    page.replace_source(source_id(), &next, fresh)
        .unwrap_or_else(|_| unreachable!());
    let after = page.preview();
    assert_eq!(
        after
            .handles
            .resolve_value(0)
            .map(|node| node.node_id.as_str()),
        Some("before-navigation")
    );
    assert!((2..after.handles.issued()).any(|index| after
        .handles
        .resolve_value(index)
        .is_some_and(|node| node.node_id.as_str() == "after-navigation")));
}

#[test]
fn a_recorded_control_requires_the_complete_current_observation_identity() {
    use task_engine::action::ObservedNodeHandle;

    let mut page = LivePage::new();
    let mut arena = PageArena::new();
    assert!(arena.push(node("continue", "Continue")));
    page.replace_source(source_id(), &evidence(), arena.clone())
        .unwrap_or_else(|_| unreachable!());
    let preview = page.preview();
    let target = preview
        .handles
        .resolve_value(0)
        .and_then(ObservedNodeHandle::from_node_handle)
        .unwrap_or_else(|| unreachable!());
    assert_eq!(
        page.observed_node(&target)
            .and_then(|node| node.name.as_deref()),
        Some("Continue")
    );
    let mut changed = evidence();
    changed.graph_revision += 1;
    page.replace_source(source_id(), &changed, arena)
        .unwrap_or_else(|_| unreachable!());
    assert!(page.observed_node(&target).is_none());
    page.invalidate_observations();
    assert!(page.observed_node(&target).is_none());
}

#[test]
fn observation_state_distinguishes_the_exact_source_from_stale_page_bytes() {
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), PageArena::new())
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    assert_eq!(
        page.observation_state_for(&source("tab-1", "https://example.test")),
        LivePageObservationState::Matching
    );
    assert_eq!(
        page.observation_state_for(&source("tab-other", "https://example.test")),
        LivePageObservationState::Stale
    );
    // The tab is the match, and a source whose admitted origin differs from
    // the one the reading came from is a tab that moved within its own site —
    // which is the case the browser already authorized and the one the core
    // used to refuse (decision 0160 section 2).
    assert_eq!(
        page.observation_state_for(&source("tab-1", "https://other.test")),
        LivePageObservationState::Matching
    );
    assert!(page.has_exact_observation(&source("tab-1", "https://example.test"), &evidence()));
    let mut different_epoch = evidence();
    different_epoch.page_epoch = PageEpoch::new("epoch-other");
    assert!(
        !page.has_exact_observation(&source("tab-1", "https://example.test"), &different_epoch,)
    );
}

#[test]
fn only_complete_exact_readable_observation_becomes_workspace_content() {
    let mut arena = PageArena::new();
    assert!(arena.push(node("n-1", "Already redacted evidence")));
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let exact = evidence();
    assert_eq!(
        page.redacted_workspace_content(&exact)
            .map(|content| content.values().to_vec()),
        Some(vec!["Already redacted evidence".to_owned()])
    );

    let mut incomplete = exact.clone();
    incomplete.completeness = ObservationCompleteness::Incomplete;
    incomplete.truncated = true;
    assert_eq!(page.redacted_workspace_content(&incomplete), None);
    let mut mismatched_redaction = exact.clone();
    mismatched_redaction.redacted_field_count = 1;
    assert_eq!(page.redacted_workspace_content(&mismatched_redaction), None);
    let mut superseded = exact;
    superseded.graph_revision = 4;
    assert_eq!(page.redacted_workspace_content(&superseded), None);

    let mut empty = LivePage::new();
    empty
        .replace_source(source_id(), &evidence(), PageArena::new())
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    assert_eq!(empty.redacted_workspace_content(&evidence()), None);
}

#[test]
fn workspace_content_is_fact_count_and_utf8_byte_bounded() {
    let mut arena = PageArena::new();
    let long = "🙂".repeat(300);
    for ordinal in 0..8 {
        assert!(arena.push(node(&format!("n-{ordinal}"), &long)));
    }
    let mut page = LivePage::new();
    page.replace_source(source_id(), &evidence(), arena)
        .unwrap_or_else(|_| unreachable!("canonical evidence"));
    let content = page
        .redacted_workspace_content(&evidence())
        .unwrap_or_else(|| unreachable!("readable exact page has content"));
    assert_eq!(content.values().len(), MAX_WORKSPACE_PAGE_FACTS);
    assert!(content.values().iter().all(|value| {
        value.len() == MAX_WORKSPACE_PAGE_FACT_BYTES
            && value.chars().all(|character| character == '🙂')
    }));
}
