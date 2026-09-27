// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Between the delivery crate's vocabulary and the Core Service contract's.
//!
//! Two vocabularies for one subject is a cost, and it is paid deliberately.
//! The crate's types are what a policy is written in and are free to change
//! with the policy; the contract's are frozen, because a device that already
//! wrote a state to disk must still be able to read it. Translating in one
//! file is what stops the two drifting into each other.

use asset_plane::catalog::Kind;
use asset_plane::plan::NetworkCost;
use asset_plane::{Container, ModelFormat, Platform, Presence, RefusalReason};
use core_service_types as wire;

/// The platform a bootstrap named.
pub fn platform_from_wire(value: wire::AssetPlatform) -> Platform {
    match value {
        wire::AssetPlatform::AndroidArm64 => Platform::AndroidArm64,
        wire::AssetPlatform::AndroidX64 => Platform::AndroidX64,
        wire::AssetPlatform::MacosArm64 => Platform::MacosArm64,
        wire::AssetPlatform::MacosX64 => Platform::MacosX64,
        wire::AssetPlatform::WindowsX64 => Platform::WindowsX64,
        wire::AssetPlatform::WindowsArm64 => Platform::WindowsArm64,
        wire::AssetPlatform::Unsupported => Platform::Unsupported,
    }
}

/// What the connection costs.
pub fn network_from_wire(value: wire::AssetNetworkCost) -> NetworkCost {
    match value {
        wire::AssetNetworkCost::Offline => NetworkCost::Offline,
        wire::AssetNetworkCost::Metered => NetworkCost::Metered,
        wire::AssetNetworkCost::Unmetered => NetworkCost::Unmetered,
    }
}

/// What an asset is for.
pub fn network_to_wire(value: NetworkCost) -> wire::AssetNetworkCost {
    match value {
        NetworkCost::Offline => wire::AssetNetworkCost::Offline,
        NetworkCost::Metered => wire::AssetNetworkCost::Metered,
        NetworkCost::Unmetered => wire::AssetNetworkCost::Unmetered,
    }
}

pub fn kind_to_wire(value: Kind) -> wire::AssetKind {
    match value {
        Kind::PythonStdlib => wire::AssetKind::PythonStdlib,
        Kind::PythonPackages => wire::AssetKind::PythonPackages,
        Kind::ModelWeights => wire::AssetKind::ModelWeights,
        Kind::ModelTokenizer => wire::AssetKind::ModelTokenizer,
        Kind::FilterList => wire::AssetKind::FilterList,
        Kind::CountryFlags => wire::AssetKind::CountryFlags,
        Kind::StartScenes => wire::AssetKind::StartScenes,
    }
}

/// The runtime format a model catalog row names.
pub fn model_format_to_wire(value: ModelFormat) -> wire::ToolModelArtifactKind {
    match value {
        ModelFormat::LitertTflite => wire::ToolModelArtifactKind::LitertTflite,
        ModelFormat::OnnxRuntime => wire::ToolModelArtifactKind::OnnxRuntime,
        ModelFormat::Gguf => wire::ToolModelArtifactKind::Gguf,
    }
}

/// What the transferred bytes are.
pub fn container_to_wire(value: Container) -> wire::AssetContainer {
    match value {
        Container::Raw => wire::AssetContainer::Raw,
        Container::Zip => wire::AssetContainer::Zip,
    }
}

/// How much of an asset is on the device.
pub fn presence_to_wire(value: Presence) -> wire::AssetPresence {
    match value {
        Presence::Absent => wire::AssetPresence::Absent,
        Presence::Partial => wire::AssetPresence::Partial,
        Presence::Complete => wire::AssetPresence::Complete,
        Presence::Installed => wire::AssetPresence::Installed,
    }
}

/// The same, the other way, for a state a device wrote before this build ran.
pub fn presence_from_wire(value: wire::AssetPresence) -> Presence {
    match value {
        wire::AssetPresence::Absent => Presence::Absent,
        wire::AssetPresence::Partial => Presence::Partial,
        wire::AssetPresence::Complete => Presence::Complete,
        wire::AssetPresence::Installed => Presence::Installed,
    }
}

/// Why the plane will not install an asset.
pub fn refusal_to_wire(value: RefusalReason) -> wire::AssetRefusalReason {
    match value {
        RefusalReason::UnknownAsset => wire::AssetRefusalReason::UnknownAsset,
        RefusalReason::NoVariantForPlatform => wire::AssetRefusalReason::NoVariantForPlatform,
        RefusalReason::NotPublishedYet => wire::AssetRefusalReason::NotPublishedYet,
        RefusalReason::CatalogRowIncomplete => wire::AssetRefusalReason::CatalogRowIncomplete,
        RefusalReason::VariantTooLarge => wire::AssetRefusalReason::VariantTooLarge,
        RefusalReason::AttemptsExhausted => wire::AssetRefusalReason::AttemptsExhausted,
        RefusalReason::IntegrityFailed => wire::AssetRefusalReason::IntegrityFailed,
        RefusalReason::NetworkNotPermitted => wire::AssetRefusalReason::NetworkNotPermitted,
        RefusalReason::DeclinedByPerson => wire::AssetRefusalReason::DeclinedByPerson,
    }
}
