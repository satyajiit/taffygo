// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Where one asset has got to.
//!
//! The state is a fact about a device, not a decision. It is what the browser
//! reports and what the surface renders; [`crate::plan`] is the part that reads
//! it and says what should happen next. Keeping the two apart is what makes the
//! decision testable without a device and the fact recordable without a policy.

use crate::ids::{AssetId, AssetRevision};

/// Whether an asset's bytes are on the device.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Presence {
    /// Nothing has been written.
    Absent,
    /// Some bytes are staged and more may follow.
    Partial,
    /// Every byte was written and judged sound, but has not been committed.
    Complete,
    /// Every byte was written, judged sound and committed.
    Installed,
}

/// How far a transfer has got.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TransferProgress {
    /// Bytes already on disk.
    pub written_bytes: u64,
    /// Bytes the catalog says there are, which is never zero for a fetchable
    /// variant, so progress is never indeterminate.
    pub total_bytes: u64,
}

impl TransferProgress {
    /// Progress in basis points, from `0` to `10_000`.
    ///
    /// Basis points rather than a float because this number crosses a process
    /// boundary into a contract, and an integer means the two sides cannot
    /// round differently.
    pub fn basis_points(self) -> u32 {
        if self.total_bytes == 0 {
            return 0;
        }
        let written = self.written_bytes.min(self.total_bytes);
        let scaled = u128::from(written) * 10_000 / u128::from(self.total_bytes);
        u32::try_from(scaled).unwrap_or(10_000)
    }
}

/// Why the plane will not fetch an asset.
///
/// Every one of these is a refusal a person may be shown, so each names a
/// condition rather than a component: "the bytes were not published" is
/// something a surface can say, and "variant lookup returned None" is not.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RefusalReason {
    /// The catalog has no row with that identity and revision.
    UnknownAsset,
    /// The row publishes nothing for this device's platform.
    NoVariantForPlatform,
    /// The row names the platform but its bytes do not exist yet.
    NotPublishedYet,
    /// The row is published but incomplete — no digest, no length or no path.
    CatalogRowIncomplete,
    /// The row declares more bytes than a variant may.
    VariantTooLarge,
    /// The transfer failed as many times as it may.
    AttemptsExhausted,
    /// The bytes that arrived were not the bytes the catalog names.
    IntegrityFailed,
    /// The device is on a connection this asset may not use.
    NetworkNotPermitted,
    /// A person turned this asset off.
    DeclinedByPerson,
}

impl RefusalReason {
    /// Whether asking again could reach a different answer without anything
    /// else changing.
    ///
    /// A catalog defect and a person's decision cannot; a network condition and
    /// exhausted attempts can. The surface uses this to decide whether to
    /// offer a retry, so an unretryable refusal never renders a button that
    /// does nothing.
    pub fn is_retryable(self) -> bool {
        match self {
            Self::AttemptsExhausted | Self::NetworkNotPermitted | Self::IntegrityFailed => true,
            Self::UnknownAsset
            | Self::NoVariantForPlatform
            | Self::NotPublishedYet
            | Self::CatalogRowIncomplete
            | Self::VariantTooLarge
            | Self::DeclinedByPerson => false,
        }
    }
}

/// One asset's state on one device.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetState {
    /// Which asset.
    pub id: AssetId,
    /// Which revision of it.
    pub revision: AssetRevision,
    /// Whether its bytes are here.
    pub presence: Presence,
    /// How far the current or last transfer got.
    pub progress: TransferProgress,
    /// How many transfers have been attempted and ended.
    pub attempts: u32,
    /// The monotonic millisecond before which nothing should be attempted.
    pub retry_after_monotonic_ms: u64,
    /// Why the last attempt did not end in an install, when one did not.
    ///
    /// A retryable reason survives a scheduled retry deliberately: it is what
    /// a surface says while the device waits. Whether the plane will proceed
    /// again is [`RefusalReason::is_retryable`] together with the attempt
    /// count, never the presence of a reason.
    pub refusal: Option<RefusalReason>,
}

impl AssetState {
    /// A state for an asset nothing has been done about.
    pub fn absent(id: AssetId, revision: AssetRevision, total_bytes: u64) -> Self {
        Self {
            id,
            revision,
            presence: Presence::Absent,
            progress: TransferProgress {
                written_bytes: 0,
                total_bytes,
            },
            attempts: 0,
            retry_after_monotonic_ms: 0,
            refusal: None,
        }
    }

    /// Whether the asset is usable right now.
    pub fn is_installed(&self) -> bool {
        matches!(self.presence, Presence::Installed)
    }

    /// The byte a resumed transfer should ask for first.
    ///
    /// Zero unless a partial staging file exists, because a range request for
    /// byte zero and a plain request are the same request and the simpler one
    /// is likelier to be answered.
    pub fn resume_offset(&self) -> u64 {
        match self.presence {
            Presence::Partial => self.progress.written_bytes,
            Presence::Absent | Presence::Complete | Presence::Installed => 0,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{AssetState, Presence, RefusalReason, TransferProgress};
    use crate::ids::{AssetId, AssetRevision};

    fn state() -> AssetState {
        AssetState::absent(
            AssetId::parse("python-stdlib").unwrap_or_else(|_| unreachable!()),
            AssetRevision::parse("3.14.2").unwrap_or_else(|_| unreachable!()),
            1_000,
        )
    }

    #[test]
    fn progress_is_basis_points_and_never_exceeds_them() {
        let full = TransferProgress {
            written_bytes: 1_000,
            total_bytes: 1_000,
        };
        assert_eq!(full.basis_points(), 10_000);
        let over = TransferProgress {
            written_bytes: 5_000,
            total_bytes: 1_000,
        };
        assert_eq!(over.basis_points(), 10_000);
        let third = TransferProgress {
            written_bytes: 1,
            total_bytes: 3,
        };
        assert_eq!(third.basis_points(), 3_333);

        let largest = TransferProgress {
            written_bytes: u64::MAX,
            total_bytes: u64::MAX,
        };
        assert_eq!(
            largest.basis_points(),
            10_000,
            "multiplication must not saturate before the division"
        );
    }

    #[test]
    fn an_unknown_total_reports_no_progress_rather_than_all_of_it() {
        let unknown = TransferProgress {
            written_bytes: 500,
            total_bytes: 0,
        };
        assert_eq!(unknown.basis_points(), 0);
    }

    #[test]
    fn only_a_partial_transfer_resumes_from_an_offset() {
        let mut it = state();
        it.progress.written_bytes = 400;
        for (presence, expected) in [
            (Presence::Absent, 0),
            (Presence::Partial, 400),
            (Presence::Complete, 0),
            (Presence::Installed, 0),
        ] {
            it.presence = presence;
            assert_eq!(it.resume_offset(), expected, "{presence:?}");
        }
    }

    #[test]
    fn a_catalog_defect_offers_no_retry_and_a_network_condition_does() {
        assert!(!RefusalReason::NotPublishedYet.is_retryable());
        assert!(!RefusalReason::CatalogRowIncomplete.is_retryable());
        assert!(!RefusalReason::DeclinedByPerson.is_retryable());
        assert!(RefusalReason::NetworkNotPermitted.is_retryable());
        assert!(RefusalReason::AttemptsExhausted.is_retryable());
    }
}
