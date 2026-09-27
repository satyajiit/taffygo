// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ordering and stable identity with more than one required asset.

use asset_plane::catalog::{Container, Kind, Necessity, Publication};
use asset_plane::plan::NoJitter;
use asset_plane::{Catalog, CatalogEntry, Platform, Variant};
use core_runtime::adapters::assets::ProductionAssetDelivery;
use core_runtime::ports::AssetDeliveryPort;
use core_service_types as wire;

use super::support::{DIGEST, DIGEST_HEX, STDLIB};

static FLAGS: &[Variant] = &[Variant::new(
    Platform::AndroidArm64,
    Publication::Published,
    "country-flags/7.5.0/flags-4x3-webp.zip",
    584_892,
    584_892,
    DIGEST_HEX,
)];

static TWO_REQUIRED: &[CatalogEntry] = &[
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
        "country-flags",
        "7.5.0",
        Kind::CountryFlags,
        Necessity::Required,
        Container::Zip,
        None,
        FLAGS,
    ),
];

fn two_required() -> ProductionAssetDelivery {
    let mut delivery = ProductionAssetDelivery::with_catalog(
        Catalog::from_entries(TWO_REQUIRED, 12),
        Box::new(NoJitter),
    );
    delivery.set_platform(wire::AssetPlatform::AndroidArm64);
    delivery.set_network(wire::AssetNetworkCost::Unmetered, false);
    delivery
}

fn planned_ids(delivery: &ProductionAssetDelivery, now: u64) -> Vec<String> {
    delivery
        .plan(now)
        .iter()
        .filter_map(|effect| effect.fetch.as_ref().map(|fetch| fetch.asset_id.clone()))
        .collect()
}

fn installed(asset_id: &str, asset_revision: &str, bytes: u64) -> wire::AssetTransferReport {
    wire::AssetTransferReport {
        asset_id: asset_id.to_owned(),
        asset_revision: asset_revision.to_owned(),
        outcome: wire::AssetTransferOutcome::Installed,
        written_bytes: bytes,
        observed_bytes: bytes,
        observed_digest: DIGEST,
    }
}

#[test]
fn the_second_required_asset_starts_once_the_first_one_is_installed() {
    let mut delivery = two_required();
    assert_eq!(
        planned_ids(&delivery, 0),
        vec!["python-stdlib".to_owned()],
        "one transfer at a time, in catalog order"
    );

    delivery
        .record_transfer(&installed("python-stdlib", "3.14.2", 12_000_000), 1_000)
        .unwrap_or_else(|_| unreachable!());

    assert_eq!(
        planned_ids(&delivery, 1_000),
        vec!["country-flags".to_owned()],
        "the next required artifact takes the freed slot"
    );
}

// Catalog order is a stable query for one loaded catalogue. Browser effect
// identity deliberately does not consume it: content, causation and attempt
// are the durable facts.
#[test]
fn each_catalog_row_has_its_own_stable_position() {
    let mut delivery = two_required();
    let python = delivery
        .catalog_position("python-stdlib", "3.14.2")
        .unwrap_or_else(|| unreachable!());
    let flags = delivery
        .catalog_position("country-flags", "7.5.0")
        .unwrap_or_else(|| unreachable!());
    assert_ne!(python, flags, "two rows, two names");

    delivery
        .record_transfer(&installed("python-stdlib", "3.14.2", 12_000_000), 1_000)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        delivery.catalog_position("country-flags", "7.5.0"),
        Some(flags)
    );
    assert_eq!(
        delivery.catalog_position("python-stdlib", "3.14.2"),
        Some(python)
    );
}

#[test]
fn every_planned_effect_names_a_row_the_catalog_has() {
    let delivery = two_required();
    for effect in delivery.plan(0) {
        let fetch = effect.fetch.as_ref().unwrap_or_else(|| unreachable!());
        assert!(
            delivery
                .catalog_position(&fetch.asset_id, &fetch.asset_revision)
                .is_some(),
            "{} is planned but not cataloged",
            fetch.asset_id
        );
    }
}
