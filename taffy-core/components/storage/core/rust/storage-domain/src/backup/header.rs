// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact content-free public prefix for `.aib` archive version 1.

use super::{
    BackupCipherSuite, BackupError, BACKUP_PAYLOAD_CHUNK_BYTES, MAX_BACKUP_MANIFEST_BYTES,
    MAX_BACKUP_PLAINTEXT_BYTES,
};

/// The fixed public prefix of every `.aib` archive.
pub const AIB_MAGIC: [u8; 8] = *b"TAFFYAIB";
/// First independently readable archive format.
pub const AIB_FORMAT_VERSION: u16 = 1;
/// Exact byte length of the content-free version-1 public prefix.
pub const AIB_PUBLIC_HEADER_BYTES: usize = 56;
/// Ciphertext, manifest and authentication overhead must remain within this
/// streaming-parser bound before any destination bytes are allocated.
const MAX_AIB_ARCHIVE_BYTES: u64 = 9 * 1024 * 1024 * 1024;
const SEALED_SECTION_OVERHEAD_BYTES: u64 = 12 + 16;

/// Content-free public archive metadata. Record identities, timestamps and
/// counts stay inside the encrypted manifest.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AibHeader {
    pub magic: [u8; 8],
    pub format_version: u16,
    pub cipher_suite: BackupCipherSuite,
    pub archive_id: [u8; 16],
    pub key_wrap_nonce: [u8; 12],
    pub encrypted_manifest_bytes: u64,
    pub encrypted_payload_bytes: u64,
}

impl AibHeader {
    pub fn validate(&self) -> Result<(), BackupError> {
        if self.magic != AIB_MAGIC {
            return Err(BackupError::WrongMagic);
        }
        if self.format_version != AIB_FORMAT_VERSION {
            return Err(BackupError::UnsupportedFormat(self.format_version));
        }
        if self.archive_id.iter().all(|byte| *byte == 0)
            || self.key_wrap_nonce.iter().all(|byte| *byte == 0)
        {
            return Err(BackupError::InvalidNonce);
        }
        if self.encrypted_manifest_bytes <= SEALED_SECTION_OVERHEAD_BYTES {
            return Err(BackupError::MissingManifest);
        }
        if self.encrypted_manifest_bytes > MAX_BACKUP_MANIFEST_BYTES + SEALED_SECTION_OVERHEAD_BYTES
        {
            return Err(BackupError::ArchiveTooLarge);
        }
        if self.encrypted_payload_bytes != 0
            && self.encrypted_payload_bytes <= SEALED_SECTION_OVERHEAD_BYTES
        {
            return Err(BackupError::ArchiveTooLarge);
        }
        if !is_canonical_payload_section(self.encrypted_payload_bytes) {
            return Err(BackupError::MalformedHeader);
        }
        if self.encrypted_payload_bytes > max_encrypted_payload_bytes()? {
            return Err(BackupError::ArchiveTooLarge);
        }
        let encrypted_bytes = self
            .encrypted_manifest_bytes
            .checked_add(self.encrypted_payload_bytes)
            .ok_or(BackupError::ArchiveTooLarge)?;
        if encrypted_bytes > MAX_AIB_ARCHIVE_BYTES {
            return Err(BackupError::ArchiveTooLarge);
        }
        Ok(())
    }
}

#[must_use]
pub fn encode_aib_header(header: &AibHeader) -> [u8; AIB_PUBLIC_HEADER_BYTES] {
    let mut encoded = Vec::with_capacity(AIB_PUBLIC_HEADER_BYTES);
    encoded.extend_from_slice(&header.magic);
    encoded.extend_from_slice(&header.format_version.to_le_bytes());
    encoded.extend_from_slice(&cipher_suite_wire(header.cipher_suite).to_le_bytes());
    encoded.extend_from_slice(&header.archive_id);
    encoded.extend_from_slice(&header.key_wrap_nonce);
    encoded.extend_from_slice(&header.encrypted_manifest_bytes.to_le_bytes());
    encoded.extend_from_slice(&header.encrypted_payload_bytes.to_le_bytes());
    encoded
        .try_into()
        .unwrap_or([0_u8; AIB_PUBLIC_HEADER_BYTES])
}

pub fn decode_aib_header(encoded: &[u8]) -> Result<AibHeader, BackupError> {
    if encoded.len() != AIB_PUBLIC_HEADER_BYTES {
        return Err(BackupError::MalformedHeader);
    }
    let mut remaining = encoded;
    let magic = take_header_array::<8>(&mut remaining)?;
    let format_version = u16::from_le_bytes(take_header_array::<2>(&mut remaining)?);
    let cipher_wire = u16::from_le_bytes(take_header_array::<2>(&mut remaining)?);
    let header = AibHeader {
        magic,
        format_version,
        cipher_suite: cipher_suite_from_wire(cipher_wire)?,
        archive_id: take_header_array::<16>(&mut remaining)?,
        key_wrap_nonce: take_header_array::<12>(&mut remaining)?,
        encrypted_manifest_bytes: u64::from_le_bytes(take_header_array::<8>(&mut remaining)?),
        encrypted_payload_bytes: u64::from_le_bytes(take_header_array::<8>(&mut remaining)?),
    };
    if !remaining.is_empty() {
        return Err(BackupError::MalformedHeader);
    }
    header.validate()?;
    Ok(header)
}

fn take_header_array<const N: usize>(input: &mut &[u8]) -> Result<[u8; N], BackupError> {
    let (value, remaining) = input
        .split_at_checked(N)
        .ok_or(BackupError::MalformedHeader)?;
    *input = remaining;
    value.try_into().map_err(|_| BackupError::MalformedHeader)
}

fn cipher_suite_wire(suite: BackupCipherSuite) -> u16 {
    match suite {
        BackupCipherSuite::Aes256GcmHkdfSha256 => 1,
    }
}

fn cipher_suite_from_wire(wire: u16) -> Result<BackupCipherSuite, BackupError> {
    match wire {
        1 => Ok(BackupCipherSuite::Aes256GcmHkdfSha256),
        value => Err(BackupError::UnsupportedCipherSuite(value)),
    }
}

fn is_canonical_payload_section(section_bytes: u64) -> bool {
    if section_bytes == 0 {
        return true;
    }
    let full_frame_bytes = BACKUP_PAYLOAD_CHUNK_BYTES + SEALED_SECTION_OVERHEAD_BYTES;
    let final_frame_bytes = section_bytes % full_frame_bytes;
    final_frame_bytes == 0 || final_frame_bytes > SEALED_SECTION_OVERHEAD_BYTES
}

fn max_encrypted_payload_bytes() -> Result<u64, BackupError> {
    let chunks = MAX_BACKUP_PLAINTEXT_BYTES.div_ceil(BACKUP_PAYLOAD_CHUNK_BYTES);
    MAX_BACKUP_PLAINTEXT_BYTES
        .checked_add(
            chunks
                .checked_mul(SEALED_SECTION_OVERHEAD_BYTES)
                .ok_or(BackupError::ArchiveTooLarge)?,
        )
        .ok_or(BackupError::ArchiveTooLarge)
}
