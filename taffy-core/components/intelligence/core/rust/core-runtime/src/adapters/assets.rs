// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The canonical delivery adapter: the compiled-in catalog, and one state per row.
//!
//! It owns no socket and no file. What it owns is the answer to "what should
//! the browser do next about the product's own artifacts", derived from a
//! catalog that cannot change at run time and a set of facts the browser
//! reported. Everything consequential is [`asset_plane`]'s; this file gives it
//! the contract's vocabulary and a place to keep state.

mod model_registration;
mod translate;

use std::collections::{BTreeMap, BTreeSet};

use asset_plane::plan::{
    advance, initial_state, plan_one_asset, plan_startup, restart_retry_series, DevicePolicy,
    InstallStep, Jitter, NetworkCost, TransferOutcome,
};
use asset_plane::{
    AssetId, AssetRevision, AssetState, Catalog, Platform, Presence, RefusalReason, Variant,
};
use core_service_types as wire;

use crate::ports::{
    AssetDeliveryError, AssetDeliveryPort, AssetDeliveryPosture, AssetInstallationView,
};

/// The most asset records a bootstrap may carry.
///
/// The catalog is compiled in, so a device cannot legitimately hold more rows
/// than the product has. A bootstrap carrying more is refused rather than
/// truncated, because truncation would silently drop the record of an artifact
/// that is really on the disk.
const MAX_RESTORED: usize = 64;

/// The canonical [`AssetDeliveryPort`].
pub struct ProductionAssetDelivery {
    catalog: Catalog,
    states: BTreeMap<(String, String), AssetState>,
    requested: BTreeSet<(String, String)>,
    policy: DevicePolicy,
    jitter: Box<dyn Jitter>,
}

impl core::fmt::Debug for ProductionAssetDelivery {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("ProductionAssetDelivery")
            .field("catalog_fingerprint", &self.catalog.fingerprint())
            .field("state_count", &self.states.len())
            .field("requested_count", &self.requested.len())
            .finish_non_exhaustive()
    }
}

impl ProductionAssetDelivery {
    /// Builds the adapter over the catalog this build was compiled with.
    ///
    /// `jitter` is the host's randomness, injected for the same reason the
    /// model router injects its own: a backoff schedule that cannot be
    /// reproduced is a schedule nobody can test.
    ///
    /// The device starts offline. That is the safe initial answer rather than
    /// a guess: nothing is fetched until the browser says there is a live
    /// connection. Required rows then start; on-demand rows wait until a
    /// feature asks. Either may use that connection.
    pub fn new(jitter: Box<dyn Jitter>) -> Self {
        Self::with_catalog(Catalog::product(), jitter)
    }

    /// Builds the adapter over a catalog a test supplies.
    pub fn with_catalog(catalog: Catalog, jitter: Box<dyn Jitter>) -> Self {
        Self {
            catalog,
            states: BTreeMap::new(),
            requested: BTreeSet::new(),
            policy: DevicePolicy {
                platform: Platform::AndroidArm64,
                network: NetworkCost::Offline,
                metered_permitted: false,
            },
            jitter,
        }
    }

    /// The catalog's fingerprint, so one build's set can be told from another's.
    pub const fn catalog_fingerprint(&self) -> u32 {
        self.catalog.fingerprint()
    }

    /// Whether an asset is installed and may be used.
    pub fn is_installed(&self, asset_id: &str, asset_revision: &str) -> bool {
        self.states
            .get(&key(asset_id, asset_revision))
            .is_some_and(AssetState::is_installed)
    }

    fn parse(asset_id: &str, asset_revision: &str) -> Result<(), AssetDeliveryError> {
        if AssetId::parse(asset_id).is_err() || AssetRevision::parse(asset_revision).is_err() {
            return Err(AssetDeliveryError::MalformedIdentity {
                asset_id: asset_id.to_owned(),
            });
        }
        Ok(())
    }

    fn require_cataloged(
        &self,
        asset_id: &str,
        asset_revision: &str,
    ) -> Result<(), AssetDeliveryError> {
        Self::parse(asset_id, asset_revision)?;
        let known = self
            .catalog
            .entries()
            .iter()
            .any(|entry| entry.id() == asset_id && entry.revision() == asset_revision);
        if known {
            return Ok(());
        }
        Err(AssetDeliveryError::UnknownAsset {
            asset_id: asset_id.to_owned(),
            asset_revision: asset_revision.to_owned(),
        })
    }

    fn known_states(&self) -> Vec<AssetState> {
        self.states.values().cloned().collect()
    }

    fn apply(&mut self, asset_id: &str, asset_revision: &str, state: AssetState) {
        self.states.insert(key(asset_id, asset_revision), state);
    }

    /// The state for one asset, or the absent state its catalog row implies.
    fn state_or_absent(&self, asset_id: &str, asset_revision: &str) -> Option<AssetState> {
        if let Some(existing) = self.states.get(&key(asset_id, asset_revision)) {
            return Some(existing.clone());
        }
        let entry = self
            .catalog
            .entries()
            .iter()
            .find(|it| it.id() == asset_id && it.revision() == asset_revision)?;
        let variant = entry.variant(self.policy.platform)?;
        initial_state(entry, variant)
    }
}

/// Backoff jitter drawn from the generation's browser-minted entropy.
///
/// The core reads no clock and no randomness, so jitter cannot come from the
/// host here. It comes from the thirty-two bytes the browser already minted for
/// this generation, which makes the schedule different on every device and on
/// every generation, and still exactly reproducible from a recorded bootstrap.
/// Two devices retrying in lockstep is the thing jitter exists to prevent, and
/// per-generation entropy prevents it without anything reading `/dev/urandom`
/// from inside a sandbox that should not have it.
#[derive(Clone, Copy, Debug)]
pub struct GenerationJitter {
    entropy: [u8; 32],
}

impl GenerationJitter {
    /// Builds jitter from one generation's capability entropy.
    pub const fn new(entropy: [u8; 32]) -> Self {
        Self { entropy }
    }
}

impl Jitter for GenerationJitter {
    fn fraction_percent(&self, attempt: u32) -> u32 {
        let index = (attempt as usize) % self.entropy.len();
        let byte = self.entropy.get(index).copied().unwrap_or(0);
        u32::from(byte) * 100 / 255
    }
}

fn key(asset_id: &str, asset_revision: &str) -> (String, String) {
    (asset_id.to_owned(), asset_revision.to_owned())
}

/// The effect one fetch step becomes, or `None` for a step that is a state.
///
/// A wait and a refusal are states, not work. They reach a person through
/// [`AssetDeliveryPort::installations`], which is where a surface reads them;
/// emitting them as effects would ask the browser to perform nothing and then
/// report that it had.
fn step_to_effect(step: &InstallStep) -> Option<wire::AssetDeliveryEffect> {
    let InstallStep::Fetch {
        id,
        revision,
        path,
        offset,
        total_bytes,
        digest,
        container,
    } = step
    else {
        return None;
    };
    Some(wire::AssetDeliveryEffect {
        operation_kind: wire::AssetDeliveryOperation::FetchAsset,
        fetch: Some(wire::AssetFetchRequest {
            asset_id: id.clone(),
            asset_revision: revision.clone(),
            origin_path: path.clone(),
            offset_bytes: *offset,
            total_bytes: *total_bytes,
            expected_digest: *digest.as_bytes(),
            container: translate::container_to_wire(*container),
        }),
        remove: None,
    })
}

/// What one report means to the plane.
fn outcome_of(report: &wire::AssetTransferReport) -> TransferOutcome {
    match report.outcome {
        wire::AssetTransferOutcome::Interrupted => TransferOutcome::Interrupted {
            written_bytes: report.written_bytes,
        },
        wire::AssetTransferOutcome::OriginRefusedTemporary => {
            TransferOutcome::OriginRefused { permanent: false }
        }
        wire::AssetTransferOutcome::OriginRefusedPermanent => {
            TransferOutcome::OriginRefused { permanent: true }
        }
        wire::AssetTransferOutcome::IntegritySound => {
            TransferOutcome::Judged(asset_plane::IntegrityVerdict::Sound)
        }
        wire::AssetTransferOutcome::IntegrityWrongLength => {
            TransferOutcome::Judged(asset_plane::IntegrityVerdict::WrongLength {
                expected: report.written_bytes,
                actual: report.observed_bytes,
            })
        }
        wire::AssetTransferOutcome::IntegrityWrongDigest => {
            TransferOutcome::Judged(asset_plane::IntegrityVerdict::WrongDigest)
        }
        wire::AssetTransferOutcome::Installed => TransferOutcome::Installed,
        wire::AssetTransferOutcome::Declined => TransferOutcome::Declined,
    }
}

impl AssetDeliveryPort for ProductionAssetDelivery {
    fn restore(&mut self, found: &[wire::AssetOnDisk]) -> Result<(), AssetDeliveryError> {
        if found.len() > MAX_RESTORED {
            return Err(AssetDeliveryError::TooManyAssets { count: found.len() });
        }
        let mut restored = BTreeMap::new();
        for record in found {
            self.require_cataloged(&record.asset_id, &record.asset_revision)?;
            let id = AssetId::parse(&record.asset_id).map_err(|_| {
                AssetDeliveryError::MalformedIdentity {
                    asset_id: record.asset_id.clone(),
                }
            })?;
            let revision = AssetRevision::parse(&record.asset_revision).map_err(|_| {
                AssetDeliveryError::MalformedIdentity {
                    asset_id: record.asset_id.clone(),
                }
            })?;
            // The catalog owns how large the artifact is; the filesystem owns
            // how much of it is there. Taking the total from the disk would
            // mean a truncated file that reported its own length as the whole
            // artifact, which reads as a finished install.
            let variant = self
                .catalog
                .entries()
                .iter()
                .find(|entry| {
                    entry.id() == record.asset_id && entry.revision() == record.asset_revision
                })
                .and_then(|entry| entry.variant(self.policy.platform));
            let total_bytes = variant.map_or(0, Variant::transfer_bytes);
            let mut state = AssetState::absent(id, revision, total_bytes);
            let exact_install = variant.is_some_and(|candidate| {
                record.presence == wire::AssetPresence::Installed
                    && record.written_bytes == candidate.installed_bytes()
                    && candidate.installed_bytes() > 0
            });
            if exact_install {
                state.presence = Presence::Installed;
                state.progress.written_bytes = record.written_bytes;
            } else if record.presence != wire::AssetPresence::Installed {
                state.presence = translate::presence_from_wire(record.presence);
                state.progress.written_bytes = record.written_bytes.min(total_bytes);
            }
            restored.insert(key(&record.asset_id, &record.asset_revision), state);
        }
        self.states = restored;
        Ok(())
    }

    fn set_platform(&mut self, platform: wire::AssetPlatform) {
        self.policy.platform = translate::platform_from_wire(platform);
    }

    fn set_network(&mut self, cost: wire::AssetNetworkCost, metered_permitted: bool) {
        self.policy.network = translate::network_from_wire(cost);
        self.policy.metered_permitted = metered_permitted;
    }

    fn plan(&self, now_monotonic_ms: u64) -> Vec<wire::AssetDeliveryEffect> {
        let states = self.known_states();
        let mut effects: Vec<wire::AssetDeliveryEffect> =
            plan_startup(&self.catalog, &states, self.policy, now_monotonic_ms)
                .steps
                .iter()
                .filter_map(step_to_effect)
                .collect();
        for (asset_id, asset_revision) in &self.requested {
            if effects.len() >= asset_plane::defaults::MAX_CONCURRENT_TRANSFERS {
                break;
            }
            let step = plan_one_asset(
                &self.catalog,
                asset_id,
                asset_revision,
                self.states.get(&key(asset_id, asset_revision)),
                self.policy,
                now_monotonic_ms,
            );
            if let Some(effect) = step_to_effect(&step) {
                effects.push(effect);
            }
        }
        effects
    }

    fn catalog_position(&self, asset_id: &str, asset_revision: &str) -> Option<u32> {
        self.catalog
            .entries()
            .iter()
            .position(|entry| entry.id() == asset_id && entry.revision() == asset_revision)
            .and_then(|position| u32::try_from(position).ok())
    }

    fn completed_attempts(&self, asset_id: &str, asset_revision: &str) -> Option<u32> {
        self.require_cataloged(asset_id, asset_revision).ok()?;
        Some(
            self.states
                .get(&key(asset_id, asset_revision))
                .map_or(0, |state| state.attempts),
        )
    }

    fn request(&mut self, asset_id: &str, asset_revision: &str) -> Result<(), AssetDeliveryError> {
        self.require_cataloged(asset_id, asset_revision)?;
        let asset_key = key(asset_id, asset_revision);
        // The method call is the explicit request edge. Set insertion is not:
        // an exhausted on-demand asset is still retained in `requested`, so a
        // later request must restart its bounded series even when `insert`
        // reports that the identity was already present. Planning stays a
        // read, and therefore cannot restart an exhausted series by polling.
        if let Some(state) = self.states.get_mut(&asset_key) {
            *state = restart_retry_series(state);
        }
        self.requested.insert(asset_key);
        Ok(())
    }

    fn remove(
        &mut self,
        asset_id: &str,
        asset_revision: &str,
    ) -> Result<wire::AssetDeliveryEffect, AssetDeliveryError> {
        self.require_cataloged(asset_id, asset_revision)?;
        self.requested.remove(&key(asset_id, asset_revision));
        if let Some(state) = self.state_or_absent(asset_id, asset_revision) {
            let declined = advance(&state, TransferOutcome::Declined, 0, self.jitter.as_ref());
            self.apply(asset_id, asset_revision, declined);
        }
        Ok(wire::AssetDeliveryEffect {
            operation_kind: wire::AssetDeliveryOperation::RemoveAsset,
            fetch: None,
            remove: Some(wire::AssetRemoveRequest {
                asset_id: asset_id.to_owned(),
                asset_revision: asset_revision.to_owned(),
            }),
        })
    }

    fn record_transfer(
        &mut self,
        report: &wire::AssetTransferReport,
        now_monotonic_ms: u64,
    ) -> Result<(), AssetDeliveryError> {
        self.require_cataloged(&report.asset_id, &report.asset_revision)?;
        let before = self
            .state_or_absent(&report.asset_id, &report.asset_revision)
            .ok_or_else(|| AssetDeliveryError::UnexpectedReport {
                asset_id: report.asset_id.clone(),
            })?;
        let after = advance(
            &before,
            outcome_of(report),
            now_monotonic_ms,
            self.jitter.as_ref(),
        );
        if after.is_installed() {
            self.requested
                .remove(&key(&report.asset_id, &report.asset_revision));
        }
        self.apply(&report.asset_id, &report.asset_revision, after);
        Ok(())
    }

    fn record_removal(
        &mut self,
        report: &wire::AssetRemovalReport,
    ) -> Result<(), AssetDeliveryError> {
        self.require_cataloged(&report.asset_id, &report.asset_revision)?;
        self.states
            .remove(&key(&report.asset_id, &report.asset_revision));
        Ok(())
    }

    fn installations(&self) -> Vec<AssetInstallationView> {
        self.catalog
            .entries()
            .iter()
            .filter_map(|entry| {
                let variant = entry.variant(self.policy.platform)?;
                let state = self.states.get(&key(entry.id(), entry.revision()));
                let presence = state.map_or(Presence::Absent, |it| it.presence);
                let refusal = state.and_then(|it| it.refusal);
                Some(AssetInstallationView {
                    asset_id: entry.id().to_owned(),
                    asset_revision: entry.revision().to_owned(),
                    kind: translate::kind_to_wire(entry.kind()),
                    presence: translate::presence_to_wire(presence),
                    written_bytes: state.map_or(0, |it| it.progress.written_bytes),
                    total_bytes: variant.transfer_bytes(),
                    attempts: state.map_or(0, |it| it.attempts),
                    retry_after_monotonic_ms: state.map_or(0, |it| it.retry_after_monotonic_ms),
                    refusal: refusal.map(translate::refusal_to_wire),
                    refusal_retryable: refusal.is_some_and(RefusalReason::is_retryable),
                })
            })
            .collect()
    }

    fn model_artifact_registrations(
        &self,
    ) -> Result<Vec<wire::ModelArtifactRegistration>, AssetDeliveryError> {
        self.build_model_artifact_registrations()
    }

    fn posture(&self) -> AssetDeliveryPosture {
        AssetDeliveryPosture {
            // Any published variant at all, asked of the catalog rather than
            // of the platform enumeration: what makes a device supported is
            // that there are bytes for it, and a platform the product names
            // but has not built for yet is exactly a device with none.
            platform_supported: self
                .catalog
                .entries()
                .iter()
                .any(|entry| entry.variant(self.policy.platform).is_some()),
            network_cost: translate::network_to_wire(self.policy.network),
            metered_permitted: self.policy.metered_permitted,
        }
    }
}
