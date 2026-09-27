// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{
    decode_manifest, encode_manifest, BackupError, BackupManifest, BackupRecord, BackupRecordKind,
    BackupRecordState, BackupSelection, MANIFEST_FORMAT_VERSION,
};

fn manifest() -> Result<BackupManifest, BackupError> {
    Ok(BackupManifest {
        backup_id: "backup-01".to_owned(),
        source_installation_id: "installation-01".to_owned(),
        created_at_utc: "2026-09-04T18:00:00Z".to_owned(),
        selection: BackupSelection::new([
            BackupRecordKind::SavedWorkspace,
            BackupRecordKind::MemoryRecord,
        ])?,
        records: vec![
            BackupRecord {
                kind: BackupRecordKind::SavedWorkspace,
                stable_id: "workspace-01".to_owned(),
                revision: 7,
                schema_version: 3,
                state: BackupRecordState::Active,
                plaintext_bytes: 4_096,
                plaintext_sha256: [4; 32],
            },
            BackupRecord {
                kind: BackupRecordKind::MemoryRecord,
                stable_id: "memory-02".to_owned(),
                revision: 9,
                schema_version: 2,
                state: BackupRecordState::Tombstone,
                plaintext_bytes: 0,
                plaintext_sha256: [0; 32],
            },
        ],
        snapshot_sha256: [9; 32],
    })
}

fn first_record_offset(manifest: &BackupManifest) -> usize {
    4 + 2
        + 2
        + 2
        + manifest.backup_id.len()
        + 2
        + manifest.source_installation_id.len()
        + 2
        + manifest.created_at_utc.len()
        + 4
        + 32
}

fn byte_mut(bytes: &mut [u8], offset: usize) -> Result<&mut u8, BackupError> {
    bytes.get_mut(offset).ok_or(BackupError::MalformedManifest)
}

#[test]
fn canonical_manifest_round_trips_with_exact_record_order() -> Result<(), BackupError> {
    let source = manifest()?;
    let encoded = encode_manifest(&source)?;
    let decoded = decode_manifest(&encoded)?;
    assert_eq!(decoded, source);
    assert_eq!(encode_manifest(&decoded)?, encoded);
    let version_bytes: [u8; 2] = encoded
        .get(4..6)
        .ok_or(BackupError::MalformedManifest)?
        .try_into()
        .map_err(|_| BackupError::MalformedManifest)?;
    assert_eq!(u16::from_le_bytes(version_bytes), MANIFEST_FORMAT_VERSION);
    Ok(())
}

#[test]
fn every_truncated_prefix_and_trailing_byte_is_refused() -> Result<(), BackupError> {
    let encoded = encode_manifest(&manifest()?)?;
    for length in 0..encoded.len() {
        let prefix = encoded
            .get(..length)
            .ok_or(BackupError::MalformedManifest)?;
        assert!(decode_manifest(prefix).is_err(), "accepted {length} bytes");
    }
    let mut trailing = encoded;
    trailing.push(0);
    assert_eq!(
        decode_manifest(&trailing),
        Err(BackupError::TrailingManifestData)
    );
    Ok(())
}

#[test]
fn unknown_selection_kind_state_and_reserved_bits_fail_closed() -> Result<(), BackupError> {
    let source = manifest()?;
    let encoded = encode_manifest(&source)?;
    let record_offset = first_record_offset(&source);

    let mut unknown_selection = encoded.clone();
    *byte_mut(&mut unknown_selection, 7)? |= 0x80;
    assert_eq!(
        decode_manifest(&unknown_selection),
        Err(BackupError::MalformedManifest)
    );

    let mut unknown_kind = encoded.clone();
    *byte_mut(&mut unknown_kind, record_offset)? = 8;
    assert_eq!(
        decode_manifest(&unknown_kind),
        Err(BackupError::UnknownRecordKind(8))
    );
    let mut unknown_state = encoded.clone();
    *byte_mut(&mut unknown_state, record_offset + 1)? = 2;
    assert_eq!(
        decode_manifest(&unknown_state),
        Err(BackupError::UnknownRecordState(2))
    );
    let mut reserved = encoded;
    *byte_mut(&mut reserved, record_offset + 2)? = 1;
    assert_eq!(
        decode_manifest(&reserved),
        Err(BackupError::MalformedManifest)
    );
    Ok(())
}

#[test]
fn invalid_utf8_and_zero_digests_are_not_canonical() -> Result<(), BackupError> {
    let source = manifest()?;
    let mut encoded = encode_manifest(&source)?;
    *byte_mut(&mut encoded, 10)? = 0xff;
    assert_eq!(
        decode_manifest(&encoded),
        Err(BackupError::MalformedManifest)
    );

    let mut invalid_snapshot = source.clone();
    invalid_snapshot.snapshot_sha256 = [0; 32];
    assert_eq!(
        encode_manifest(&invalid_snapshot),
        Err(BackupError::InvalidDigest)
    );
    let mut invalid_tombstone = source;
    invalid_tombstone
        .records
        .get_mut(1)
        .ok_or(BackupError::MalformedManifest)?
        .plaintext_sha256 = [1; 32];
    assert_eq!(
        encode_manifest(&invalid_tombstone),
        Err(BackupError::InvalidDigest)
    );
    Ok(())
}
