// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one model turn is composed from, read from the reducer.
//!
//! Split from the port so the port file stays under the line cap, and because
//! the two functions here answer one question between them: which tab a turn
//! names. They disagreed once — discovery authority ends when a source is
//! bound, and the turn kept asking it for a tab anyway — and a turn that named
//! no tab froze the task (decision 0178).

use super::super::{ModelTurnFacts, TaskDiscoveryAuthorityFacts};
use super::transcript::transcript_of;

/// The exact discovery authority an errand still holds.
///
/// It survives binding a source, because the two grants are independent: the
/// sheet says "read this page" and "you may open up to N sites" in separate
/// sentences, and spending the first never spends the second. What ends this
/// is the cap reaching zero. It used to end at the first source as well,
/// which made the cap unspendable for the ordinary errand — one asked from a
/// page — and sent that errand's first search through the person's own tab
/// (decision 0224).
///
/// The tab named here is only ever a tab the browser prepared for this task.
/// It is not where a turn with no page acts; that is `empty_page_tab_id` in
/// [`model_turn_facts`], and it prefers a tab the task holds a source for.
///
/// One landing, and then it is over. The browser froze the cap in its own
/// bootstrap record when it opened the tab, and `IsExactTaskDiscoveryTab`
/// compares against that frozen number — so the moment anything has been
/// discovered, a fact sent from here is answered `kTabGone`, which is
/// `Abandon` on the recovery ladder and ends the task on a tab that is
/// sitting right there. A spent site is therefore the end of this authority
/// and not merely a smaller cap: it is also, exactly, when the blank tab
/// stopped being blank.
pub(super) fn discovery_authority_facts<C, I>(
    reducer: &task_engine::Reducer<C, I>,
) -> Option<TaskDiscoveryAuthorityFacts>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    let task = reducer.task();
    let snapshot = task.snapshot();
    let discovery_tab_id = snapshot.discovery_tab_id.as_ref()?;
    // Nothing discovered yet. The ledger is charged once for each source
    // consent named and once more for each one the task went and found, so
    // this equality holds exactly until the first landing — which is also
    // exactly when `remaining_new_source_cap` stops matching the number the
    // browser froze when it opened the tab.
    let consented = u64::try_from(snapshot.consented_sources.len()).unwrap_or(u64::MAX);
    if snapshot.template_id != task_engine::TaskTemplateId::WebErrand
        || !snapshot.source_discovery_enabled
        || snapshot.remaining_new_source_cap == 0
        || snapshot.remaining_new_source_cap > task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP
        || task.ledger().spent(task_engine::BudgetKind::MaxSources) != consented
    {
        return None;
    }
    Some(TaskDiscoveryAuthorityFacts {
        discovery_tab_id: discovery_tab_id.as_str().to_owned(),
        browser_session_id: snapshot.browser_session_id.as_str().to_owned(),
        remaining_new_source_cap: snapshot.remaining_new_source_cap,
    })
}

/// The tab the browser opened for this task, for as long as the task lives.
///
/// Not an authority and never checked as one: every gate that spends
/// discovery re-derives its own facts. This answers only "which tab is
/// Taffy's own", which is what a search needs to know.
fn own_tab_id(snapshot: &task_engine::TaskSnapshot) -> Option<String> {
    snapshot
        .discovery_tab_id
        .as_ref()
        .map(|tab| tab.as_str().to_owned())
}

/// Everything one model turn is composed from, read off the reducer.
pub(super) fn model_turn_facts<C, I>(reducer: &task_engine::Reducer<C, I>) -> ModelTurnFacts
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    let task = reducer.task();
    let snapshot = task.snapshot();
    ModelTurnFacts {
        task_id: task.task_id().as_str().to_owned(),
        attempts_started: reducer
            .model_turn()
            .map_or(0, task_engine::ModelTurn::attempts_started),
        candidate_ordinal: reducer
            .model_turn()
            .map_or(0, task_engine::ModelTurn::candidate_ordinal),
        can_afford_model_attempt: reducer.can_afford_model_attempt(),
        transcript: transcript_of(reducer),
        provider_route_id: snapshot
            .provider_route
            .as_ref()
            .map(|route| route.as_str().to_owned()),
        tool_allowlist: snapshot.tool_allowlist.clone(),
        milestone: snapshot.milestone,
        template_id: snapshot.template_id,
        source_count: task.consented_sources().len(),
        remaining_new_source_cap: snapshot.remaining_new_source_cap,
        // A tab the task holds a source for first, then its own tab. This is
        // where a turn with no page acts, so a running errand whose tab has
        // just left its source must land back on that source's tab and not on
        // a tab it has no business reading. A zero-source errand has no first
        // clause to answer and gets its own, which is the whole of what it
        // owns. The order matters only now that a task can hold both
        // (decision 0224); it used to be unobservable, because the two were
        // never both present.
        empty_page_tab_id: task
            .consented_sources()
            .iter()
            .map(|source| source.tab_id.as_str().to_owned())
            .find(|tab| !tab.is_empty())
            .or_else(|| own_tab_id(snapshot)),
        // Read from the snapshot rather than from the authority above,
        // because the tab and the authority end at different moments and a
        // search needs the tab. The authority is spent by the first landing;
        // the tab is the task's for the rest of its life, and a second search
        // belongs in it exactly as the first did. Taking this from the
        // authority sent the second search back through the person's own tab,
        // which is the whole defect one step later — measured on a phone,
        // errand `e9dc28a8`.
        discovery_tab_id: own_tab_id(snapshot),
        persons_pages: reducer.persons_pages(),
        activated: Vec::new(),
        nested_goal: None,
        // A model preference belongs to the provider the person configured,
        // not to the task's durable record, so the reducer has nothing to
        // say about either. Both are filled in by the composition that
        // holds the provider registry, and absent here is the honest
        // answer rather than a default: Taffy decides the rung, and the
        // provider's own order stands.
        thinking: None,
    }
}
