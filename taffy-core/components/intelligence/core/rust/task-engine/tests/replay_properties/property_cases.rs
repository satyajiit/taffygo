// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use proptest::prelude::*;

proptest! {
    #![proptest_config(ProptestConfig::with_cases(96))]

    #[test]
    fn a_rebuild_reconstructs_the_same_task_and_the_same_journal(
        script in prop::collection::vec(0_usize..ALPHABET.len(), 1..40)
    ) {
        let live = run(&script);
        let (rebuilt, recovery) = rebuild(&live.journal);

        prop_assert_eq!(rebuilt.task().state(), live.final_state);
        prop_assert_eq!(rebuilt.task().revision(), live.final_revision);
        prop_assert_eq!(recovery.state, live.final_state);
        prop_assert_eq!(recovery.revision, live.final_revision);
        prop_assert_eq!(rebuilt.journal().entries(), live.journal.entries());
        prop_assert_eq!(recovery.leases_restored, 0);
        // A journal this build wrote never needs a bound held as history: a
        // command it admitted live, it admits again (decision 0235).
        prop_assert!(recovery.admitted_as_history.is_empty());
    }

    /// The loop is pure: the same reconstructed state answers the same
    /// question the same way — with one stated exception, which is the whole of
    /// decision 0150.
    ///
    /// The turn register is compared whole rather than field by field, so a
    /// field added to it later is covered by this property the moment it
    /// exists. Then the loop itself is asked, with no residency — the honest
    /// post-restart condition, since the arena died with the process — and the
    /// two answers have to be the same command, payload included rather than
    /// only the same kind: rows 9 and 15 of the table both answer `FailTask`
    /// and differ only in the reason, so a comparison of kinds would report a
    /// provider outage and an exhausted budget as the same answer. A loop that
    /// answered differently after a restart would mint a different effect
    /// identity, and the browser's ledger would have nothing to refuse.
    ///
    /// **The exception is a call that was still in flight.** The generation
    /// holding it is gone, so nothing will ever deliver its answer, and a
    /// rebuild that reproduced `IN_FLIGHT` would be a task waiting for ever —
    /// which is the wedge decision 0150 was written for. The rebuild gives it
    /// up as a gap instead, names it in the recovery, and asks again. So the
    /// property is stated in two halves: equality where nothing was in flight,
    /// and exactly that one difference where something was.
    #[test]
    fn a_rebuild_reproduces_the_turn_register_and_the_loops_next_answer(
        script in prop::collection::vec(0_usize..ALPHABET.len(), 1..40)
    ) {
        let live = run(&script);
        let (rebuilt, recovery) = rebuild(&live.journal);
        let was_in_flight = live
            .turn
            .as_ref()
            .is_some_and(|turn| turn.phase().is_in_flight());

        prop_assert_eq!(rebuilt.turns_started(), live.turns_started);
        if !was_in_flight {
            prop_assert_eq!(recovery.interrupted_model_call, None);
            prop_assert_eq!(rebuilt.model_turn().cloned(), live.turn);
            prop_assert_eq!(
                rebuilt.next_agent_command(None, &Digest),
                live.next_agent_command
            );
            return Ok(());
        }

        // The one call the dead generation was holding, named rather than
        // silently dropped: the runtime settles a live attempt against it.
        let held = live
            .turn
            .as_ref()
            .map(|turn| turn.call_id().clone());
        prop_assert_eq!(&recovery.interrupted_model_call, &held);
        let restored = rebuilt
            .model_turn()
            .cloned()
            .expect("a turn that was in flight is still a turn after the rebuild");
        prop_assert_eq!(restored.phase(), TurnPhase::Gap(TurnGap::OutcomeUnknown));
        prop_assert_eq!(Some(restored.call_id().clone()), held);
        prop_assert!(!rebuilt.model_turn_in_flight());
    }

    /// Replaying a rebuild's own journal reaches the rebuild.
    ///
    /// The half above states where a rebuild differs from the run. This states
    /// that the difference happens once and settles: whatever the second
    /// generation holds, a third generation built from the same journal holds
    /// exactly that, register and next answer alike. A give-up that moved on
    /// every start would be a task whose loop changed its mind for ever.
    ///
    /// The recovery still *names* the same interrupted call on the third
    /// generation, and that is not a repeat of the give-up: giving up is
    /// derived from the journal rather than written to it, so every generation
    /// re-derives the same conclusion from the same commands. What must not
    /// move is the state it lands in.
    #[test]
    fn a_rebuild_of_a_rebuild_changes_nothing(
        script in prop::collection::vec(0_usize..ALPHABET.len(), 1..40)
    ) {
        let live = run(&script);
        let (second, first_recovery) = rebuild(&live.journal);
        let (third, second_recovery) = rebuild(second.journal());

        prop_assert_eq!(third.task().state(), second.task().state());
        prop_assert_eq!(third.task().revision(), second.task().revision());
        prop_assert_eq!(third.model_turn().cloned(), second.model_turn().cloned());
        prop_assert_eq!(
            second_recovery.interrupted_model_call,
            first_recovery.interrupted_model_call
        );
        prop_assert_eq!(
            third.next_agent_command(None, &Digest),
            second.next_agent_command(None, &Digest)
        );
        prop_assert_eq!(
            third.task().activity().steps(),
            second.task().activity().steps()
        );
    }

    #[test]
    fn a_rebuild_never_duplicates_a_dispatched_action(
        script in prop::collection::vec(0_usize..ALPHABET.len(), 1..40)
    ) {
        let live = run(&script);
        let (rebuilt, recovery) = rebuild(&live.journal);

        // Every dispatch that happened is in the journal exactly once.
        let journalled = live.journal.dispatched_keys();
        let mut unique = journalled.clone();
        unique.sort();
        unique.dedup();
        prop_assert_eq!(journalled.len(), unique.len());

        // The rebuild knows the same dispatched keys and adds none.
        let after: Vec<IdempotencyKey> = rebuilt.dispatched_keys().iter().cloned().collect();
        prop_assert_eq!(&after, &live.dispatched);
        prop_assert_eq!(
            rebuilt.journal().dispatched_keys().len(),
            journalled.len()
        );
        prop_assert_eq!(
            u64::try_from(live.journal.commands().count()).unwrap_or(u64::MAX),
            recovery.commands_replayed
        );
    }

    #[test]
    fn a_rebuild_never_invents_completion(
        script in prop::collection::vec(0_usize..ALPHABET.len(), 1..40)
    ) {
        let live = run(&script);
        let (rebuilt, recovery) = rebuild(&live.journal);

        if live.final_state != TaskState::Completed {
            prop_assert_ne!(rebuilt.task().state(), TaskState::Completed);
        }
        if rebuilt.task().state() == TaskState::Completed {
            let complete = rebuilt
                .task()
                .terminal_result()
                .is_some_and(task_engine::TaskResult::is_complete);
            prop_assert!(complete);
        }

        // An attempt that was in flight when the process died is unknown, never
        // verified and never retried.
        prop_assert_eq!(
            recovery.unknown_outcome_actions.len(),
            live.in_flight_actions
        );
        for action_id in &recovery.unknown_outcome_actions {
            prop_assert_eq!(
                rebuilt.action(action_id).map(task_engine::ActionRecord::state),
                Some(ActionState::OutcomeUnknown)
            );
            prop_assert!(!rebuilt
                .action(action_id)
                .is_some_and(|action| action.recovery_rule().permits_unattended_retry()
                    && action.proposal().idempotency()
                        != task_engine::IdempotencyClass::PureRead));
        }
    }

    #[test]
    fn a_rebuild_lands_on_every_point_the_run_passed_through(
        script in prop::collection::vec(0_usize..ALPHABET.len(), 1..24)
    ) {
        let live = run(&script);
        let entries = live.journal.entries();
        for checkpoint in &live.checkpoints {
            let Some(prefix) = entries.get(..checkpoint.entries) else {
                prop_assert!(false, "checkpoint beyond the journal");
                continue;
            };
            let partial = match TaskJournal::from_entries(prefix) {
                Ok(partial) => partial,
                Err(error) => {
                    prop_assert!(false, "a prefix of a written journal must load: {error:?}");
                    continue;
                }
            };
            let (rebuilt, recovery) = rebuild(&partial);
            prop_assert_eq!(rebuilt.task().state(), checkpoint.state);
            prop_assert_eq!(rebuilt.task().revision(), checkpoint.revision);
            prop_assert_eq!(recovery.state, checkpoint.state);
        }
    }
}
