// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{Command, CommandKind, PauseCause};

#[test]
fn every_command_kind_has_a_distinct_compiled_in_name() {
    let mut seen: Vec<&str> = Vec::new();
    for kind in CommandKind::ALL {
        assert!(!seen.contains(&kind.label()), "{} repeated", kind.label());
        seen.push(kind.label());
    }
    assert_eq!(seen.len(), CommandKind::ALL.len());
}

#[test]
fn the_thirteen_user_commands_are_the_ones_the_specification_names() {
    let user: Vec<&str> = CommandKind::ALL
        .iter()
        .filter(|kind| kind.is_user_command())
        .map(|kind| kind.label())
        .collect();
    assert_eq!(
        user,
        vec![
            "CreateTask",
            "StartTask",
            "ApproveAction",
            "DenyAction",
            "PauseTask",
            "TakeOver",
            "ResumeTask",
            "CancelTask",
            "FollowUp",
            "CorrectFact",
            "ExcludeSource",
            "AcceptArtifact",
            "ExportArtifact",
        ]
    );
}

#[test]
fn a_command_reports_its_own_discriminant() {
    assert_eq!(Command::CreateTask.kind(), CommandKind::CreateTask);
    assert_eq!(
        Command::PauseTask {
            cause: PauseCause::User
        }
        .kind(),
        CommandKind::PauseTask
    );
}
