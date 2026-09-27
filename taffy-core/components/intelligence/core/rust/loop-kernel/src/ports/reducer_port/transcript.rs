// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The conversation a task carries into its next turn, read from the reducer.
//!
//! Split from the port so the port file stays under the line cap and so the
//! one function that turns durable action records into a model-facing
//! transcript has a file of its own to be read in.

use std::collections::BTreeMap;

use task_engine::{
    ActionState, FRUITLESS_ERRAND_ARRIVAL_NUDGE, TURNS_WITHOUT_PROGRESS_NUDGE,
    UNANSWERED_VALUE_ASK_NUDGE,
};

use crate::context::opening::{
    errand_situation, values_after_placing, COMPARISON_PREAMBLE, ERRAND_NUDGE, ERRAND_PREAMBLE,
    ERRAND_REPEAT_NUDGE, SNAPSHOT_LINE, STALLED_TURNS_NUDGE, UNANSWERED_VALUES_NUDGE,
};

/// The conversation so far, rebuilt from the reducer's own durable records.
///
/// Pure: no clock, no identifier mint, no IO, and nothing remembered between
/// calls. It reads the action records the reducer already holds, which is the
/// only durable trace a past turn leaves — [`task_engine::TurnDigest`] declares
/// no string, so the journal keeps the *shape* of a turn and the actions keep
/// which tool was called and what became of it (decision 0052 section 5).
///
/// Grouping is by the identity the key already carries rather than by anything
/// this function decides. `turn_call_of` is the inverse of the minting in
/// `task-engine`, so the format is read where it is written; an action whose
/// key names no turn — a plan step's, today — is simply not part of the
/// conversation and is skipped.
///
/// Two things a reader should know it does *not* see. A call the reducer
/// refused on sight never became an action, so it leaves no record here.
/// The still-resident Length-stop overlay is what tells the model to
/// re-issue; a restart without residency still has no such exchange. The
/// arguments are gone for the reason [`crate::context::transcript`] states.
/// Both are closed by the turn's residency, which this reader does not have.
pub(super) fn transcript_of<C, I>(
    reducer: &task_engine::Reducer<C, I>,
) -> crate::context::TaskTranscript
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    let mut by_turn: BTreeMap<u64, BTreeMap<u32, crate::context::RecordedCall>> = BTreeMap::new();
    for action in reducer.actions() {
        let proposal = action.proposal();
        let Some((ordinal, sequence)) = task_engine::turn_call_of(&proposal.idempotency_key) else {
            continue;
        };
        // No arguments survive in a durable record, and an empty object is
        // what that looks like on the wire. The composer that holds the
        // residency supplies the real ones.
        let arguments = model_router::json::JsonValue::Object(BTreeMap::new());
        let call_id = proposal.idempotency_key.as_str().to_owned();
        let tool = proposal.tool_name().to_owned();
        // A refused or failed action carries the code the browser or policy
        // answered with, and the model reads the sentence for that code
        // rather than the bare word. Every other state keeps the word alone.
        let recorded = match (action.state(), action.result()) {
            (state @ (ActionState::Rejected | ActionState::Failed), Some(code)) => {
                crate::context::RecordedCall::refused(call_id, tool, arguments, state, code)
            }
            (state, _) => crate::context::RecordedCall::new(call_id, tool, arguments, state),
        };
        by_turn
            .entry(ordinal)
            .or_default()
            .insert(sequence, recorded);
    }
    let exchanges = by_turn
        .into_iter()
        .filter_map(|(ordinal, calls)| {
            crate::context::TurnExchange::new(ordinal, calls.into_values().collect())
        })
        .collect();
    crate::context::TaskTranscript::with_floor(
        reducer.task().user_goal().to_owned(),
        exchanges,
        crate::context::TranscriptBudget::default(),
        reducer.evicted_through(),
    )
    .with_preface(opening_pieces(reducer))
}

/// The compiled-in pieces the model reads after the goal, when the task's
/// shape calls for any.
///
/// A comparison gets offer and price guidance. Other research opens with its
/// goal and page alone. An errand gets the preamble, its position from counts the reducer
/// holds, the snapshot rule, what came of its last request for values, and —
/// after a reply that did nothing, or a run of turns that changed nothing —
/// the nudge.
/// Nothing here is read from a page, a reply or the goal.
fn opening_pieces<C, I>(reducer: &task_engine::Reducer<C, I>) -> Vec<String>
where
    C: task_engine::Clock,
    I: task_engine::IdSource,
{
    if reducer.task().snapshot().template_id == task_engine::TaskTemplateId::CompareProducts {
        return vec![COMPARISON_PREAMBLE.to_owned()];
    }
    if !reducer.is_web_errand() {
        return Vec::new();
    }
    let task = reducer.task();
    let snapshot = task.snapshot();
    let mut pieces = vec![
        ERRAND_PREAMBLE.to_owned(),
        errand_situation(
            task.consented_sources().len(),
            snapshot.remaining_new_source_cap,
            snapshot.discovery_tab_id.is_some(),
        ),
        SNAPSHOT_LINE.to_owned(),
    ];
    // Told after the task has put what it could into the page, so the model
    // hears what is filled and what is still its to fill (decision 0238).
    if let Some(supplied) = reducer.supplied_field_values() {
        pieces.push(values_after_placing(
            reducer.held_values_placed(),
            supplied.count().get(),
            supplied.outcome(),
        ));
    }
    if reducer.unproductive_replies() > 0 {
        pieces.push(ERRAND_NUDGE.to_owned());
    }
    if reducer.fruitless_arrivals() >= FRUITLESS_ERRAND_ARRIVAL_NUDGE {
        pieces.push(ERRAND_REPEAT_NUDGE.to_owned());
    }
    if reducer.unanswered_value_asks() >= UNANSWERED_VALUE_ASK_NUDGE {
        pieces.push(UNANSWERED_VALUES_NUDGE.to_owned());
    }
    if reducer.turns_without_progress() >= TURNS_WITHOUT_PROGRESS_NUDGE {
        pieces.push(STALLED_TURNS_NUDGE.to_owned());
    }
    pieces
}

#[cfg(test)]
mod tests {
    use bip_types::identity::{ProfileId, TaskId};
    use task_engine::{
        BrowserSessionId, BudgetDefaults, ControlMode, IdempotencyKey, ManualClock, Milestone,
        PolicyVersion, Reducer, SequentialIds, TaskBudgets, TaskKind, TaskSeed, TaskSnapshot,
        TaskTemplateId, TraceId,
    };

    use super::transcript_of;
    use crate::context::opening::{ERRAND_NUDGE, ERRAND_PREAMBLE, SNAPSHOT_LINE};

    fn reducer(template: TaskTemplateId, cap: u32) -> Reducer<ManualClock, SequentialIds> {
        Reducer::create(
            TaskSeed {
                task_id: TaskId::new("task-1"),
                workspace_id: None,
                browser_profile_id: ProfileId::new("profile-1"),
                kind: TaskKind::Research,
                user_goal: "a goal the preface never repeats".to_owned(),
                control_mode: ControlMode::Assistant,
                snapshot: TaskSnapshot {
                    template_id: template,
                    assistant_config_version: 1,
                    skill_version_id: None,
                    builtin_skill: None,
                    tool_allowlist: Vec::new(),
                    capability_policy_version: PolicyVersion(1),
                    provider_route: None,
                    consented_sources: Vec::new(),
                    source_discovery_enabled: cap > 0,
                    remaining_new_source_cap: cap,
                    discovery_tab_id: None,
                    library_refresh: None,
                    browser_session_id: BrowserSessionId::new("browser-session-1")
                        .unwrap_or_else(|_| unreachable!()),
                    milestone: Milestone::M5,
                },
                budgets: TaskBudgets::none(),
                deadline: None,
                deadline_utc: None,
                predecessor_task_id: None,
            },
            BudgetDefaults::uniform(8),
            ManualClock::at(1_000),
            SequentialIds::new(),
            IdempotencyKey::new("create"),
            TraceId::new("trace"),
        )
    }

    #[test]
    fn an_errand_opens_with_the_preamble_its_position_and_the_snapshot_rule() {
        let transcript = transcript_of(&reducer(TaskTemplateId::WebErrand, 3));
        let preface = transcript.preface();
        assert_eq!(preface.len(), 3);
        assert_eq!(preface.first().map(String::as_str), Some(ERRAND_PREAMBLE));
        assert!(preface
            .get(1)
            .is_some_and(|line| line.contains("no site yet") && line.contains("3 more new sites")));
        assert_eq!(preface.get(2).map(String::as_str), Some(SNAPSHOT_LINE));
        // No reply has been recorded, so nothing has been done "yet" in the
        // sense the nudge means, and the nudge is absent.
        assert!(!preface.iter().any(|line| line == ERRAND_NUDGE));
        assert!(!preface
            .iter()
            .any(|line| line.contains("a goal the preface")));
    }

    #[test]
    fn other_research_templates_keep_their_unprefaced_opening() {
        for template in [
            TaskTemplateId::SummarizeEvidence,
            TaskTemplateId::BuildSourceTable,
        ] {
            assert!(
                transcript_of(&reducer(template, 0)).preface().is_empty(),
                "{template:?}"
            );
        }
    }

    #[test]
    fn a_comparison_opening_guides_offer_differences_without_rewriting_evidence() {
        use model_router::wire::{Speaker, Turn};

        let transcript = transcript_of(&reducer(TaskTemplateId::CompareProducts, 0));
        assert_eq!(transcript.preface().len(), 1);
        let instruction = transcript.preface().first().expect("comparison guidance");
        for required in [
            "Answer the requested comparison directly",
            "differences, not by themselves conflicting evidence",
            "Source label and observed title",
            "lowest observed offer",
            "calculate the price difference",
            "currency, product/variant and price basis are comparable",
            "listed item prices from delivered totals",
            "only when material",
            "Never invent exchange rates or missing terms",
            "cheapest beyond the sources read",
        ] {
            assert!(instruction.contains(required), "{required}");
        }
        let evidence =
            "Source 1: Seller A, variant A, EUR 80\nSource 2: Seller B, variant B, GBP 70";
        let views = transcript.views(Some(evidence), None);
        let turns = views.turns();
        let Some(Turn::Said { speaker, text }) = turns.first() else {
            panic!("the comparison is in the actual opening turn");
        };
        assert_eq!(*speaker, Speaker::User);
        assert_eq!(
            *text,
            [
                "a goal the preface never repeats",
                instruction.as_str(),
                evidence
            ]
        );
        assert_eq!(turns.len(), 1);
        assert!(!instruction.contains("a goal the preface"));
    }
}
