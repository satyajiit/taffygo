// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The delivery plane, as a surface sees it.
//!
//! Every value here is carried across unchanged. The delivery vocabulary is
//! declared twice — once in the Core Service contract the browser reports over
//! and once in the Core API contract a surface reads — and
//! `contracts/codegen/cross_contracts.py` holds the two sets equal member for
//! member, so the matches below are renames a checker can prove rather than
//! judgements this file is making.
//!
//! That is deliberate. The reason a person is shown for an artifact that did
//! not arrive has to be the reason it did not arrive, and a projection allowed
//! to summarise, round or soften a verdict on the way out is a projection that
//! can tell them something the product never concluded.

use core_api_types::{
    AssetDeliveryView, AssetKindView, AssetNetworkCostView, AssetPresenceView, AssetRefusal,
    AssetRefusalView, AssetViewState, MAX_ASSETS,
};
use core_service_types as wire;

use crate::ports::{AssetDeliveryPosture, AssetInstallationView};

/// The delivery plane's whole outward state, or nothing.
///
/// `None` when the catalog carries more rows than the contract's bound, which
/// is a build defect rather than a device condition: the alternative is
/// truncating the list, and a person told about nine of ten artifacts has no
/// way to notice the tenth is missing.
pub fn project_asset_delivery(
    posture: AssetDeliveryPosture,
    installations: Vec<AssetInstallationView>,
) -> Option<AssetDeliveryView> {
    if installations.len() > MAX_ASSETS {
        return None;
    }
    Some(AssetDeliveryView {
        platform_supported: posture.platform_supported,
        network_cost: network_cost(posture.network_cost),
        metered_permitted: posture.metered_permitted,
        assets: installations.into_iter().map(asset).collect(),
    })
}

fn asset(state: AssetInstallationView) -> AssetViewState {
    AssetViewState {
        asset_id: state.asset_id,
        asset_revision: state.asset_revision,
        kind: kind(state.kind),
        presence: presence(state.presence),
        written_bytes: state.written_bytes,
        total_bytes: state.total_bytes,
        attempts: state.attempts,
        refusal: state.refusal.map(|reason| AssetRefusal {
            reason: refusal(reason),
            retryable: state.refusal_retryable,
        }),
        waiting_until_monotonic_ms: state.retry_after_monotonic_ms,
    }
}

fn kind(value: wire::AssetKind) -> AssetKindView {
    match value {
        wire::AssetKind::PythonStdlib => AssetKindView::PythonStdlib,
        wire::AssetKind::PythonPackages => AssetKindView::PythonPackages,
        wire::AssetKind::ModelWeights => AssetKindView::ModelWeights,
        wire::AssetKind::ModelTokenizer => AssetKindView::ModelTokenizer,
        wire::AssetKind::FilterList => AssetKindView::FilterList,
        wire::AssetKind::CountryFlags => AssetKindView::CountryFlags,
        wire::AssetKind::StartScenes => AssetKindView::StartScenes,
    }
}

fn presence(value: wire::AssetPresence) -> AssetPresenceView {
    match value {
        wire::AssetPresence::Absent => AssetPresenceView::Absent,
        wire::AssetPresence::Partial => AssetPresenceView::Partial,
        wire::AssetPresence::Complete => AssetPresenceView::Complete,
        wire::AssetPresence::Installed => AssetPresenceView::Installed,
    }
}

fn network_cost(value: wire::AssetNetworkCost) -> AssetNetworkCostView {
    match value {
        wire::AssetNetworkCost::Offline => AssetNetworkCostView::Offline,
        wire::AssetNetworkCost::Metered => AssetNetworkCostView::Metered,
        wire::AssetNetworkCost::Unmetered => AssetNetworkCostView::Unmetered,
    }
}

fn refusal(value: wire::AssetRefusalReason) -> AssetRefusalView {
    match value {
        wire::AssetRefusalReason::UnknownAsset => AssetRefusalView::UnknownAsset,
        wire::AssetRefusalReason::NoVariantForPlatform => AssetRefusalView::NoVariantForPlatform,
        wire::AssetRefusalReason::NotPublishedYet => AssetRefusalView::NotPublishedYet,
        wire::AssetRefusalReason::CatalogRowIncomplete => AssetRefusalView::CatalogRowIncomplete,
        wire::AssetRefusalReason::VariantTooLarge => AssetRefusalView::VariantTooLarge,
        wire::AssetRefusalReason::AttemptsExhausted => AssetRefusalView::AttemptsExhausted,
        wire::AssetRefusalReason::IntegrityFailed => AssetRefusalView::IntegrityFailed,
        wire::AssetRefusalReason::NetworkNotPermitted => AssetRefusalView::NetworkNotPermitted,
        wire::AssetRefusalReason::DeclinedByPerson => AssetRefusalView::DeclinedByPerson,
    }
}

#[cfg(test)]
mod tests {
    use super::{project_asset_delivery, AssetDeliveryPosture, AssetInstallationView};
    use core_api_types::{AssetPresenceView, AssetRefusalView, MAX_ASSETS};
    use core_service_types as wire;

    fn posture(platform_supported: bool) -> AssetDeliveryPosture {
        AssetDeliveryPosture {
            platform_supported,
            network_cost: wire::AssetNetworkCost::Metered,
            metered_permitted: false,
        }
    }

    fn installation(index: usize) -> AssetInstallationView {
        AssetInstallationView {
            asset_id: format!("asset-{index}"),
            asset_revision: "1".to_owned(),
            kind: wire::AssetKind::PythonStdlib,
            presence: wire::AssetPresence::Partial,
            written_bytes: 1,
            total_bytes: 2,
            attempts: 3,
            retry_after_monotonic_ms: 4,
            refusal: Some(wire::AssetRefusalReason::NetworkNotPermitted),
            refusal_retryable: true,
        }
    }

    #[test]
    fn a_refusal_reaches_the_surface_as_the_verdict_the_plane_reached() {
        let view = project_asset_delivery(posture(true), vec![installation(0)])
            .expect("one asset is within the bound");
        let asset = view.assets.first().expect("the one asset projected");
        let refusal = asset.refusal.as_ref().expect("the refusal carried across");
        assert_eq!(refusal.reason, AssetRefusalView::NetworkNotPermitted);
        assert!(refusal.retryable);
        assert_eq!(asset.presence, AssetPresenceView::Partial);
        assert_eq!(asset.waiting_until_monotonic_ms, 4);
        assert_eq!(view.network_cost, super::AssetNetworkCostView::Metered);
        assert!(!view.metered_permitted);
    }

    #[test]
    fn retryability_is_the_planes_answer_and_not_recomputed_here() {
        // The same reason, reported as unretryable. A projection that decided
        // for itself would contradict the plane and draw a control that does
        // nothing; this one carries whatever it was handed.
        let mut state = installation(0);
        state.refusal_retryable = false;
        let view = project_asset_delivery(posture(true), vec![state]).expect("within the bound");
        let asset = view.assets.first().expect("the one asset projected");
        assert!(!asset.refusal.as_ref().expect("a refusal").retryable);
    }

    #[test]
    fn a_device_the_catalog_publishes_nothing_for_says_so_rather_than_looking_idle() {
        let view = project_asset_delivery(posture(false), Vec::new()).expect("an empty list fits");
        assert!(!view.platform_supported);
        assert!(view.assets.is_empty());
    }

    #[test]
    fn more_assets_than_the_contract_bounds_project_nothing_at_all() {
        let over = (0..=MAX_ASSETS).map(installation).collect::<Vec<_>>();
        assert!(project_asset_delivery(posture(true), over).is_none());
        let exact = (0..MAX_ASSETS).map(installation).collect::<Vec<_>>();
        assert!(project_asset_delivery(posture(true), exact).is_some());
    }
}
