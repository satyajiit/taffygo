// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The digest port: the one cryptographic primitive the kernel is lent.
//!
//! Production strength comes from Chromium/BoringSSL above; the kernel only
//! states the shape. Hashing one bounded value is local, stateless CPU work —
//! an implementation performs no Mojo, network, filesystem, or randomness
//! work.

/// Why the reviewed digest adapter could not produce SHA-256.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DigestError {
    /// The local utility adapter refused or failed bounded computation.
    Unavailable,
}

/// Narrow SHA-256 primitive implemented by Chromium/BoringSSL in production.
pub trait Sha256Port {
    /// Computes exactly one 32-byte SHA-256 digest.
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError>;
}
