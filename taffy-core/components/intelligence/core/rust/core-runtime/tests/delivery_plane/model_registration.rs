// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Catalog-to-browser model registration at the delivery port's interface.

use asset_plane::catalog::{
    ArtifactRole, Container, Kind, ModelFacts, ModelFormat, Necessity, Publication,
};
use asset_plane::plan::NoJitter;
use asset_plane::{Catalog, CatalogEntry, Platform, Variant};
use core_runtime::adapters::assets::ProductionAssetDelivery;
use core_runtime::ports::{AssetDeliveryError, AssetDeliveryPort};
use core_service_types as wire;

const MODEL_DIGEST: [u8; 32] = [0x33; 32];
const ADAPTER_DIGEST: [u8; 32] = [0x44; 32];
const TOKENIZER_DIGEST: [u8; 32] = [0x55; 32];

static MODEL_VARIANT: &[Variant] = &[Variant::new(
    Platform::AndroidArm64,
    Publication::Published,
    "models/base.gguf",
    9,
    9,
    "3333333333333333333333333333333333333333333333333333333333333333",
)];
static ADAPTER_VARIANT: &[Variant] = &[Variant::new(
    Platform::AndroidArm64,
    Publication::Published,
    "models/adapter.gguf",
    4,
    4,
    "4444444444444444444444444444444444444444444444444444444444444444",
)];
static TOKENIZER_VARIANT: &[Variant] = &[Variant::new(
    Platform::AndroidArm64,
    Publication::Published,
    "models/tokenizer.bin",
    5,
    5,
    "5555555555555555555555555555555555555555555555555555555555555555",
)];

static MODEL_ROWS: &[CatalogEntry] = &[
    CatalogEntry::new(
        "base-model",
        "1",
        Kind::ModelWeights,
        Necessity::OnDemand,
        Container::Raw,
        Some(ModelFacts::new(ModelFormat::Gguf, ArtifactRole::Whole)),
        MODEL_VARIANT,
    ),
    CatalogEntry::new(
        "base-adapter",
        "1",
        Kind::ModelWeights,
        Necessity::OnDemand,
        Container::Raw,
        Some(ModelFacts::new(ModelFormat::Gguf, ArtifactRole::Adapter)),
        ADAPTER_VARIANT,
    ),
    CatalogEntry::new(
        "base-tokenizer",
        "1",
        Kind::ModelTokenizer,
        Necessity::OnDemand,
        Container::Raw,
        Some(ModelFacts::new(ModelFormat::Gguf, ArtifactRole::Whole)),
        TOKENIZER_VARIANT,
    ),
];

fn port(entries: &'static [CatalogEntry]) -> ProductionAssetDelivery {
    let mut delivery = ProductionAssetDelivery::with_catalog(
        Catalog::from_entries(entries, 17),
        Box::new(NoJitter),
    );
    delivery.set_platform(wire::AssetPlatform::AndroidArm64);
    delivery
}

fn installed(id: &str, bytes: u64) -> wire::AssetOnDisk {
    wire::AssetOnDisk {
        asset_id: id.to_owned(),
        asset_revision: "1".to_owned(),
        presence: wire::AssetPresence::Installed,
        written_bytes: bytes,
    }
}

#[test]
fn exact_installed_rows_export_every_catalog_fact_and_no_path() {
    let mut delivery = port(MODEL_ROWS);
    delivery
        .restore(&[
            installed("base-model", 9),
            installed("base-adapter", 4),
            installed("base-tokenizer", 5),
        ])
        .unwrap_or_else(|_| unreachable!());

    let registrations = delivery
        .model_artifact_registrations()
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(registrations.len(), 3);
    let model = registrations.first().unwrap_or_else(|| unreachable!());
    assert_eq!(model.asset_id, "base-model");
    assert_eq!(model.asset_revision, "1");
    assert_eq!(model.asset_kind, wire::AssetKind::ModelWeights);
    assert_eq!(model.format, wire::ToolModelArtifactKind::Gguf);
    assert!(!model.adapter);
    assert_eq!(model.byte_length, 9);
    assert_eq!(model.digest, MODEL_DIGEST);

    let adapter = registrations.get(1).unwrap_or_else(|| unreachable!());
    assert!(adapter.adapter);
    assert_eq!(adapter.digest, ADAPTER_DIGEST);
    let tokenizer = registrations.get(2).unwrap_or_else(|| unreachable!());
    assert_eq!(tokenizer.asset_kind, wire::AssetKind::ModelTokenizer);
    assert!(!tokenizer.adapter);
    assert_eq!(tokenizer.digest, TOKENIZER_DIGEST);
}

#[test]
fn an_installed_name_with_the_wrong_length_is_not_a_registration() {
    let mut delivery = port(MODEL_ROWS);
    delivery
        .restore(&[installed("base-model", 8)])
        .unwrap_or_else(|_| unreachable!());

    assert!(!delivery.is_installed("base-model", "1"));
    assert!(delivery
        .model_artifact_registrations()
        .unwrap_or_else(|_| unreachable!())
        .is_empty());
}

#[test]
fn an_uninstalled_or_partial_model_exports_nothing() {
    let mut delivery = port(MODEL_ROWS);
    delivery
        .restore(&[wire::AssetOnDisk {
            presence: wire::AssetPresence::Partial,
            ..installed("base-model", 4)
        }])
        .unwrap_or_else(|_| unreachable!());
    assert!(delivery
        .model_artifact_registrations()
        .unwrap_or_else(|_| unreachable!())
        .is_empty());
}

#[test]
fn malformed_model_catalog_facts_fail_the_complete_snapshot() {
    static BAD_ROWS: &[CatalogEntry] = &[CatalogEntry::new(
        "not-a-model",
        "1",
        Kind::CountryFlags,
        Necessity::OnDemand,
        Container::Raw,
        Some(ModelFacts::new(ModelFormat::Gguf, ArtifactRole::Whole)),
        MODEL_VARIANT,
    )];
    let mut delivery = port(BAD_ROWS);
    delivery
        .restore(&[installed("not-a-model", 9)])
        .unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        delivery.model_artifact_registrations(),
        Err(AssetDeliveryError::InvalidModelArtifact { .. })
    ));
}

#[test]
fn an_adapter_role_on_a_tokenizer_fails_closed() {
    static BAD_ROWS: &[CatalogEntry] = &[CatalogEntry::new(
        "bad-tokenizer",
        "1",
        Kind::ModelTokenizer,
        Necessity::OnDemand,
        Container::Raw,
        Some(ModelFacts::new(ModelFormat::Gguf, ArtifactRole::Adapter)),
        TOKENIZER_VARIANT,
    )];
    let mut delivery = port(BAD_ROWS);
    delivery
        .restore(&[installed("bad-tokenizer", 5)])
        .unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        delivery.model_artifact_registrations(),
        Err(AssetDeliveryError::InvalidModelArtifact { .. })
    ));
}
