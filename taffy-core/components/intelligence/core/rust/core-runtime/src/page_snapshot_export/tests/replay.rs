// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;

#[test]
fn exact_replay_is_cached_and_changed_replay_is_refused() {
    let mut exporter = PageSnapshotExporter::new();
    let original = command(wire::PageSnapshotExportFormat::CanonicalJson);
    let first = exporter.export(&original, &ReferenceSha256);
    let mut replay_command = original.clone();
    replay_command.operation.operation_id = "page-export-retry-2".to_owned();
    replay_command.operation.deadline_monotonic_ms = 8_000;
    replay_command.captured_at_epoch_ms = 1_725_000_009_999;
    let replay = exporter.export(&replay_command, &ReferenceSha256);
    assert_eq!(replay.operation, replay_command.operation);
    assert_eq!(replay.content, first.content);
    assert_eq!(replay.captured_at_epoch_ms, first.captured_at_epoch_ms);

    let mut changed = replay_command.clone();
    changed.operation.operation_id = "page-export-retry-3".to_owned();
    changed.expected_graph_revision = 8;
    let conflict = exporter.export(&changed, &ReferenceSha256);
    assert_eq!(
        conflict.status,
        wire::PageSnapshotExportStatus::ReplayConflict
    );
    assert!(conflict.content.is_empty());

    let mut changed_observation = original.clone();
    changed_observation.operation.operation_id = "page-export-retry-4".to_owned();
    changed_observation.observation.private_profile = true;
    let conflict = exporter.export(&changed_observation, &ReferenceSha256);
    assert_eq!(
        conflict.status,
        wire::PageSnapshotExportStatus::ReplayConflict
    );
    assert!(conflict.content.is_empty());
}

#[test]
fn logical_replay_binds_every_immutable_export_input() {
    let mut exporter = PageSnapshotExporter::new();
    let original = command(wire::PageSnapshotExportFormat::Markdown);
    let initial = exporter.export(&original, &ReferenceSha256);
    assert_eq!(initial.status, wire::PageSnapshotExportStatus::Exported);

    assert_replay_conflict(&mut exporter, &original, "format", |value| {
        value.format = wire::PageSnapshotExportFormat::CanonicalJson;
    });
    assert_replay_conflict(&mut exporter, &original, "origin", |value| {
        value.expected_origin = "https://changed.test".to_owned();
    });
    assert_replay_conflict(&mut exporter, &original, "query", |value| {
        value.source_query_withheld = false;
    });
    assert_replay_conflict(&mut exporter, &original, "security", |value| {
        value.observation.is_potentially_trustworthy = false;
    });
    assert_replay_conflict(&mut exporter, &original, "graph", |value| {
        if let Some(first) = value.observation.graph_payload.first_mut() {
            *first ^= 0xff;
        }
    });
}

#[test]
fn a_distinct_logical_identity_exports_independently() {
    let mut exporter = PageSnapshotExporter::new();
    let first_command = command(wire::PageSnapshotExportFormat::Markdown);
    let first = exporter.export(&first_command, &ReferenceSha256);

    let mut independent = first_command.clone();
    independent.operation.operation_id = "page-export-independent".to_owned();
    independent.operation.idempotency_key = "page-export-key-independent".to_owned();
    independent.captured_at_epoch_ms = first_command.captured_at_epoch_ms + 10;
    let second = exporter.export(&independent, &ReferenceSha256);

    assert_eq!(second.status, wire::PageSnapshotExportStatus::Exported);
    assert_eq!(second.operation, independent.operation);
    assert_eq!(
        second.captured_at_epoch_ms,
        independent.captured_at_epoch_ms
    );
    assert_ne!(second.content, first.content);
}

#[test]
fn bounded_replay_window_does_not_disable_later_exports() {
    let mut exporter = PageSnapshotExporter::new();
    for sequence in 0..=MAX_REPLAY_ENTRIES {
        let mut next = command(wire::PageSnapshotExportFormat::Markdown);
        next.operation.operation_id = format!("page-export-{sequence}");
        next.operation.idempotency_key = format!("page-export-key-{sequence}");
        let result = exporter.export(&next, &ReferenceSha256);
        assert_eq!(result.status, wire::PageSnapshotExportStatus::Exported);
    }
    assert_eq!(exporter.completed.len(), MAX_REPLAY_ENTRIES);
    assert_eq!(exporter.completion_order.len(), MAX_REPLAY_ENTRIES);
    assert!(!exporter.completed.contains_key("page-export-key-0"));
    assert!(exporter
        .completed
        .contains_key(&format!("page-export-key-{MAX_REPLAY_ENTRIES}")));
}

#[test]
fn cancellation_is_exact_terminal_and_content_free() {
    let mut exporter = PageSnapshotExporter::new();
    assert!(exporter.cancel("page-export-1", "page-export-key-1"));
    assert!(!exporter.cancel("page-export-1", "page-export-key-1"));
    let command = command(wire::PageSnapshotExportFormat::Markdown);
    let cancelled = exporter.export(&command, &ReferenceSha256);
    assert_eq!(cancelled.status, wire::PageSnapshotExportStatus::Cancelled);
    assert!(cancelled.content.is_empty());
    assert!(!exporter.completed.contains_key("page-export-key-1"));

    let repeated = exporter.export(&command, &ReferenceSha256);
    assert_eq!(repeated.status, wire::PageSnapshotExportStatus::Cancelled);
    assert!(repeated.content.is_empty());

    let mut retry = command.clone();
    retry.operation.operation_id = "page-export-2".to_owned();
    retry.operation.deadline_monotonic_ms = 7_000;
    retry.captured_at_epoch_ms += 50;
    let retry_result = exporter.export(&retry, &ReferenceSha256);
    assert_eq!(
        retry_result.status,
        wire::PageSnapshotExportStatus::Exported
    );
    assert_eq!(retry_result.operation, retry.operation);
    assert_eq!(
        retry_result.captured_at_epoch_ms,
        retry.captured_at_epoch_ms
    );

    let cancelled_again = exporter.export(&command, &ReferenceSha256);
    assert_eq!(
        cancelled_again.status,
        wire::PageSnapshotExportStatus::Cancelled
    );
}

#[test]
fn cancellation_identity_and_window_are_bounded() {
    let mut exporter = PageSnapshotExporter::new();
    assert!(exporter.cancel("page-export-bound", "page-export-key-bound"));
    let mut mismatched = command(wire::PageSnapshotExportFormat::Markdown);
    mismatched.operation.operation_id = "page-export-bound".to_owned();
    mismatched.operation.idempotency_key = "page-export-key-other".to_owned();
    let conflict = exporter.export(&mismatched, &ReferenceSha256);
    assert_eq!(
        conflict.status,
        wire::PageSnapshotExportStatus::ReplayConflict
    );
    assert!(conflict.content.is_empty());

    for sequence in 0..=MAX_CANCELLED_ENTRIES {
        assert!(exporter.cancel(
            &format!("page-export-cancel-{sequence}"),
            &format!("page-export-cancel-key-{sequence}"),
        ));
    }
    assert_eq!(exporter.cancelled.len(), MAX_CANCELLED_ENTRIES);
    assert_eq!(exporter.cancellation_order.len(), MAX_CANCELLED_ENTRIES);
    assert!(!exporter.cancelled.contains_key("page-export-bound"));
}

#[test]
fn a_new_generation_exporter_has_no_prior_replay_state() {
    let mut first_generation = PageSnapshotExporter::new();
    let original = command(wire::PageSnapshotExportFormat::Markdown);
    let first = first_generation.export(&original, &ReferenceSha256);

    let mut next_generation = PageSnapshotExporter::new();
    let mut restored = original.clone();
    restored.operation.operation_id = "page-export-generation-10".to_owned();
    restored.operation.service_generation = 10;
    restored.captured_at_epoch_ms += 1_000;
    let next = next_generation.export(&restored, &ReferenceSha256);

    assert_eq!(next.status, wire::PageSnapshotExportStatus::Exported);
    assert_eq!(next.operation, restored.operation);
    assert_eq!(next.captured_at_epoch_ms, restored.captured_at_epoch_ms);
    assert_ne!(next.content, first.content);
}

fn assert_replay_conflict(
    exporter: &mut PageSnapshotExporter,
    original: &wire::PageSnapshotExportCommand,
    suffix: &str,
    change: impl FnOnce(&mut wire::PageSnapshotExportCommand),
) {
    let mut changed = original.clone();
    changed.operation.operation_id = format!("page-export-changed-{suffix}");
    change(&mut changed);
    let conflict = exporter.export(&changed, &ReferenceSha256);
    assert_eq!(
        conflict.status,
        wire::PageSnapshotExportStatus::ReplayConflict
    );
    assert_eq!(conflict.operation, changed.operation);
    assert!(conflict.content.is_empty());
    assert!(conflict.origin.is_empty());
    assert_eq!(conflict.captured_at_epoch_ms, 0);
}
