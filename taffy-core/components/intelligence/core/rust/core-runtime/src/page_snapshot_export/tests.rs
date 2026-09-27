// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::PROTOCOL_VERSION;
use std::cell::Cell;

use super::*;
use crate::account::crypto::ReferenceSha256;

#[cfg(test)]
mod replay;

#[derive(Debug, Default)]
struct CountingDigest {
    calls: Cell<u32>,
}

impl Sha256Port for CountingDigest {
    fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], crate::DigestError> {
        self.calls.set(self.calls.get().saturating_add(1));
        Ok([0u8; 32])
    }
}

fn u16(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn u64(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn short(out: &mut Vec<u8>, value: &str) {
    u16(out, u16::try_from(value.len()).unwrap());
    out.extend_from_slice(value.as_bytes());
}

fn public_node(out: &mut Vec<u8>) {
    short(out, "raw-public-node-id");
    short(out, "frame-1");
    u16(out, 2); // HEADING
    out.push(0); // NOT_SENSITIVE
    out.push(0); // no withholding
    out.push(9); // UNKNOWN value kind
    short(out, "Public *heading*");
    u32(out, 1);
    u64(out, 9);
    u32(out, 1);
    u16(out, 1); // FOCUS
    u32(out, 1);
    out.push(1); // NOT_VISIBLE
    out.push(0); // no destination
    out.push(2); // FIRST_PARTY_DOCUMENT
    u32(out, 1);
    out.push(0); // HIDDEN_BY_STYLE
    u32(out, 1);
    short(out, "Safe text");
    out.push(0); // RENDERED_TEXT
    out.push(0); // NOT_SENSITIVE
    out.push(0);
    out.push(3); // USER_GENERATED_CONTENT
    u32(out, 1);
    out.push(0); // HIDDEN_BY_STYLE
}

fn withheld_node(out: &mut Vec<u8>) {
    short(out, "SECRET-CANARY-RAW-NODE-ID");
    short(out, "frame-1");
    u16(out, 12); // TEXT_FIELD
    out.push(10); // CREDENTIAL
    out.push(0x29); // name, secret value, and text withheld
    out.push(8); // SECRET_WITHHELD value kind
    short(out, "");
    u32(out, 1);
    u64(out, 64);
    u32(out, 0);
    u32(out, 0);
    out.push(0);
    out.push(2); // FIRST_PARTY_DOCUMENT
    u32(out, 0);
    u32(out, 0); // no text bytes may cross
}

fn graph() -> Vec<u8> {
    let mut out = vec![4];
    short(&mut out, PROTOCOL_VERSION);
    u32(&mut out, 2);
    public_node(&mut out);
    withheld_node(&mut out);
    u32(&mut out, 0); // no relationships
    out
}

fn command(format: wire::PageSnapshotExportFormat) -> wire::PageSnapshotExportCommand {
    let payload = graph();
    wire::PageSnapshotExportCommand {
        operation: wire::OperationEnvelope {
            operation_id: "page-export-1".to_owned(),
            service_generation: 9,
            task_revision: 0,
            deadline_monotonic_ms: 5_000,
            idempotency_key: "page-export-key-1".to_owned(),
        },
        format,
        expected_tab_id: "tab-1".to_owned(),
        expected_frame_id: "frame-1".to_owned(),
        expected_page_epoch: "epoch-1".to_owned(),
        expected_graph_revision: 7,
        expected_origin: "https://example.test".to_owned(),
        max_bytes: u32::try_from(wire::MAX_PAGE_SNAPSHOT_EXPORT_BYTES).unwrap(),
        observation: wire::ObservationEffectResult {
            status: wire::BipObservationStatus::Ok,
            schema_version: PROTOCOL_VERSION.to_owned(),
            tab_id: "tab-1".to_owned(),
            frame_id: "frame-1".to_owned(),
            page_epoch: "epoch-1".to_owned(),
            graph_revision: 7,
            origin: "https://example.test".to_owned(),
            is_potentially_trustworthy: true,
            private_profile: false,
            node_count: 2,
            total_bytes: u32::try_from(payload.len()).unwrap(),
            truncated: false,
            may_change_answer: false,
            redacted_field_count: 3,
            suppressed_secret_value_count: 1,
            sensitive_zone_count: 1,
            policy_filtered_frame_count: 0,
            highest_sensitivity: wire::BipSensitivity::Credential,
            graph_encoding: wire::BipGraphEncoding::BipContract,
            graph_payload: payload,
            media: None,
        },
        captured_at_epoch_ms: 1_725_000_000_123,
        source_query_withheld: true,
        source_fragment_withheld: true,
    }
}

fn export(command: &wire::PageSnapshotExportCommand) -> wire::PageSnapshotExportResult {
    PageSnapshotExporter::new().export(command, &ReferenceSha256)
}

#[test]
fn markdown_is_deterministic_redacted_and_identity_free() {
    let first = export(&command(wire::PageSnapshotExportFormat::Markdown));
    let second = export(&command(wire::PageSnapshotExportFormat::Markdown));
    assert_eq!(first, second);
    assert_eq!(first.status, wire::PageSnapshotExportStatus::Exported);
    let text = String::from_utf8(first.content).unwrap();
    assert!(text.contains("Origin: https://example.test"));
    assert!(text.contains("URL disclosure: origin only"));
    assert!(text.contains("Captured at (Unix epoch ms): 1725000000123"));
    assert!(text.contains("Source query withheld: yes"));
    assert!(text.contains("Source fragment withheld: yes"));
    assert!(text.contains("Public \\*heading\\*"));
    assert!(text.contains("Content trust: `FIRST_PARTY_DOCUMENT`"));
    assert!(text.contains("Content signals: `HIDDEN_BY_STYLE`"));
    assert!(text.contains("States: `NOT_VISIBLE`"));
    assert!(text.contains("[content trust `USER_GENERATED_CONTENT`; signals `HIDDEN_BY_STYLE`]"));
    assert!(text.contains("[withheld]"));
    assert!(text.contains("Suppressed secret values: 1"));
    assert!(!text.contains("raw-public-node-id"));
    assert!(!text.contains("SECRET-CANARY"));
    assert!(!text.contains("epoch-1"));
}

#[test]
fn canonical_json_has_fixed_order_and_no_raw_identity() {
    let result = export(&command(wire::PageSnapshotExportFormat::CanonicalJson));
    assert_eq!(result.status, wire::PageSnapshotExportStatus::Exported);
    let text = String::from_utf8(result.content).unwrap();
    assert!(text.starts_with("{\"format\":\"taffy-page-snapshot-v1\",\"nodes\":["));
    assert!(text.contains("\"captured_at_epoch_ms\":1725000000123"));
    assert!(text.contains("\"source_fragment_withheld\":true"));
    assert!(text.contains("\"source_query_withheld\":true"));
    assert!(text.contains("\"source_url_disclosure\":\"origin_only\""));
    assert!(text.contains("\"display_id\":\"node-1\""));
    assert!(text.contains(
        "\"content_signals\":[\"HIDDEN_BY_STYLE\"],\"content_trust\":\"FIRST_PARTY_DOCUMENT\""
    ));
    assert!(text.contains(
        "\"run_metadata\":[{\"content_signals\":[\"HIDDEN_BY_STYLE\"],\"content_trust\":\"USER_GENERATED_CONTENT\"}]"
    ));
    assert!(text.contains("\"states\":[\"NOT_VISIBLE\"]"));
    assert!(text.contains("\"withheld\":true"));
    assert!(text.ends_with("}}"));
    assert!(!text.contains("raw-public-node-id"));
    assert!(!text.contains("SECRET-CANARY"));
    assert!(!text.contains("epoch-1"));
}

#[test]
fn stale_private_incomplete_oversize_and_malformed_fail_without_bytes() {
    let mut stale = command(wire::PageSnapshotExportFormat::Markdown);
    stale.expected_page_epoch = "other-epoch".to_owned();
    assert_failure(&stale, wire::PageSnapshotExportStatus::StalePage);

    let mut private = command(wire::PageSnapshotExportFormat::Markdown);
    private.observation.private_profile = true;
    assert_failure(&private, wire::PageSnapshotExportStatus::PrivateProfile);

    let mut incomplete = command(wire::PageSnapshotExportFormat::Markdown);
    incomplete.observation.status = wire::BipObservationStatus::Incomplete;
    incomplete.observation.truncated = true;
    assert_failure(&incomplete, wire::PageSnapshotExportStatus::Incomplete);

    let mut oversize = command(wire::PageSnapshotExportFormat::Markdown);
    oversize.max_bytes = 32;
    assert_failure(&oversize, wire::PageSnapshotExportStatus::Oversize);

    let mut malformed = command(wire::PageSnapshotExportFormat::Markdown);
    malformed.observation.graph_payload.push(0xff);
    malformed.observation.total_bytes =
        u32::try_from(malformed.observation.graph_payload.len()).unwrap();
    assert_failure(&malformed, wire::PageSnapshotExportStatus::Malformed);
}

#[test]
fn oversized_or_mismatched_graph_is_refused_before_hashing() {
    let digest = CountingDigest::default();
    let mut exporter = PageSnapshotExporter::new();
    let mut oversized = command(wire::PageSnapshotExportFormat::Markdown);
    oversized.observation.graph_payload =
        vec![0u8; wire::MAX_TASK_OBSERVATION_TOTAL_BYTES.saturating_add(1)];
    oversized.observation.total_bytes =
        u32::try_from(oversized.observation.graph_payload.len()).unwrap();
    assert_failure_with(&mut exporter, &oversized, &digest);

    let mut mismatched = command(wire::PageSnapshotExportFormat::Markdown);
    mismatched.observation.total_bytes = mismatched.observation.total_bytes.saturating_add(1);
    assert_failure_with(&mut exporter, &mismatched, &digest);

    let mut oversized_metadata = command(wire::PageSnapshotExportFormat::Markdown);
    oversized_metadata.observation.schema_version =
        "x".repeat(wire::MAX_IDENTIFIER_BYTES.saturating_add(1));
    assert_failure_with(&mut exporter, &oversized_metadata, &digest);
    assert_eq!(digest.calls.get(), 0);
}

fn assert_failure(
    command: &wire::PageSnapshotExportCommand,
    status: wire::PageSnapshotExportStatus,
) {
    let result = export(command);
    assert_eq!(result.status, status);
    assert!(result.content.is_empty());
    assert!(result.origin.is_empty());
    assert!(result.mime_type.is_empty());
}

fn assert_failure_with(
    exporter: &mut PageSnapshotExporter,
    command: &wire::PageSnapshotExportCommand,
    digest: &dyn Sha256Port,
) {
    let result = exporter.export(command, digest);
    assert_eq!(result.status, wire::PageSnapshotExportStatus::Malformed);
    assert!(result.content.is_empty());
}
