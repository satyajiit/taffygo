// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The delivery port: what the core knows about the product's own artifacts.
//!
//! The port speaks the Core Service contract's types rather than the delivery
//! crate's, for the same reason every other port here does: what crosses the
//! process boundary is the contract, and a port that spoke a private vocabulary
//! would need a translation at each call site instead of one inside its
//! adapter.
//!
//! Nothing here fetches anything. The port decides what should be fetched; the
//! browser opens the socket, writes the file and reports back, because the
//! browser is the only process that may.

use core_service_types as wire;

/// One asset's complete state, as the core knows it.
///
/// Wider than [`wire::AssetOnDisk`], which is what the browser found on a
/// filesystem, because this is what the core adds to it: the catalog's answer
/// for what the asset is and how large it should be, and the plane's answer
/// for how many attempts have been spent and why the last one did not install.
///
/// It is a type of this crate rather than of a contract because it crosses no
/// process boundary itself. The Core API `AssetViewState` is what reaches a
/// surface, projected from this in `runtime::status`; keeping the two apart is
/// what lets the outward shape be frozen while this one stays a working type.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetInstallationView {
    /// The asset's stable identity.
    pub asset_id: String,
    /// The revision this state is about.
    pub asset_revision: String,
    /// What the asset is for, from the catalog.
    pub kind: wire::AssetKind,
    /// How much of it is on the device.
    pub presence: wire::AssetPresence,
    /// Bytes on disk.
    pub written_bytes: u64,
    /// Bytes the catalog says there are; zero for a variant with no published bytes.
    pub total_bytes: u64,
    /// How many transfers have been attempted and ended.
    pub attempts: u32,
    /// The monotonic millisecond before which nothing should be attempted.
    pub retry_after_monotonic_ms: u64,
    /// Why the last attempt did not end in an install, when one did not.
    pub refusal: Option<wire::AssetRefusalReason>,
    /// Whether asking again could reach a different answer, when there is a
    /// refusal to ask about.
    ///
    /// Carried rather than derived from `refusal`, because the rule that
    /// decides it belongs to the delivery crate that owns the reasons. A second
    /// copy of that match here would be a second place for it to be wrong, and
    /// the wrong answer is a surface offering a control that cannot work.
    pub refusal_retryable: bool,
}

/// What the device is and what it is allowed to spend, as the core knows it.
///
/// The three facts that decide whether any artifact moves at all, held apart
/// from the per-asset states because they explain all of them at once: a
/// surface showing nine assets waiting needs to say "no connection" once, not
/// nine times.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AssetDeliveryPosture {
    /// Whether the catalog this build carries publishes anything for this
    /// device.
    ///
    /// False is not a failure. A host test binary, and any target the catalog
    /// has no variants for, is a build that correctly fetches nothing — and a
    /// surface that cannot tell that apart from a stalled fetch would show a
    /// person an empty list with no reason for it.
    pub platform_supported: bool,
    /// What the connection costs right now.
    pub network_cost: wire::AssetNetworkCost,
    /// Carried for the wire. Transfer does not read it.
    pub metered_permitted: bool,
}

/// Why a delivery port refused what it was handed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AssetDeliveryError {
    /// A restored record named an identity or revision that is not one.
    MalformedIdentity {
        /// The identity as it arrived.
        asset_id: String,
    },
    /// A restored record named an asset this build's catalog does not have.
    ///
    /// Not an error to recover from by guessing: an artifact on disk that the
    /// product no longer knows about is one the browser should be told to
    /// remove, and inventing a catalog row for it would make it permanent.
    UnknownAsset {
        /// The identity as it arrived.
        asset_id: String,
        /// The revision as it arrived.
        asset_revision: String,
    },
    /// More records than a profile may hold.
    TooManyAssets {
        /// How many arrived.
        count: usize,
    },
    /// A report arrived for an asset no plan had asked about.
    UnexpectedReport {
        /// The identity as it arrived.
        asset_id: String,
    },
    /// A model row which reached the installed state cannot be described to
    /// the browser without weakening one of the catalog's claims.
    InvalidModelArtifact {
        /// The catalog identity whose registration could not be built.
        asset_id: String,
        /// The exact revision whose bytes were installed.
        asset_revision: String,
    },
    /// More installed model rows than the Core Service contract may publish.
    TooManyModelArtifacts {
        /// The number the complete replacement snapshot would contain.
        count: usize,
    },
}

/// What the core knows and decides about asset delivery for one profile.
pub trait AssetDeliveryPort {
    /// Replaces everything known with what the browser found on disk.
    ///
    /// Called once, at bootstrap. A record the catalog does not recognise is
    /// refused rather than kept, so the set the core reasons about is always a
    /// subset of the set the product was built with.
    fn restore(&mut self, found: &[wire::AssetOnDisk]) -> Result<(), AssetDeliveryError>;

    /// Records what this device is. Called once, at bootstrap.
    fn set_platform(&mut self, platform: wire::AssetPlatform);

    /// Records what the connection costs and what a person allowed.
    fn set_network(&mut self, cost: wire::AssetNetworkCost, metered_permitted: bool);

    /// What the browser should do about every asset right now.
    ///
    /// A pure question: asking twice with the same monotonic time answers the
    /// same thing, and asking does not change anything.
    fn plan(&self, now_monotonic_ms: u64) -> Vec<wire::AssetDeliveryEffect>;

    /// Where this asset sits in the catalog this build carries.
    ///
    /// For catalog-order assertions and presentation only. Effect identity is
    /// content-derived: a position can name different content after a catalog
    /// update. `None` means the catalog has no such row.
    fn catalog_position(&self, asset_id: &str, asset_revision: &str) -> Option<u32>;

    /// Completed transfer attempts for one catalogued asset.
    ///
    /// A direct keyed query for effect identity; it does not allocate the full
    /// installation projection merely to inspect one row.
    fn completed_attempts(&self, asset_id: &str, asset_revision: &str) -> Option<u32>;

    /// Marks one asset as wanted although start-up would not have fetched it.
    fn request(&mut self, asset_id: &str, asset_revision: &str) -> Result<(), AssetDeliveryError>;

    /// Marks one asset as no longer wanted, and answers what to remove.
    fn remove(
        &mut self,
        asset_id: &str,
        asset_revision: &str,
    ) -> Result<wire::AssetDeliveryEffect, AssetDeliveryError>;

    /// Records what the browser observed about one transfer.
    fn record_transfer(
        &mut self,
        report: &wire::AssetTransferReport,
        now_monotonic_ms: u64,
    ) -> Result<(), AssetDeliveryError>;

    /// Records that an asset's bytes were deleted.
    fn record_removal(
        &mut self,
        report: &wire::AssetRemovalReport,
    ) -> Result<(), AssetDeliveryError>;

    /// Every asset the catalog carries, in catalog order, with its state.
    ///
    /// Every asset, not every installed one: a surface that only hears about
    /// the assets that worked cannot show a person why another did not.
    fn installations(&self) -> Vec<AssetInstallationView>;

    /// Installed model rows, reconciled against this adapter's catalog.
    ///
    /// This is the only catalog-fact carrier into the browser. It returns no
    /// path and no row the browser did not report as installed at the exact
    /// catalog length. The browser still reopens and hashes the descriptor;
    /// this answer describes what that descriptor must prove to become usable.
    fn model_artifact_registrations(
        &self,
    ) -> Result<Vec<wire::ModelArtifactRegistration>, AssetDeliveryError>;

    /// What the device is and what it may spend.
    fn posture(&self) -> AssetDeliveryPosture;
}

impl<T> AssetDeliveryPort for Box<T>
where
    T: AssetDeliveryPort + ?Sized,
{
    fn restore(&mut self, found: &[wire::AssetOnDisk]) -> Result<(), AssetDeliveryError> {
        self.as_mut().restore(found)
    }

    fn set_platform(&mut self, platform: wire::AssetPlatform) {
        self.as_mut().set_platform(platform);
    }

    fn set_network(&mut self, cost: wire::AssetNetworkCost, metered_permitted: bool) {
        self.as_mut().set_network(cost, metered_permitted);
    }

    fn plan(&self, now_monotonic_ms: u64) -> Vec<wire::AssetDeliveryEffect> {
        self.as_ref().plan(now_monotonic_ms)
    }

    fn catalog_position(&self, asset_id: &str, asset_revision: &str) -> Option<u32> {
        self.as_ref().catalog_position(asset_id, asset_revision)
    }

    fn completed_attempts(&self, asset_id: &str, asset_revision: &str) -> Option<u32> {
        self.as_ref().completed_attempts(asset_id, asset_revision)
    }

    fn request(&mut self, asset_id: &str, asset_revision: &str) -> Result<(), AssetDeliveryError> {
        self.as_mut().request(asset_id, asset_revision)
    }

    fn remove(
        &mut self,
        asset_id: &str,
        asset_revision: &str,
    ) -> Result<wire::AssetDeliveryEffect, AssetDeliveryError> {
        self.as_mut().remove(asset_id, asset_revision)
    }

    fn record_transfer(
        &mut self,
        report: &wire::AssetTransferReport,
        now_monotonic_ms: u64,
    ) -> Result<(), AssetDeliveryError> {
        self.as_mut().record_transfer(report, now_monotonic_ms)
    }

    fn record_removal(
        &mut self,
        report: &wire::AssetRemovalReport,
    ) -> Result<(), AssetDeliveryError> {
        self.as_mut().record_removal(report)
    }

    fn installations(&self) -> Vec<AssetInstallationView> {
        self.as_ref().installations()
    }

    fn model_artifact_registrations(
        &self,
    ) -> Result<Vec<wire::ModelArtifactRegistration>, AssetDeliveryError> {
        self.as_ref().model_artifact_registrations()
    }

    fn posture(&self) -> AssetDeliveryPosture {
        self.as_ref().posture()
    }
}
