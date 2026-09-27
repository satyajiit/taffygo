// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical encrypted-manifest encoding for `.aib` version 1.
//!
//! Record boundaries exist only here, inside the encrypted manifest. Payload
//! bytes follow in this exact record order and are chunked independently of
//! those boundaries by the browser archive writer.

use super::{
    BackupError, BackupManifest, BackupRecord, BackupRecordKind, BackupRecordState,
    BackupSelection, MAX_BACKUP_MANIFEST_BYTES, MAX_BACKUP_RECORDS,
};

const MANIFEST_MAGIC: [u8; 4] = *b"AIBM";
const SNAPSHOT_MATERIAL_MAGIC: [u8; 4] = *b"AIBS";
pub const MANIFEST_FORMAT_VERSION: u16 = 1;
const FIXED_RECORD_BYTES: usize = 1 + 1 + 2 + 2 + 8 + 4 + 8 + 32;

pub fn encode_manifest(manifest: &BackupManifest) -> Result<Vec<u8>, BackupError> {
    manifest.validate()?;
    let record_count =
        u32::try_from(manifest.records.len()).map_err(|_| BackupError::TooManyRecords)?;
    let mut encoded = Vec::with_capacity(encoded_size(manifest, true)?);
    encoded.extend_from_slice(&MANIFEST_MAGIC);
    encoded.extend_from_slice(&MANIFEST_FORMAT_VERSION.to_le_bytes());
    encoded.extend_from_slice(&selection_mask(&manifest.selection).to_le_bytes());
    push_text(&mut encoded, &manifest.backup_id)?;
    push_text(&mut encoded, &manifest.source_installation_id)?;
    push_text(&mut encoded, &manifest.created_at_utc)?;
    encoded.extend_from_slice(&record_count.to_le_bytes());
    encoded.extend_from_slice(&manifest.snapshot_sha256);
    for record in &manifest.records {
        encode_record(&mut encoded, record)?;
    }
    if u64::try_from(encoded.len()).map_err(|_| BackupError::ArchiveTooLarge)?
        > MAX_BACKUP_MANIFEST_BYTES
    {
        return Err(BackupError::ArchiveTooLarge);
    }
    Ok(encoded)
}

pub(super) fn encode_snapshot_material(manifest: &BackupManifest) -> Result<Vec<u8>, BackupError> {
    let record_count =
        u32::try_from(manifest.records.len()).map_err(|_| BackupError::TooManyRecords)?;
    let mut encoded = Vec::with_capacity(encoded_size(manifest, false)?);
    encoded.extend_from_slice(&SNAPSHOT_MATERIAL_MAGIC);
    encoded.extend_from_slice(&MANIFEST_FORMAT_VERSION.to_le_bytes());
    encoded.extend_from_slice(&selection_mask(&manifest.selection).to_le_bytes());
    push_text(&mut encoded, &manifest.backup_id)?;
    push_text(&mut encoded, &manifest.source_installation_id)?;
    push_text(&mut encoded, &manifest.created_at_utc)?;
    encoded.extend_from_slice(&record_count.to_le_bytes());
    for record in &manifest.records {
        encode_record(&mut encoded, record)?;
    }
    if u64::try_from(encoded.len()).map_err(|_| BackupError::ArchiveTooLarge)?
        > MAX_BACKUP_MANIFEST_BYTES
    {
        return Err(BackupError::ArchiveTooLarge);
    }
    Ok(encoded)
}

fn encoded_size(manifest: &BackupManifest, includes_snapshot: bool) -> Result<usize, BackupError> {
    let mut bytes = 4_usize
        .checked_add(2 + 2 + 4)
        .and_then(|value| value.checked_add(usize::from(includes_snapshot) * 32))
        .ok_or(BackupError::ArchiveTooLarge)?;
    for text in [
        manifest.backup_id.as_str(),
        manifest.source_installation_id.as_str(),
        manifest.created_at_utc.as_str(),
    ] {
        bytes = bytes
            .checked_add(2)
            .and_then(|value| value.checked_add(text.len()))
            .ok_or(BackupError::ArchiveTooLarge)?;
    }
    for record in &manifest.records {
        bytes = bytes
            .checked_add(FIXED_RECORD_BYTES)
            .and_then(|value| value.checked_add(record.stable_id.len()))
            .ok_or(BackupError::ArchiveTooLarge)?;
    }
    if u64::try_from(bytes).map_err(|_| BackupError::ArchiveTooLarge)? > MAX_BACKUP_MANIFEST_BYTES {
        return Err(BackupError::ArchiveTooLarge);
    }
    Ok(bytes)
}

pub fn decode_manifest(encoded: &[u8]) -> Result<BackupManifest, BackupError> {
    if u64::try_from(encoded.len()).map_err(|_| BackupError::ArchiveTooLarge)?
        > MAX_BACKUP_MANIFEST_BYTES
    {
        return Err(BackupError::ArchiveTooLarge);
    }
    let mut reader = Reader::new(encoded);
    if reader.take_array::<4>()? != MANIFEST_MAGIC {
        return Err(BackupError::MalformedManifest);
    }
    let version = reader.u16()?;
    if version != MANIFEST_FORMAT_VERSION {
        return Err(BackupError::UnsupportedManifestVersion(version));
    }
    let selection = selection_from_mask(reader.u16()?)?;
    let backup_id = reader.text()?;
    let source_installation_id = reader.text()?;
    let created_at_utc = reader.text()?;
    let record_count = usize::try_from(reader.u32()?).map_err(|_| BackupError::TooManyRecords)?;
    if record_count > MAX_BACKUP_RECORDS || record_count > reader.remaining() / FIXED_RECORD_BYTES {
        return Err(BackupError::TooManyRecords);
    }
    let snapshot_sha256 = reader.take_array::<32>()?;
    let mut records = Vec::with_capacity(record_count);
    for _ in 0..record_count {
        records.push(decode_record(&mut reader)?);
    }
    if reader.remaining() != 0 {
        return Err(BackupError::TrailingManifestData);
    }
    let manifest = BackupManifest {
        backup_id,
        source_installation_id,
        created_at_utc,
        selection,
        records,
        snapshot_sha256,
    };
    manifest.validate()?;
    Ok(manifest)
}

fn encode_record(encoded: &mut Vec<u8>, record: &BackupRecord) -> Result<(), BackupError> {
    encoded.push(kind_wire(record.kind));
    encoded.push(state_wire(record.state));
    encoded.extend_from_slice(&0_u16.to_le_bytes());
    push_text(encoded, &record.stable_id)?;
    encoded.extend_from_slice(&record.revision.to_le_bytes());
    encoded.extend_from_slice(&record.schema_version.to_le_bytes());
    encoded.extend_from_slice(&record.plaintext_bytes.to_le_bytes());
    encoded.extend_from_slice(&record.plaintext_sha256);
    Ok(())
}

fn decode_record(reader: &mut Reader<'_>) -> Result<BackupRecord, BackupError> {
    let kind = kind_from_wire(reader.u8()?)?;
    let state = state_from_wire(reader.u8()?)?;
    if reader.u16()? != 0 {
        return Err(BackupError::MalformedManifest);
    }
    let stable_id = reader.text()?;
    let revision = reader.u64()?;
    let schema_version = reader.u32()?;
    let plaintext_bytes = reader.u64()?;
    let plaintext_sha256 = reader.take_array::<32>()?;
    Ok(BackupRecord {
        kind,
        stable_id,
        revision,
        schema_version,
        state,
        plaintext_bytes,
        plaintext_sha256,
    })
}

fn push_text(encoded: &mut Vec<u8>, value: &str) -> Result<(), BackupError> {
    let length = u16::try_from(value.len()).map_err(|_| BackupError::MalformedManifest)?;
    encoded.extend_from_slice(&length.to_le_bytes());
    encoded.extend_from_slice(value.as_bytes());
    Ok(())
}

fn selection_mask(selection: &BackupSelection) -> u16 {
    selection
        .iter()
        .fold(0_u16, |mask, kind| mask | (1_u16 << kind_wire(kind)))
}

fn selection_from_mask(mask: u16) -> Result<BackupSelection, BackupError> {
    let known_mask = BackupRecordKind::ALL
        .iter()
        .fold(0_u16, |known, kind| known | (1_u16 << kind_wire(*kind)));
    if mask == 0 || mask & !known_mask != 0 {
        return Err(BackupError::MalformedManifest);
    }
    BackupSelection::new(
        BackupRecordKind::ALL
            .into_iter()
            .filter(|kind| mask & (1_u16 << kind_wire(*kind)) != 0),
    )
}

const fn kind_wire(kind: BackupRecordKind) -> u8 {
    match kind {
        BackupRecordKind::AssistantConfiguration => 0,
        BackupRecordKind::SavedWorkspace => 1,
        BackupRecordKind::LibraryEntry => 2,
        BackupRecordKind::MemoryRecord => 3,
        BackupRecordKind::UserAuthoredSkill => 4,
        BackupRecordKind::LearnedProcedure => 5,
        BackupRecordKind::Bookmark => 6,
        BackupRecordKind::BrowserPreference => 7,
    }
}

fn kind_from_wire(wire: u8) -> Result<BackupRecordKind, BackupError> {
    match wire {
        0 => Ok(BackupRecordKind::AssistantConfiguration),
        1 => Ok(BackupRecordKind::SavedWorkspace),
        2 => Ok(BackupRecordKind::LibraryEntry),
        3 => Ok(BackupRecordKind::MemoryRecord),
        4 => Ok(BackupRecordKind::UserAuthoredSkill),
        5 => Ok(BackupRecordKind::LearnedProcedure),
        6 => Ok(BackupRecordKind::Bookmark),
        7 => Ok(BackupRecordKind::BrowserPreference),
        _ => Err(BackupError::UnknownRecordKind(wire)),
    }
}

const fn state_wire(state: BackupRecordState) -> u8 {
    match state {
        BackupRecordState::Active => 0,
        BackupRecordState::Tombstone => 1,
    }
}

fn state_from_wire(wire: u8) -> Result<BackupRecordState, BackupError> {
    match wire {
        0 => Ok(BackupRecordState::Active),
        1 => Ok(BackupRecordState::Tombstone),
        _ => Err(BackupError::UnknownRecordState(wire)),
    }
}

struct Reader<'a> {
    remaining: &'a [u8],
}

impl<'a> Reader<'a> {
    const fn new(encoded: &'a [u8]) -> Self {
        Self { remaining: encoded }
    }

    const fn remaining(&self) -> usize {
        self.remaining.len()
    }

    fn take(&mut self, length: usize) -> Result<&'a [u8], BackupError> {
        let (taken, remaining) = self
            .remaining
            .split_at_checked(length)
            .ok_or(BackupError::MalformedManifest)?;
        self.remaining = remaining;
        Ok(taken)
    }

    fn take_array<const N: usize>(&mut self) -> Result<[u8; N], BackupError> {
        self.take(N)?
            .try_into()
            .map_err(|_| BackupError::MalformedManifest)
    }

    fn u8(&mut self) -> Result<u8, BackupError> {
        self.take(1)?
            .first()
            .copied()
            .ok_or(BackupError::MalformedManifest)
    }

    fn u16(&mut self) -> Result<u16, BackupError> {
        Ok(u16::from_le_bytes(self.take_array()?))
    }

    fn u32(&mut self) -> Result<u32, BackupError> {
        Ok(u32::from_le_bytes(self.take_array()?))
    }

    fn u64(&mut self) -> Result<u64, BackupError> {
        Ok(u64::from_le_bytes(self.take_array()?))
    }

    fn text(&mut self) -> Result<String, BackupError> {
        let length = usize::from(self.u16()?);
        let text =
            core::str::from_utf8(self.take(length)?).map_err(|_| BackupError::MalformedManifest)?;
        Ok(text.to_owned())
    }
}
