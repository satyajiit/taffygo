// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

#[test]
fn spliced_stale_and_mismatched_reports_fail_closed() {
    let mut first = protocol(false);
    let first_plan = first.plan_restore(restore_request("candidate-a", NOW + 10), NOW, &TestDigest);
    let first_binding = first.active_binding().expect("first binding").clone();
    let first_stage = first
        .confirm_plan(
            operation("first-confirm", NOW + 20),
            &first_binding,
            first_plan.confirmation_sha256,
            NOW + 1,
            &TestDigest,
        )
        .expect("first stage");

    let mut second = protocol(false);
    let second_plan =
        second.plan_restore(restore_request("candidate-b", NOW + 20), NOW, &TestDigest);
    let second_binding = second.active_binding().expect("second binding").clone();
    let second_stage = second
        .confirm_plan(
            operation("second-confirm", NOW + 20),
            &second_binding,
            second_plan.confirmation_sha256,
            NOW + 1,
            &TestDigest,
        )
        .expect("second stage");
    assert_eq!(
        first.report_stage_verified(
            operation("spliced", NOW + 20),
            &second_stage,
            first_plan.snapshot_sha256,
            Vec::new(),
            NOW + 2,
        ),
        Err(BackupRestoreProtocolError::BindingMismatch)
    );
    assert_eq!(
        first.report_stage_verified(
            operation("wrong-digest", NOW + 20),
            &first_stage,
            [99; 32],
            Vec::new(),
            NOW + 2,
        ),
        Err(BackupRestoreProtocolError::SnapshotMismatch)
    );
    assert_eq!(first.active_binding(), None);

    assert_eq!(
        second.confirm_plan(
            operation("expired-decision", NOW + 20),
            &second_binding,
            second_plan.confirmation_sha256,
            NOW + 20,
            &TestDigest,
        ),
        Err(BackupRestoreProtocolError::InvalidOperation)
    );
    assert_eq!(second.phase(), Some(RestoreSessionPhase::Staging));

    let mut disconnected = protocol(false);
    let disconnected_plan =
        disconnected.plan_restore(restore_request("candidate-c", NOW + 20), NOW, &TestDigest);
    let disconnected_binding = disconnected
        .active_binding()
        .expect("disconnected binding")
        .clone();
    disconnected
        .service_disconnected()
        .expect("pre-commit disconnect cancels");
    assert_eq!(disconnected.active_binding(), None);
    assert_eq!(
        disconnected
            .plan_restore(
                restore_request("candidate-d", NOW + 30),
                NOW + 1,
                &TestDigest,
            )
            .status,
        wire::BackupPlanningStatus::Unavailable
    );
    assert_eq!(
        disconnected.confirm_plan(
            operation("after-disconnect", NOW + 30),
            &disconnected_binding,
            disconnected_plan.confirmation_sha256,
            NOW + 1,
            &TestDigest,
        ),
        Err(BackupRestoreProtocolError::Unavailable)
    );
}

#[test]
fn plan_rpc_expiry_does_not_expire_review_or_hidden_staging() {
    let mut restore = protocol(false);
    let planned = restore.plan_restore(restore_request("candidate-a", NOW + 30), NOW, &TestDigest);
    let binding = restore.active_binding().expect("binding").clone();

    let stage = restore
        .confirm_plan(
            operation("confirm-after-review", NOW + 90),
            &binding,
            planned.confirmation_sha256,
            NOW + 31,
            &TestDigest,
        )
        .expect("fresh decision after review");
    let commit = restore
        .report_stage_verified(
            operation("commit-after-stage", NOW + 120),
            &stage,
            planned.snapshot_sha256,
            Vec::new(),
            NOW + 91,
        )
        .expect("fresh commit decision after hidden staging");

    assert_eq!(commit.binding(), &binding);
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::CommitIssued));
}

#[test]
fn exact_cancel_blocks_a_late_stage_completion() {
    let mut restore = protocol(false);
    let planned = restore.plan_restore(restore_request("candidate-a", NOW + 30), NOW, &TestDigest);
    let binding = restore.active_binding().expect("binding").clone();
    let stage = restore
        .confirm_plan(
            operation("confirm", NOW + 60),
            &binding,
            planned.confirmation_sha256,
            NOW + 1,
            &TestDigest,
        )
        .expect("stage authority");

    restore
        .cancel_before_commit(&operation("cancel", NOW + 90), &binding, NOW + 31)
        .expect("exact precommit cancellation");
    assert_eq!(restore.active_binding(), None);
    assert_eq!(
        restore.report_stage_verified(
            operation("late-stage", NOW + 120),
            &stage,
            planned.snapshot_sha256,
            Vec::new(),
            NOW + 32,
        ),
        Err(BackupRestoreProtocolError::Unavailable)
    );
}

#[test]
fn disconnect_after_commit_issuance_retires_all_decision_methods() {
    let mut restore = protocol(false);
    let planned = restore.plan_restore(restore_request("candidate-a", NOW + 50), NOW, &TestDigest);
    let binding = restore.active_binding().expect("binding").clone();
    let stage = restore
        .confirm_plan(
            operation("confirm", NOW + 50),
            &binding,
            planned.confirmation_sha256,
            NOW + 1,
            &TestDigest,
        )
        .expect("stage");
    let commit = restore
        .report_stage_verified(
            operation("commit", NOW + 50),
            &stage,
            planned.snapshot_sha256,
            Vec::new(),
            NOW + 2,
        )
        .expect("commit");
    assert_eq!(
        restore.service_disconnected(),
        Err(BackupRestoreProtocolError::ReconcileRequired)
    );
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::CommitIssued));
    assert_eq!(
        restore.resolve_commit(
            &operation("resolve-after-disconnect", NOW + 50),
            &commit,
            BackupRestoreCommitOutcome::Committed,
            NOW + 3,
        ),
        Err(BackupRestoreProtocolError::Unavailable)
    );
    assert_eq!(
        restore.choose_resolution(
            operation("publish-after-disconnect", NOW + 50),
            &binding,
            BackupRestoreResolutionChoice::AcceptCandidate,
            NOW + 3,
        ),
        Err(BackupRestoreProtocolError::Unavailable)
    );
    assert_eq!(
        restore.report_stage_verified(
            operation("stage-after-disconnect", NOW + 50),
            &stage,
            planned.snapshot_sha256,
            Vec::new(),
            NOW + 3,
        ),
        Err(BackupRestoreProtocolError::Unavailable)
    );
}
