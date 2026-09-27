// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A journal is history, and a later build's stricter bound on the task's own
//! conduct is not re-judged over commands an earlier build admitted (decision
//! 0235).
//!
//! On 2026-09-23 a phone installed the build that taught the repetition
//! register to count policy denials (decision 0233) and the core would not
//! start: `[taffy_core_initialization_refused] reason=task-restore label=`.
//! The in-progress errand was not the cause. Two errands in the same profile,
//! both already stopped, had each proposed a call policy had already
//! denied three times the same way; the build that ran them admitted those
//! proposals, and the build that replayed them refused the fourth
//! `repeated_refusals_abandoned`. Every start replays every task, so one
//! finished errand refused the whole profile.
//!
//! Each test here writes a journal under one admission rule and replays it
//! under a stricter one. The repetition rule is compiled in, so the journal
//! the earlier build wrote is made the only honest way available: the current
//! reducer writes it with the third denial under a different code, which it
//! does not count as a repeat, and the one field that differs is then set to
//! the code the phone's journal carried. What results is exactly the command
//! sequence a build that did not count policy denials wrote. The budget rule
//! is a parameter of the replay itself, so that one needs no splice at all.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{ActionId, SemanticNodeId, TabId};
use bip_types::ActionResultCode;
use task_engine::action::{ActionProposal, BrowserIntent, ScrollDirection};
use task_engine::authority::{Denial, ProposalDecision};
use task_engine::budget::{BudgetDefaults, BudgetKind};
use task_engine::command::{Command, CommandEnvelope, CommandKind};
use task_engine::ids::IdempotencyKey;
use task_engine::reducer::Reducer;
use task_engine::transition::RefusalReason;
use task_engine::{
    CallFingerprint, HistoryAdmission, JournalEntry, ManualClock, Recovery, ReplayError,
    SequentialIds, TaskJournal, TaskState, TraceId, MAX_IDENTICAL_REFUSALS,
};

const NODE: &str = "node_7";
const LOOP_TOOL: &str = "browser.dom.scroll";

type TestReducer = Reducer<ManualClock, SequentialIds>;

fn scroll(key: &str) -> ActionProposal {
    common::proposal_for(
        BrowserIntent::DomScroll {
            tab: TabId::new("tab_1"),
            direction: ScrollDirection::ToNode,
            target: Some(SemanticNodeId::new(NODE)),
        },
        key,
    )
}

fn call() -> CallFingerprint {
    CallFingerprint::of_target(LOOP_TOOL, Some("tab_1"), Some(NODE))
}

fn rounds() -> usize {
    usize::try_from(MAX_IDENTICAL_REFUSALS).unwrap()
}

/// Proposes the one call and has policy deny it with `code`.
fn propose_and_deny(fixture: &mut common::Fixture, key: &str, code: ActionResultCode) {
    let existing = fixture
        .reducer
        .actions()
        .map(|action| action.action_id().clone())
        .collect::<Vec<_>>();
    fixture.must_apply(Command::ProposeAction(Box::new(scroll(key))));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| !existing.contains(action.action_id()))
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id,
        decision: Box::new(ProposalDecision::Deny(Denial::new(code))),
        dispatch_id: None,
    });
}

/// The action a proposal keyed `key` minted.
fn action_keyed(reducer: &TestReducer, key: &str) -> ActionId {
    reducer
        .actions()
        .find(|action| action.proposal().idempotency_key == IdempotencyKey::new(key))
        .map(|action| action.action_id().clone())
        .expect("the proposal is in the register")
}

/// The journal a build that did not count policy denials wrote for one more
/// proposal of a call policy had denied three times: four proposals of the
/// same call, each denied `DeniedByPolicy`, all admitted.
fn journal_from_before_policy_denials_counted() -> TaskJournal {
    let mut fixture = common::running();
    let keys = (0..=rounds())
        .map(|round| format!("denied_key_{round}"))
        .collect::<Vec<_>>();
    let stand_in = ActionResultCode::CapabilityExpired;
    for (round, key) in keys.iter().enumerate() {
        let code = if round + 1 == rounds() {
            stand_in
        } else {
            ActionResultCode::DeniedByPolicy
        };
        propose_and_deny(&mut fixture, key, code);
    }
    let third = action_keyed(&fixture.reducer, &keys[rounds() - 1]);
    let mut entries = fixture.reducer.journal().entries().to_vec();
    let mut spliced = 0;
    for entry in &mut entries {
        if let JournalEntry::Command(record) = entry {
            if let Command::RecordPolicyDecision {
                action_id,
                decision,
                ..
            } = &mut record.envelope.command
            {
                if *action_id == third {
                    **decision =
                        ProposalDecision::Deny(Denial::new(ActionResultCode::DeniedByPolicy));
                    spliced += 1;
                }
            }
        }
    }
    assert_eq!(spliced, 1, "exactly the third denial is rewritten");
    TaskJournal::from_entries(&entries).expect("the spliced journal is well formed")
}

fn replay_within(
    defaults: BudgetDefaults,
    journal: &TaskJournal,
) -> Result<(TestReducer, Recovery), ReplayError> {
    Reducer::replay(
        common::seed(),
        defaults,
        ManualClock::at(1_000),
        SequentialIds::new(),
        journal,
    )
}

fn live(reducer: &TestReducer, key: &str, command: Command) -> CommandEnvelope {
    CommandEnvelope::new(
        IdempotencyKey::new(key),
        reducer.task().revision(),
        TraceId::new("trace_test"),
        command,
    )
}

/// The sequence of the proposal keyed `key` in `journal`.
fn sequence_of_proposal(journal: &TaskJournal, key: &str) -> u64 {
    journal
        .commands()
        .find(|record| {
            matches!(&record.envelope.command,
                Command::ProposeAction(proposal)
                    if proposal.idempotency_key == IdempotencyKey::new(key))
        })
        .map(|record| record.sequence)
        .expect("the proposal is journalled")
}

/// The phone's two finished errands, in miniature: the proposal the earlier
/// build admitted replays as the history it is, the task comes back exactly
/// as the journal recorded it, and the recovery names the one command it was
/// held over. The same journal is refused by the replay without this rule,
/// which is the whole defect.
#[test]
fn a_proposal_the_repetition_bound_now_refuses_replays_as_history() {
    let journal = journal_from_before_policy_denials_counted();
    let (rebuilt, recovery) =
        replay_within(common::defaults(), &journal).expect("the earlier build's journal replays");

    assert_eq!(recovery.state, TaskState::Running);
    assert_eq!(rebuilt.task().revision(), journal.revision());
    assert_eq!(rebuilt.journal().entries(), journal.entries());
    let fourth = format!("denied_key_{}", rounds());
    assert_eq!(
        recovery.admitted_as_history,
        vec![HistoryAdmission {
            sequence: sequence_of_proposal(&journal, &fourth),
            command: CommandKind::ProposeAction,
            bound: RefusalReason::RepeatedRefusalsAbandoned,
        }]
    );
}

/// Nothing was widened. The register the replay rebuilt counts every denial
/// the journal holds, the fourth included, so the call is abandoned and the
/// first proposal of it the restored task is asked to admit live is refused
/// exactly as this build refuses it anywhere else.
#[test]
fn the_bound_applies_to_the_first_live_command_after_the_restore() {
    let journal = journal_from_before_policy_denials_counted();
    let (mut rebuilt, _) = replay_within(common::defaults(), &journal).unwrap();

    assert!(rebuilt.refusals().is_abandoned(call()));
    assert_eq!(
        rebuilt
            .refusals()
            .count_of(call(), ActionResultCode::DeniedByPolicy),
        MAX_IDENTICAL_REFUSALS + 1
    );
    let fifth = live(
        &rebuilt,
        "key_live_fifth",
        Command::ProposeAction(Box::new(scroll("denied_key_live"))),
    );
    assert_eq!(
        rebuilt
            .apply(fifth)
            .map(|_| ())
            .map_err(|refusal| refusal.reason),
        Err(RefusalReason::RepeatedRefusalsAbandoned),
        "a live proposal is judged by this build's bound"
    );
}

/// And it holds for every generation after: the restored journal, and the
/// journal a live command then extends, replay the same way each time.
#[test]
fn a_journal_held_as_history_replays_for_ever_after() {
    let journal = journal_from_before_policy_denials_counted();
    let (mut generation, first) = replay_within(common::defaults(), &journal).unwrap();
    let stop = live(&generation, "key_live_stop", Command::CancelTask);
    generation.apply(stop).expect("a stop is admitted live");
    for _ in 0..3 {
        let (next, recovery) = replay_within(common::defaults(), generation.journal())
            .expect("the extended journal replays");
        assert_eq!(recovery.admitted_as_history, first.admitted_as_history);
        assert_eq!(recovery.state, generation.task().state());
        assert_eq!(next.journal().entries(), generation.journal().entries());
        generation = next;
    }
}

/// The budget ledger is the other bound a replay holds as history. A journal
/// written under a looser limit — the stand-in for a build that charges a
/// model turn more, or allows fewer — replays under a stricter one, and the
/// restored task cannot spend past what this build allows.
#[test]
fn a_draw_the_budget_now_refuses_replays_as_history() {
    let mut fixture = common::agent::running();
    for _ in 0..2 {
        let turn = common::agent::request_turn(&fixture);
        fixture.must_apply(turn);
        let recorded = common::agent::record_turn(&fixture);
        fixture.must_apply(recorded);
    }
    let journal = fixture.reducer.journal().clone();
    let stricter = BudgetDefaults::new(|kind| match kind {
        BudgetKind::MaxModelRequests => 1,
        other => common::defaults().limit(other),
    });

    let (mut rebuilt, recovery) =
        replay_within(stricter, &journal).expect("the looser build's journal replays");
    assert_eq!(rebuilt.task().revision(), journal.revision());
    assert_eq!(rebuilt.journal().entries(), journal.entries());
    assert_eq!(recovery.admitted_as_history.len(), 1);
    assert_eq!(
        recovery.admitted_as_history[0].command,
        CommandKind::RequestModelTurn
    );
    assert_eq!(
        recovery.admitted_as_history[0].bound,
        RefusalReason::BudgetExhausted
    );

    let third = live(
        &rebuilt,
        "key_live_turn",
        Command::RequestModelTurn {
            call_id: rebuilt.next_model_call_id(),
        },
    );
    assert_eq!(
        rebuilt
            .apply(third)
            .map(|_| ())
            .map_err(|refusal| refusal.reason),
        Err(RefusalReason::BudgetExhausted),
        "the restored task spends nothing past this build's limit"
    );
}

/// History is not a pass for anything else. A journal whose policy decision
/// names an action the task never proposed is one this reducer cannot
/// rebuild, and it is refused by name as it always was: identity, authority
/// and the state table are what the fold stands on.
#[test]
fn a_replay_still_refuses_what_it_cannot_rebuild() {
    let mut fixture = common::running();
    propose_and_deny(
        &mut fixture,
        "denied_key_0",
        ActionResultCode::DeniedByPolicy,
    );
    let mut entries = fixture.reducer.journal().entries().to_vec();
    let decision = entries
        .iter_mut()
        .rev()
        .find_map(|entry| match entry {
            JournalEntry::Command(record) => match &mut record.envelope.command {
                Command::RecordPolicyDecision { action_id, .. } => Some(action_id),
                _ => None,
            },
            JournalEntry::Event(_) => None,
        })
        .expect("the denial is journalled");
    *decision = ActionId::new("action_never_proposed");
    let journal = TaskJournal::from_entries(&entries).unwrap();

    let refused = replay_within(common::defaults(), &journal).map(|_| ());
    assert!(
        matches!(
            refused,
            Err(ReplayError::CommandRefused { refusal, .. })
                if refusal.reason == RefusalReason::UnknownAction
                    && refusal.command == CommandKind::RecordPolicyDecision
        ),
        "{refused:?}"
    );
    assert_eq!(
        refused.unwrap_err().label(),
        RefusalReason::UnknownAction.label()
    );
}
