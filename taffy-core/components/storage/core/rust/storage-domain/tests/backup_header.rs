// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{
    decode_aib_header, encode_aib_header, AibHeader, BackupCipherSuite, BackupError,
    AIB_FORMAT_VERSION, AIB_MAGIC,
};

#[test]
fn public_header_has_no_record_metadata_and_rejects_zero_nonces() {
    let mut header = AibHeader {
        magic: AIB_MAGIC,
        format_version: AIB_FORMAT_VERSION,
        cipher_suite: BackupCipherSuite::Aes256GcmHkdfSha256,
        archive_id: [1; 16],
        key_wrap_nonce: [2; 12],
        encrypted_manifest_bytes: 128,
        encrypted_payload_bytes: 4096,
    };
    assert_eq!(header.validate(), Ok(()));
    header.archive_id = [0; 16];
    assert_eq!(header.validate(), Err(BackupError::InvalidNonce));
}

#[test]
fn public_header_matches_the_cross_language_version_one_golden() -> Result<(), BackupError> {
    const GOLDEN_HEX: &str = include!("../../../schema/aib_v1_public_header_golden.inc");
    let header = AibHeader {
        magic: AIB_MAGIC,
        format_version: AIB_FORMAT_VERSION,
        cipher_suite: BackupCipherSuite::Aes256GcmHkdfSha256,
        archive_id: [
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
            0x0e, 0x0f,
        ],
        key_wrap_nonce: [
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
        ],
        encrypted_manifest_bytes: 0x101,
        encrypted_payload_bytes: 0x0010_001c,
    };
    let encoded = encode_aib_header(&header);
    assert_eq!(hex(&encoded), GOLDEN_HEX);
    assert_eq!(decode_aib_header(&encoded)?, header);
    let truncated = encoded
        .get(..encoded.len().saturating_sub(1))
        .ok_or(BackupError::MalformedHeader)?;
    assert_eq!(
        decode_aib_header(truncated),
        Err(BackupError::MalformedHeader)
    );
    Ok(())
}

fn hex(bytes: &[u8]) -> String {
    let mut encoded = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        encoded.push(char::from_digit(u32::from(byte >> 4), 16).unwrap_or('?'));
        encoded.push(char::from_digit(u32::from(byte & 0x0f), 16).unwrap_or('?'));
    }
    encoded
}
