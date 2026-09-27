// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! PKCE encoding over an injected reviewed SHA-256 implementation.

use super::{
    AccountError, AuthorizationEntropy, GoogleNonceEntropy, GoogleNonceHash,
    GoogleRawNonceMaterial, PkceChallenge, PkceVerifierMaterial, RedirectState,
    AUTHORIZATION_ENTROPY_BYTES,
};

pub(super) fn is_base64url(value: &str) -> bool {
    value
        .bytes()
        .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
}

/// The digest port itself is the loop kernel's (decision 0072); this module
/// keeps the path and adds the account protocol's derivations over it.
pub use loop_kernel::digest::{DigestError, Sha256Port};

pub(super) fn derive_authorization_material(
    entropy: &AuthorizationEntropy,
    digest_port: &dyn Sha256Port,
) -> Result<(RedirectState, PkceVerifierMaterial, PkceChallenge), AccountError> {
    if entropy.0.iter().all(|byte| *byte == 0) {
        return Err(AccountError::InvalidEntropy);
    }
    let mut halves = entropy.0.chunks_exact(AUTHORIZATION_ENTROPY_BYTES / 2);
    let state_entropy = halves.next().ok_or(AccountError::CryptoInvariant)?;
    let verifier_entropy = halves.next().ok_or(AccountError::CryptoInvariant)?;
    if !halves.remainder().is_empty() {
        return Err(AccountError::CryptoInvariant);
    }
    let state = RedirectState::new(encode_base64url(state_entropy)?)?;
    let verifier = encode_base64url(verifier_entropy)?.into_bytes();
    let digest = digest_port
        .sha256(&verifier)
        .map_err(|_| AccountError::CryptoInvariant)?;
    let challenge = PkceChallenge::from_digest(&digest)?;
    Ok((state, PkceVerifierMaterial(verifier), challenge))
}

/// Derives the exact nonce pair required by Supabase's Google ID-token flow.
///
/// Google receives the lowercase SHA-256 hexadecimal value. The matching raw
/// unpadded base64url value immediately leaves Rust for transient secure
/// storage and returns only as an opaque single-use handle.
pub(super) fn derive_google_nonce_material(
    entropy: &GoogleNonceEntropy,
    digest_port: &dyn Sha256Port,
) -> Result<(GoogleRawNonceMaterial, GoogleNonceHash), AccountError> {
    if entropy.0.iter().all(|byte| *byte == 0) {
        return Err(AccountError::InvalidEntropy);
    }
    let raw_nonce = encode_base64url(&entropy.0)?.into_bytes();
    if raw_nonce.len() != core_service_types::MAX_GOOGLE_RAW_NONCE_BYTES {
        return Err(AccountError::CryptoInvariant);
    }
    let digest = digest_port
        .sha256(&raw_nonce)
        .map_err(|_| AccountError::CryptoInvariant)?;
    Ok((
        GoogleRawNonceMaterial(raw_nonce),
        GoogleNonceHash::from_digest(&digest)?,
    ))
}

pub(super) fn encode_base64url(input: &[u8]) -> Result<String, AccountError> {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    fn encoded(alphabet: &[u8; 64], index: u8) -> Result<u8, AccountError> {
        alphabet
            .get(usize::from(index))
            .copied()
            .ok_or(AccountError::CryptoInvariant)
    }

    let capacity = input
        .len()
        .checked_mul(4)
        .and_then(|value| value.checked_add(2))
        .map(|value| value / 3)
        .ok_or(AccountError::CryptoInvariant)?;
    let mut output = Vec::with_capacity(capacity);
    for chunk in input.chunks(3) {
        let first = chunk
            .first()
            .copied()
            .ok_or(AccountError::CryptoInvariant)?;
        let second = chunk.get(1).copied();
        let third = chunk.get(2).copied();
        output.push(encoded(ALPHABET, first >> 2)?);
        output.push(encoded(
            ALPHABET,
            ((first & 0x03) << 4) | second.unwrap_or_default().wrapping_shr(4),
        )?);
        if let Some(second) = second {
            output.push(encoded(
                ALPHABET,
                ((second & 0x0f) << 2) | third.unwrap_or_default().wrapping_shr(6),
            )?);
        }
        if let Some(third) = third {
            output.push(encoded(ALPHABET, third & 0x3f)?);
        }
    }
    String::from_utf8(output).map_err(|_| AccountError::CryptoInvariant)
}

#[cfg(test)]
#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct ReferenceSha256;

#[cfg(test)]
impl Sha256Port for ReferenceSha256 {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        reference_sha256(input).map_err(|_| DigestError::Unavailable)
    }
}

#[cfg(test)]
#[allow(clippy::many_single_char_names, clippy::too_many_lines)]
fn reference_sha256(input: &[u8]) -> Result<[u8; 32], AccountError> {
    const ROUND: [u32; 64] = [
        0x428a_2f98,
        0x7137_4491,
        0xb5c0_fbcf,
        0xe9b5_dba5,
        0x3956_c25b,
        0x59f1_11f1,
        0x923f_82a4,
        0xab1c_5ed5,
        0xd807_aa98,
        0x1283_5b01,
        0x2431_85be,
        0x550c_7dc3,
        0x72be_5d74,
        0x80de_b1fe,
        0x9bdc_06a7,
        0xc19b_f174,
        0xe49b_69c1,
        0xefbe_4786,
        0x0fc1_9dc6,
        0x240c_a1cc,
        0x2de9_2c6f,
        0x4a74_84aa,
        0x5cb0_a9dc,
        0x76f9_88da,
        0x983e_5152,
        0xa831_c66d,
        0xb003_27c8,
        0xbf59_7fc7,
        0xc6e0_0bf3,
        0xd5a7_9147,
        0x06ca_6351,
        0x1429_2967,
        0x27b7_0a85,
        0x2e1b_2138,
        0x4d2c_6dfc,
        0x5338_0d13,
        0x650a_7354,
        0x766a_0abb,
        0x81c2_c92e,
        0x9272_2c85,
        0xa2bf_e8a1,
        0xa81a_664b,
        0xc24b_8b70,
        0xc76c_51a3,
        0xd192_e819,
        0xd699_0624,
        0xf40e_3585,
        0x106a_a070,
        0x19a4_c116,
        0x1e37_6c08,
        0x2748_774c,
        0x34b0_bcb5,
        0x391c_0cb3,
        0x4ed8_aa4a,
        0x5b9c_ca4f,
        0x682e_6ff3,
        0x748f_82ee,
        0x78a5_636f,
        0x84c8_7814,
        0x8cc7_0208,
        0x90be_fffa,
        0xa450_6ceb,
        0xbef9_a3f7,
        0xc671_78f2,
    ];

    let bit_length = u64::try_from(input.len())
        .ok()
        .and_then(|length| length.checked_mul(8))
        .ok_or(AccountError::CryptoInvariant)?;
    let mut padded = input.to_vec();
    padded.push(0x80);
    while padded.len() % 64 != 56 {
        padded.push(0);
    }
    padded.extend_from_slice(&bit_length.to_be_bytes());

    let mut state: [u32; 8] = [
        0x6a09_e667,
        0xbb67_ae85,
        0x3c6e_f372,
        0xa54f_f53a,
        0x510e_527f,
        0x9b05_688c,
        0x1f83_d9ab,
        0x5be0_cd19,
    ];
    for block in padded.chunks_exact(64) {
        let mut schedule = [0_u32; 64];
        for (slot, bytes) in schedule.iter_mut().take(16).zip(block.chunks_exact(4)) {
            let first = u32::from(
                bytes
                    .first()
                    .copied()
                    .ok_or(AccountError::CryptoInvariant)?,
            );
            let second = u32::from(bytes.get(1).copied().ok_or(AccountError::CryptoInvariant)?);
            let third = u32::from(bytes.get(2).copied().ok_or(AccountError::CryptoInvariant)?);
            let fourth = u32::from(bytes.get(3).copied().ok_or(AccountError::CryptoInvariant)?);
            *slot = (first << 24) | (second << 16) | (third << 8) | fourth;
        }
        for position in 16..schedule.len() {
            let word_15 = *schedule
                .get(position.saturating_sub(15))
                .ok_or(AccountError::CryptoInvariant)?;
            let word_2 = *schedule
                .get(position.saturating_sub(2))
                .ok_or(AccountError::CryptoInvariant)?;
            let sigma_0 = word_15.rotate_right(7) ^ word_15.rotate_right(18) ^ (word_15 >> 3);
            let sigma_1 = word_2.rotate_right(17) ^ word_2.rotate_right(19) ^ (word_2 >> 10);
            let next = schedule
                .get(position.saturating_sub(16))
                .copied()
                .ok_or(AccountError::CryptoInvariant)?
                .wrapping_add(sigma_0)
                .wrapping_add(
                    schedule
                        .get(position.saturating_sub(7))
                        .copied()
                        .ok_or(AccountError::CryptoInvariant)?,
                )
                .wrapping_add(sigma_1);
            let slot = schedule
                .get_mut(position)
                .ok_or(AccountError::CryptoInvariant)?;
            *slot = next;
        }

        let [initial_a, initial_b, initial_c, initial_d, initial_e, initial_f, initial_g, initial_h] =
            state;
        let (mut a, mut b, mut c, mut d) = (initial_a, initial_b, initial_c, initial_d);
        let (mut e, mut f, mut g, mut h) = (initial_e, initial_f, initial_g, initial_h);
        for (word, constant) in schedule.iter().zip(ROUND) {
            let sigma_1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let choice = (e & f) ^ ((!e) & g);
            let temporary_1 = h
                .wrapping_add(sigma_1)
                .wrapping_add(choice)
                .wrapping_add(constant)
                .wrapping_add(*word);
            let sigma_0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let majority = (a & b) ^ (a & c) ^ (b & c);
            let temporary_2 = sigma_0.wrapping_add(majority);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(temporary_1);
            d = c;
            c = b;
            b = a;
            a = temporary_1.wrapping_add(temporary_2);
        }
        state = [
            initial_a.wrapping_add(a),
            initial_b.wrapping_add(b),
            initial_c.wrapping_add(c),
            initial_d.wrapping_add(d),
            initial_e.wrapping_add(e),
            initial_f.wrapping_add(f),
            initial_g.wrapping_add(g),
            initial_h.wrapping_add(h),
        ];
    }

    let mut digest = [0_u8; 32];
    for (word, bytes) in state.iter().zip(digest.chunks_exact_mut(4)) {
        bytes.copy_from_slice(&word.to_be_bytes());
    }
    Ok(digest)
}
