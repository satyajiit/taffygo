// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The ordered runtime's delivery-plane surface.
//!
//! Every method here delegates to the installed [`AssetDeliveryPort`]. The
//! delegation exists because the port is a private field of
//! [`ServiceRuntimeComponents`] and the service adapter reaches the runtime,
//! not the components — the same shape the workspace and account subsystems
//! already have.
//!
//! [`AssetDeliveryPort`]: crate::ports::AssetDeliveryPort
//! [`ServiceRuntimeComponents`]: super::ServiceRuntimeComponents

use core_service_types as wire;

use crate::ports::{AssetDeliveryError, AssetInstallationView};

use super::CoreRuntime;

impl CoreRuntime {
    /// Adopts what the browser found on disk as this generation's start state.
    ///
    /// Called once, before anything is planned. A record for an asset this
    /// build's catalog does not name is refused rather than adopted: the
    /// alternative is a row nothing can ever finish, remove or explain.
    pub fn restore_assets(
        &mut self,
        found: &[wire::AssetOnDisk],
    ) -> Result<(), AssetDeliveryError> {
        self.components.assets.restore(found)
    }

    /// Records which build of the product this is.
    ///
    /// A platform the catalog has no published variant for is not an error; it
    /// is a device that fetches nothing, which is what an unpublished row means.
    pub fn set_asset_platform(&mut self, platform: wire::AssetPlatform) {
        self.components.assets.set_platform(platform);
    }

    /// Records what the connection costs and what a person allowed on it.
    pub fn set_asset_network(&mut self, cost: wire::AssetNetworkCost, metered_permitted: bool) {
        self.components.assets.set_network(cost, metered_permitted);
    }

    /// What the browser should do about assets right now, in order.
    ///
    /// Pure: planning twice at the same instant answers the same thing twice,
    /// and nothing is marked as begun until a report comes back.
    pub fn plan_asset_delivery(&self, now_monotonic_ms: u64) -> Vec<wire::AssetDeliveryEffect> {
        self.components.assets.plan(now_monotonic_ms)
    }

    /// Completed attempts for the exact catalogued asset.
    pub fn asset_completed_attempts(&self, asset_id: &str, asset_revision: &str) -> Option<u32> {
        self.components
            .assets
            .completed_attempts(asset_id, asset_revision)
    }

    /// Marks one on-demand asset as wanted. Start-up fetches the rest.
    pub fn request_asset(
        &mut self,
        asset_id: &str,
        asset_revision: &str,
    ) -> Result<(), AssetDeliveryError> {
        self.components.assets.request(asset_id, asset_revision)
    }

    /// Answers the effect that deletes one asset's bytes.
    pub fn remove_asset(
        &mut self,
        asset_id: &str,
        asset_revision: &str,
    ) -> Result<wire::AssetDeliveryEffect, AssetDeliveryError> {
        self.components.assets.remove(asset_id, asset_revision)
    }

    /// Advances one asset's state by what a transfer observed.
    pub fn record_asset_transfer(
        &mut self,
        report: &wire::AssetTransferReport,
        now_monotonic_ms: u64,
    ) -> Result<(), AssetDeliveryError> {
        self.components
            .assets
            .record_transfer(report, now_monotonic_ms)
    }

    /// Advances one asset's state by a removal the browser carried out.
    pub fn record_asset_removal(
        &mut self,
        report: &wire::AssetRemovalReport,
    ) -> Result<(), AssetDeliveryError> {
        self.components.assets.record_removal(report)
    }

    /// Every asset the plane is tracking, for a surface to show.
    pub fn asset_installations(&self) -> Vec<AssetInstallationView> {
        self.components.assets.installations()
    }

    /// Every installed model row the browser may attempt to register.
    ///
    /// The delivery adapter owns the catalog and the installed-state
    /// reconciliation, so this delegation keeps both behind that one deep
    /// interface rather than teaching state projection how catalog rows work.
    pub fn model_artifact_registrations(
        &self,
    ) -> Result<Vec<wire::ModelArtifactRegistration>, AssetDeliveryError> {
        self.components.assets.model_artifact_registrations()
    }
}
