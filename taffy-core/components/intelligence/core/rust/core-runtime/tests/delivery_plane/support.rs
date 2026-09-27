// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Shared published-catalog fixture for the delivery-plane integration tests.

use asset_plane::catalog::{Container, Kind, Necessity, Publication};
use asset_plane::plan::NoJitter;
use asset_plane::{Catalog, CatalogEntry, Platform, Variant};
use core_runtime::adapters::assets::ProductionAssetDelivery;
use core_runtime::ports::{AssetDeliveryPort, AssetInstallationView};
use core_service_types as wire;

pub(super) const DIGEST_HEX: &str =
    "2222222222222222222222222222222222222222222222222222222222222222";
pub(super) const DIGEST: [u8; 32] = [0x22; 32];

pub(super) static STDLIB: &[Variant] = &[Variant::new(
    Platform::AndroidArm64,
    Publication::Published,
    "python/3.14.2/stdlib-android-arm64.zip",
    12_000_000,
    12_000_000,
    DIGEST_HEX,
)];

static UNPUBLISHED: &[Variant] = &[Variant::new(
    Platform::AndroidArm64,
    Publication::Unpublished,
    "",
    0,
    0,
    "",
)];

static ENTRIES: &[CatalogEntry] = &[
    CatalogEntry::new(
        "python-stdlib",
        "3.14.2",
        Kind::PythonStdlib,
        Necessity::Required,
        Container::Zip,
        None,
        STDLIB,
    ),
    CatalogEntry::new(
        "python-toolkit",
        "1",
        Kind::PythonPackages,
        Necessity::OnDemand,
        Container::Zip,
        None,
        UNPUBLISHED,
    ),
];

pub(super) fn port() -> ProductionAssetDelivery {
    let mut delivery = ProductionAssetDelivery::with_catalog(
        Catalog::from_entries(ENTRIES, 11),
        Box::new(NoJitter),
    );
    delivery.set_platform(wire::AssetPlatform::AndroidArm64);
    delivery.set_network(wire::AssetNetworkCost::Unmetered, false);
    delivery
}

pub(super) fn stdlib_row(delivery: &ProductionAssetDelivery) -> AssetInstallationView {
    delivery
        .installations()
        .into_iter()
        .find(|row| row.asset_id == "python-stdlib")
        .unwrap_or_else(|| unreachable!())
}

pub(super) fn report(
    outcome: wire::AssetTransferOutcome,
    written: u64,
) -> wire::AssetTransferReport {
    wire::AssetTransferReport {
        asset_id: "python-stdlib".to_owned(),
        asset_revision: "3.14.2".to_owned(),
        outcome,
        written_bytes: written,
        observed_bytes: written,
        observed_digest: DIGEST,
    }
}

pub(super) fn exhaust_required_asset_with_partial(delivery: &mut ProductionAssetDelivery) {
    const PARTIAL_BYTES: u64 = 4_000_000;
    let mut now = 0;
    for attempt in 0..asset_plane::defaults::MAX_TRANSFER_ATTEMPTS {
        let fetch = delivery
            .plan(now)
            .first()
            .and_then(|effect| effect.fetch.clone())
            .unwrap_or_else(|| unreachable!("attempt {attempt} was not planned"));
        assert_eq!(
            fetch.offset_bytes,
            if attempt == 0 { 0 } else { PARTIAL_BYTES }
        );
        delivery
            .record_transfer(
                &report(wire::AssetTransferOutcome::Interrupted, PARTIAL_BYTES),
                now,
            )
            .unwrap_or_else(|_| unreachable!());
        now = stdlib_row(delivery).retry_after_monotonic_ms;
    }
    let exhausted = stdlib_row(delivery);
    assert_eq!(
        exhausted.attempts,
        asset_plane::defaults::MAX_TRANSFER_ATTEMPTS
    );
    assert_eq!(
        exhausted.refusal,
        Some(wire::AssetRefusalReason::AttemptsExhausted)
    );
}
