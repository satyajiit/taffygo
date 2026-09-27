// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    decode_snapshot, encode_snapshot, FactKind, WorkspaceFact, WorkspaceMediaEvidenceKind,
    WorkspaceMediaFactKind, WorkspaceMediaKind, WorkspaceMediaProvenance, WorkspacePhase,
    WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate,
};

fn id<T>(byte: u8, wrap: impl FnOnce([u8; 16]) -> T) -> T {
    wrap([byte; 16])
}

fn snapshot() -> WorkspaceSnapshot {
    let source_id = id(2, SourceId::from_bytes);
    WorkspaceSnapshot {
        workspace_id: id(1, WorkspaceId::from_bytes),
        revision: 3,
        display_name: "Evidence comparison".to_owned(),
        goal: "Compare the retained evidence".to_owned(),
        phase: WorkspacePhase::PartlyDone,
        saved: true,
        last_updated_epoch_ms: 40,
        template: WorkspaceTemplate::CompareProducts,
        sources: vec![WorkspaceSource {
            source_id,
            title: "Independent evidence".to_owned(),
            host: "evidence.example".to_owned(),
            canonical_locator: Some("https://evidence.example/report".to_owned()),
            read_at_epoch_ms: 30,
            excluded: false,
        }],
        facts: vec![WorkspaceFact {
            fact_id: id(3, FactId::from_bytes),
            field: "warranty".to_owned(),
            value: "two years".to_owned(),
            kind: FactKind::FromPage,
            sources: vec![source_id],
            correction: None,
            has_conflict: false,
            media_provenance: None,
        }],
    }
}

#[test]
fn snapshot_round_trip_is_byte_stable() {
    let value = snapshot();
    let encoded = encode_snapshot(&value).unwrap_or_default();
    assert_eq!(decode_snapshot(&encoded), Ok(value));
    let decoded = decode_snapshot(&encoded).unwrap_or_else(|_| snapshot());
    assert_eq!(encode_snapshot(&decoded), Ok(encoded));
}

#[test]
fn media_coordinates_round_trip_and_invalid_shapes_fail_closed() {
    let mut value = snapshot();
    let media = WorkspaceMediaProvenance {
        media_kind: WorkspaceMediaKind::Pdf,
        fact_kind: WorkspaceMediaFactKind::PdfTableRow,
        evidence_kind: WorkspaceMediaEvidenceKind::TableHeuristic,
        source_locator: "pdf:page/2".to_owned(),
        source_start: 10,
        source_end: 21,
        page_index_plus_one: 2,
        timestamp_start_ms: 0,
        timestamp_end_ms: 0,
        row_index_plus_one: 1,
        confidence_ppm: 650_000,
        truncated: false,
    };
    if let Some(fact) = value.facts.first_mut() {
        fact.field = "PDF table row".to_owned();
        fact.media_provenance = Some(media.clone());
    }
    let encoded = encode_snapshot(&value).unwrap_or_default();
    assert_eq!(decode_snapshot(&encoded), Ok(value.clone()));
    assert!(decode_snapshot(&encoded)
        .ok()
        .and_then(|snapshot| snapshot.facts.into_iter().next())
        .and_then(|fact| fact.media_provenance)
        .is_some_and(|stored| stored.location_descriptor()
            == "pdf:page/2; bytes 10..21; page 2; row 1; confidence 650000 ppm"));

    if let Some(fact) = value.facts.first_mut() {
        fact.media_provenance = Some(WorkspaceMediaProvenance {
            timestamp_end_ms: 30,
            ..media
        });
    }
    assert!(
        !value.validate(),
        "PDF rows cannot carry video time coordinates"
    );
}

#[test]
fn v1_snapshot_derives_a_display_name_defaults_saved_and_reencodes_as_v5() {
    let value = snapshot();
    let mut legacy = encode_snapshot(&value).unwrap_or_default();
    if let Some(version) = legacy.get_mut(8..12) {
        version.copy_from_slice(&1_u32.to_le_bytes());
    }
    remove_v5_media_absence(&mut legacy);
    remove_v4_source_locator(&mut legacy, &value);
    let name_offset = 8 + 4 + 4 + 32 + 8 + 4 + value.goal.len();
    let name_end = name_offset + 4 + value.display_name.len();
    if legacy.get(name_offset..name_end).is_some() {
        legacy.drain(name_offset..name_end);
    }
    if legacy.get(name_offset + 1).is_some() {
        legacy.remove(name_offset + 1);
    }
    let decoded = decode_snapshot(&legacy).unwrap_or_else(|_| snapshot());
    assert_eq!(decoded.display_name, "Compare the retained evidence");
    assert_eq!(decoded.goal, value.goal);
    assert!(decoded.saved);
    let reencoded = encode_snapshot(&decoded).unwrap_or_default();
    assert_ne!(reencoded, legacy);
    assert_eq!(decode_snapshot(&reencoded), Ok(decoded));
}

#[test]
fn v2_snapshot_defaults_to_saved_while_v5_round_trips_a_task_draft() {
    let mut value = snapshot();
    value.saved = false;
    let mut v2 = encode_snapshot(&value).unwrap_or_default();
    if let Some(version) = v2.get_mut(8..12) {
        version.copy_from_slice(&2_u32.to_le_bytes());
    }
    remove_v5_media_absence(&mut v2);
    remove_v4_source_locator(&mut v2, &value);
    let phase_offset = 8 + 4 + 4 + 32 + 8 + 4 + value.goal.len() + 4 + value.display_name.len();
    if v2.get(phase_offset + 1).is_some() {
        v2.remove(phase_offset + 1);
    }
    let decoded_v2 = decode_snapshot(&v2).unwrap_or_else(|_| snapshot());
    assert!(decoded_v2.saved);

    let encoded_draft = encode_snapshot(&value).unwrap_or_default();
    assert_eq!(decode_snapshot(&encoded_draft), Ok(value));
}

#[test]
fn v3_snapshot_is_readable_but_its_source_is_non_refreshable() {
    let value = snapshot();
    let mut v3 = encode_snapshot(&value).unwrap_or_default();
    if let Some(version) = v3.get_mut(8..12) {
        version.copy_from_slice(&3_u32.to_le_bytes());
    }
    remove_v5_media_absence(&mut v3);
    remove_v4_source_locator(&mut v3, &value);
    let decoded = decode_snapshot(&v3).unwrap_or_else(|_| snapshot());
    assert_eq!(
        decoded
            .sources
            .first()
            .and_then(|source| source.canonical_locator.as_ref()),
        None
    );
    assert!(decoded.saved);
}

#[test]
fn corruption_and_trailing_bytes_fail_closed() {
    let mut corrupt = encode_snapshot(&snapshot()).unwrap_or_default();
    if let Some(first) = corrupt.first_mut() {
        *first ^= 0xff;
    }
    assert!(decode_snapshot(&corrupt).is_err());

    let mut trailing = encode_snapshot(&snapshot()).unwrap_or_default();
    trailing.push(0);
    assert!(decode_snapshot(&trailing).is_err());
}

fn remove_v4_source_locator(bytes: &mut Vec<u8>, value: &WorkspaceSnapshot) {
    let source_offset = 8
        + 4
        + 4
        + 32
        + 8
        + 4
        + value.goal.len()
        + 4
        + value.display_name.len()
        + 1
        + 1
        + 8
        + 1
        + 4;
    let Some(source) = value.sources.first() else {
        return;
    };
    let locator_offset = source_offset
        + 4
        + source.source_id.to_text().len()
        + 4
        + source.title.len()
        + 4
        + source.host.len();
    let locator_bytes = source
        .canonical_locator
        .as_ref()
        .map_or(1, |locator| 1 + 4 + locator.len());
    if bytes
        .get(locator_offset..locator_offset + locator_bytes)
        .is_some()
    {
        bytes.drain(locator_offset..locator_offset + locator_bytes);
    }
}

fn remove_v5_media_absence(bytes: &mut Vec<u8>) {
    if bytes.last() == Some(&0) {
        bytes.pop();
    }
}

#[test]
fn v4_snapshot_defaults_media_provenance_to_absent() {
    let value = snapshot();
    let mut v4 = encode_snapshot(&value).unwrap_or_default();
    if let Some(version) = v4.get_mut(8..12) {
        version.copy_from_slice(&4_u32.to_le_bytes());
    }
    remove_v5_media_absence(&mut v4);
    let decoded = decode_snapshot(&v4).unwrap_or_else(|_| snapshot());
    assert!(decoded
        .facts
        .iter()
        .all(|fact| fact.media_provenance.is_none()));
    assert_eq!(
        decode_snapshot(&encode_snapshot(&decoded).unwrap_or_default()),
        Ok(decoded)
    );
}

#[test]
fn source_locator_rejects_query_fragment_credentials_and_host_mismatch() {
    for locator in [
        "https://evidence.example/report?token=secret",
        "https://evidence.example/report#private",
        "https://person:secret@evidence.example/report",
        "https://other.example/report",
        "file:///tmp/report",
    ] {
        let mut value = snapshot();
        if let Some(source) = value.sources.first_mut() {
            source.canonical_locator = Some(locator.to_owned());
        }
        assert!(
            !value.validate(),
            "locator unexpectedly admitted: {locator}"
        );
    }
}
