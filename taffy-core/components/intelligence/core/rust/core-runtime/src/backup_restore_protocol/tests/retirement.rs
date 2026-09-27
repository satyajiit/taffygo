// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

#[test]
fn lost_cancellation_reply_can_be_acknowledged_by_a_fresh_exact_decision() {
    let mut restore = protocol(false);
    let planned = restore.plan_restore(restore_request("candidate-a", NOW + 30), NOW, &TestDigest);
    let binding = restore.active_binding().expect("binding").clone();
    restore
        .cancel_before_commit(&operation("cancel", NOW + 30), &binding, NOW + 1)
        .expect("cancellation whose reply may be lost");
    assert_eq!(restore.active_binding(), None);
    assert_eq!(
        restore.cancel_before_commit(&operation("retry", NOW + 90), &binding, NOW + 31),
        Ok(())
    );
    assert_eq!(restore.active_binding(), None);
    assert_eq!(
        restore.confirm_plan(
            operation("no-restored-authority", NOW + 90),
            &binding,
            planned.confirmation_sha256,
            NOW + 32,
            &TestDigest,
        ),
        Err(BackupRestoreProtocolError::Unavailable)
    );
}

#[test]
fn cancellation_receipt_requires_exact_binding_live_decision_and_same_incarnation() {
    let mut restore = protocol(false);
    restore.plan_restore(restore_request("candidate-a", NOW + 30), NOW, &TestDigest);
    let binding = restore.active_binding().expect("binding").clone();
    restore
        .cancel_before_commit(&operation("cancel", NOW + 30), &binding, NOW + 1)
        .expect("cancelled");

    let mut other = binding.clone();
    other.confirmation_sha256[0] ^= 1;
    assert_eq!(
        restore.cancel_before_commit(&operation("spliced", NOW + 90), &other, NOW + 31),
        Err(BackupRestoreProtocolError::Unavailable)
    );
    assert_eq!(
        restore.cancel_before_commit(&operation("expired", NOW + 30), &binding, NOW + 31),
        Err(BackupRestoreProtocolError::InvalidOperation)
    );
    let mut stale = operation("stale", NOW + 90);
    stale.service_generation += 1;
    assert_eq!(
        restore.cancel_before_commit(&stale, &binding, NOW + 31),
        Err(BackupRestoreProtocolError::InvalidOperation)
    );
    restore.service_disconnected().expect("disconnect");
    assert_eq!(
        restore.cancel_before_commit(&operation("disconnected", NOW + 90), &binding, NOW + 31),
        Err(BackupRestoreProtocolError::Unavailable)
    );
    let mut replacement = protocol(false);
    assert_eq!(
        replacement.cancel_before_commit(&operation("replacement", NOW + 90), &binding, NOW + 31),
        Err(BackupRestoreProtocolError::Unavailable)
    );
}

#[test]
fn acknowledging_a_cancelled_plan_cannot_cancel_another_active_plan() {
    let mut restore = protocol(false);
    restore.plan_restore(restore_request("candidate-a", NOW + 30), NOW, &TestDigest);
    let first = restore.active_binding().expect("first").clone();
    restore
        .cancel_before_commit(&operation("first-cancel", NOW + 30), &first, NOW + 1)
        .expect("first cancelled");

    let second_plan = restore.plan_restore(
        restore_request("candidate-b", NOW + 90),
        NOW + 2,
        &TestDigest,
    );
    assert_eq!(second_plan.status, wire::BackupPlanningStatus::Succeeded);
    let second = restore.active_binding().expect("second").clone();
    assert_eq!(
        restore.cancel_before_commit(&operation("first-retry", NOW + 90), &first, NOW + 3),
        Ok(())
    );
    assert_eq!(restore.active_binding(), Some(&second));
    assert_eq!(
        restore.phase(),
        Some(RestoreSessionPhase::AwaitingPlanConfirmation)
    );
    restore
        .cancel_before_commit(&operation("second-cancel", NOW + 90), &second, NOW + 4)
        .expect("second cancelled");
    assert_eq!(restore.active_binding(), None);
    assert_eq!(
        restore.cancel_before_commit(&operation("forgotten", NOW + 90), &first, NOW + 5),
        Err(BackupRestoreProtocolError::Unavailable)
    );
    assert_eq!(
        restore.cancel_before_commit(&operation("second-retry", NOW + 90), &second, NOW + 5),
        Ok(())
    );
}

#[test]
fn the_exact_cancelled_planning_request_cannot_reactivate_its_identity() {
    let mut restore = protocol(false);
    let request = restore_request("candidate-a", NOW + 90);
    let first = restore.plan_restore(request.clone(), NOW, &TestDigest);
    let binding = restore.active_binding().expect("binding").clone();
    restore
        .cancel_before_commit(&operation("cancel", NOW + 90), &binding, NOW + 1)
        .expect("cancelled");
    let planned = restore.plan_restore(request, NOW + 2, &TestDigest);
    assert_eq!(planned.status, wire::BackupPlanningStatus::InvalidRequest);
    assert_eq!(planned.binding, None);
    assert_eq!(restore.active_binding(), None);
    assert_eq!(
        restore.confirm_plan(
            operation("confirm", NOW + 90),
            &binding,
            first.confirmation_sha256,
            NOW + 3,
            &TestDigest,
        ),
        Err(BackupRestoreProtocolError::Unavailable)
    );
    assert_eq!(
        restore.cancel_before_commit(&operation("still-a-receipt", NOW + 90), &binding, NOW + 5),
        Ok(())
    );
    assert_eq!(restore.phase(), None);
}
