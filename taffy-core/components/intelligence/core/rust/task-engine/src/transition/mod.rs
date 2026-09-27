// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The transition table (domain model section 9.2).
//!
//! # The table is the documentation
//!
//! [`disposition`] is total over [`TaskState`] times [`CommandKind`]: every one
//! of the pairs has a written-down outcome, and the exhaustive test walks all
//! of them and checks the reducer agrees. A cell that says "refused" names the
//! reason; a cell that says "transition" names every state it may reach and
//! every guard that has to hold first.
//!
//! # What the diagram decides and what this module decides
//!
//! Section 9.2's diagram owns the edges. Nothing here adds one: a command is
//! either mapped onto an edge the diagram draws, mapped onto no edge at all
//! (and then it is [`Disposition::Recorded`], changing data without changing
//! state), explicitly allowed to do either based on its closed payload, or
//! refused. Where a refusal looks surprising it is the diagram
//! speaking — a queued task has no pause edge because it holds no lease and no
//! in-flight work, and a pausing task has no cancel edge because it is already
//! settling towards a state that does.

mod disposition;
mod guard;
mod refusal;
mod table;

pub use self::disposition::Disposition;
pub use self::guard::Guard;
pub use self::refusal::RefusalReason;
pub use self::table::disposition;

#[cfg(test)]
mod tests {
    use super::{disposition, Disposition, Guard, RefusalReason};
    use crate::command::CommandKind;
    use crate::task::TaskState;

    #[test]
    fn the_table_is_total_over_every_state_and_every_command() {
        let mut cells = 0_usize;
        for state in TaskState::ALL {
            for command in CommandKind::ALL {
                let cell = disposition(*state, *command);
                match cell {
                    Disposition::Transition { targets, .. }
                    | Disposition::RecordedOrTransition { targets, .. } => {
                        assert!(
                            !targets.is_empty(),
                            "{} x {} moves nowhere",
                            state.label(),
                            command.label()
                        );
                        for target in targets {
                            assert_ne!(
                                target,
                                state,
                                "{} x {} is a self transition",
                                state.label(),
                                command.label()
                            );
                        }
                    }
                    Disposition::Recorded { .. } | Disposition::Refused(_) => {}
                }
                cells += 1;
            }
        }
        assert_eq!(cells, TaskState::ALL.len() * CommandKind::ALL.len());
    }

    #[test]
    fn a_terminal_task_never_moves_and_never_proposes() {
        for state in TaskState::TERMINAL {
            for command in CommandKind::ALL {
                let cell = disposition(*state, *command);
                // The one door out of a finished task: a follow-up reopens a
                // completed or partial task on its own conversation
                // (decision 0137). Nothing else moves any terminal state.
                let reopens = *command == CommandKind::FollowUp
                    && matches!(state, TaskState::Completed | TaskState::Partial);
                if reopens {
                    assert_eq!(
                        cell.targets(),
                        &[TaskState::Running],
                        "{} x {} reopens the task",
                        state.label(),
                        command.label()
                    );
                } else {
                    assert!(
                        cell.targets().is_empty(),
                        "{} x {} moves a terminal task",
                        state.label(),
                        command.label()
                    );
                }
            }
            for command in [
                CommandKind::ProposeAction,
                CommandKind::DispatchAction,
                CommandKind::ExecutorStarted,
            ] {
                assert_eq!(
                    disposition(*state, command).refusal(),
                    Some(RefusalReason::TaskIsTerminal),
                    "{} x {}",
                    state.label(),
                    command.label()
                );
            }
        }
    }

    #[test]
    fn every_target_of_every_cell_is_a_state_the_diagram_can_reach() {
        // Section 9.2 draws exactly these edges. A cell that names a target
        // outside them would be a transition this crate invented.
        const EDGES: &[(TaskState, TaskState)] = &[
            (TaskState::Draft, TaskState::AwaitingConsent),
            (TaskState::Draft, TaskState::Cancelled),
            (TaskState::AwaitingConsent, TaskState::Draft),
            (TaskState::AwaitingConsent, TaskState::Queued),
            (TaskState::AwaitingConsent, TaskState::Running),
            (TaskState::AwaitingConsent, TaskState::Pausing),
            (TaskState::AwaitingConsent, TaskState::Cancelling),
            (TaskState::Queued, TaskState::Running),
            (TaskState::Queued, TaskState::Cancelling),
            (TaskState::Running, TaskState::AwaitingConsent),
            (TaskState::Running, TaskState::WaitingUser),
            (TaskState::Running, TaskState::Pausing),
            (TaskState::Running, TaskState::Cancelling),
            (TaskState::Running, TaskState::Completing),
            (TaskState::Running, TaskState::Failed),
            (TaskState::WaitingUser, TaskState::Running),
            (TaskState::WaitingUser, TaskState::Pausing),
            (TaskState::WaitingUser, TaskState::Cancelling),
            (TaskState::Pausing, TaskState::Paused),
            (TaskState::Paused, TaskState::Queued),
            (TaskState::Paused, TaskState::Cancelling),
            (TaskState::Cancelling, TaskState::Cancelled),
            (TaskState::Completing, TaskState::Completed),
            (TaskState::Completing, TaskState::Partial),
            (TaskState::Completing, TaskState::Running),
            (TaskState::Completing, TaskState::Cancelling),
            (TaskState::Completed, TaskState::Running),
            (TaskState::Partial, TaskState::Running),
        ];
        for state in TaskState::ALL {
            for command in CommandKind::ALL {
                for target in disposition(*state, *command).targets() {
                    assert!(
                        EDGES.contains(&(*state, *target)),
                        "{} x {} invents the edge {} to {}",
                        state.label(),
                        command.label(),
                        state.label(),
                        target.label()
                    );
                }
            }
        }
    }

    #[test]
    fn every_edge_the_diagram_draws_has_a_command_that_reaches_it() {
        for (from, to) in [
            (TaskState::Draft, TaskState::AwaitingConsent),
            (TaskState::Draft, TaskState::Cancelled),
            (TaskState::AwaitingConsent, TaskState::Draft),
            (TaskState::AwaitingConsent, TaskState::Queued),
            (TaskState::AwaitingConsent, TaskState::Running),
            (TaskState::AwaitingConsent, TaskState::Pausing),
            (TaskState::AwaitingConsent, TaskState::Cancelling),
            (TaskState::Queued, TaskState::Running),
            (TaskState::Queued, TaskState::Cancelling),
            (TaskState::Running, TaskState::AwaitingConsent),
            (TaskState::Running, TaskState::WaitingUser),
            (TaskState::Running, TaskState::Pausing),
            (TaskState::Running, TaskState::Cancelling),
            (TaskState::Running, TaskState::Completing),
            (TaskState::Running, TaskState::Failed),
            (TaskState::WaitingUser, TaskState::Running),
            (TaskState::WaitingUser, TaskState::Pausing),
            (TaskState::WaitingUser, TaskState::Cancelling),
            (TaskState::Pausing, TaskState::Paused),
            (TaskState::Paused, TaskState::Queued),
            (TaskState::Paused, TaskState::Cancelling),
            (TaskState::Cancelling, TaskState::Cancelled),
            (TaskState::Completing, TaskState::Completed),
            (TaskState::Completing, TaskState::Partial),
            (TaskState::Completing, TaskState::Running),
            (TaskState::Completing, TaskState::Cancelling),
        ] {
            assert!(
                CommandKind::ALL
                    .iter()
                    .any(|command| disposition(from, *command).targets().contains(&to)),
                "no command reaches {} from {}",
                to.label(),
                from.label()
            );
        }
    }

    #[test]
    fn every_guard_is_used_and_every_guard_has_its_own_refusal() {
        let mut used: Vec<Guard> = Vec::new();
        for state in TaskState::ALL {
            for command in CommandKind::ALL {
                for guard in disposition(*state, *command).guards() {
                    if !used.contains(guard) {
                        used.push(*guard);
                    }
                }
            }
        }
        for guard in Guard::ALL {
            assert!(used.contains(guard), "{} is never checked", guard.label());
        }
        let mut refusals: Vec<RefusalReason> = Vec::new();
        for guard in Guard::ALL {
            assert!(
                !refusals.contains(&guard.refusal()),
                "{} shares a refusal",
                guard.label()
            );
            refusals.push(guard.refusal());
        }
    }

    #[test]
    fn every_refusal_reason_has_a_distinct_compiled_in_name() {
        let mut seen: Vec<&str> = Vec::new();
        for reason in RefusalReason::ALL {
            assert!(!seen.contains(&reason.label()), "{}", reason.label());
            seen.push(reason.label());
        }
    }
}
