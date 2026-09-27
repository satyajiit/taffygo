// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The delivery port, driven the way the service bridge will drive it.
//!
//! The product catalog publishes nothing yet — every variant is `unpublished`
//! until the artifacts are built — so a test against it could only assert
//! refusals. These entries are what a published catalog looks like. Building
//! one here is also the proof of the property the design is for: the adapter
//! has never seen these rows and answers every question about them, because a
//! new asset is data rather than code.

#[path = "delivery_plane/model_registration.rs"]
mod model_registration;
#[path = "delivery_plane/multiple_required.rs"]
mod multiple_required;
#[path = "delivery_plane/support.rs"]
mod support;

use asset_plane::plan::Jitter;
use core_runtime::adapters::assets::GenerationJitter;
use core_runtime::ports::{AssetDeliveryError, AssetDeliveryPort};
use core_service_types as wire;

use support::{exhaust_required_asset_with_partial, port, report, stdlib_row, DIGEST};

#[test]
fn a_fresh_profile_plans_the_required_asset_and_names_the_bytes_it_must_get() {
    let effects = port().plan(0);
    assert_eq!(effects.len(), 1);
    let effect = effects.first().unwrap_or_else(|| unreachable!());
    assert!(effect.has_valid_body());
    assert_eq!(
        effect.operation_kind,
        wire::AssetDeliveryOperation::FetchAsset
    );
    let fetch = effect.fetch.as_ref().unwrap_or_else(|| unreachable!());
    assert_eq!(fetch.asset_id, "python-stdlib");
    assert_eq!(fetch.origin_path, "python/3.14.2/stdlib-android-arm64.zip");
    assert_eq!(fetch.offset_bytes, 0);
    assert_eq!(fetch.total_bytes, 12_000_000);
    assert_eq!(fetch.expected_digest, DIGEST);
    assert_eq!(fetch.container, wire::AssetContainer::Zip);
}

#[test]
fn a_metered_connection_still_plans_the_required_asset() {
    let mut delivery = port();
    delivery.set_network(wire::AssetNetworkCost::Metered, false);
    let effects = delivery.plan(0);
    assert_eq!(effects.len(), 1);
    let fetch = effects
        .first()
        .and_then(|effect| effect.fetch.as_ref())
        .unwrap_or_else(|| unreachable!());
    assert_eq!(fetch.asset_id, "python-stdlib");
}

#[test]
fn an_offline_device_plans_nothing_and_still_reports_the_asset() {
    let mut delivery = port();
    delivery.set_network(wire::AssetNetworkCost::Offline, false);
    assert!(delivery.plan(0).is_empty());
    // The refusal is a state, not an effect: it reaches a person through the
    // installation list, which is what the surface reads. Every catalog row
    // with bytes for this platform is listed — including the unpublished one,
    // so a surface can say "not available yet" rather than hide it.
    let installations = delivery.installations();
    assert_eq!(installations.len(), 2);
    let row = installations
        .iter()
        .find(|row| row.asset_id == "python-stdlib")
        .unwrap_or_else(|| unreachable!());
    assert_eq!(row.presence, wire::AssetPresence::Absent);
    assert_eq!(row.refusal, None, "nothing has been attempted yet");
    assert_eq!(row.total_bytes, 12_000_000);
    let unpublished = installations
        .iter()
        .find(|row| row.asset_id == "python-toolkit")
        .unwrap_or_else(|| unreachable!());
    assert_eq!(
        unpublished.total_bytes, 0,
        "an unpublished row names no length"
    );
}

#[test]
fn an_interrupted_transfer_resumes_from_its_offset_after_the_backoff() {
    let mut delivery = port();
    delivery
        .record_transfer(
            &report(wire::AssetTransferOutcome::Interrupted, 4_000_000),
            1_000,
        )
        .unwrap_or_else(|_| unreachable!());

    // Still inside the backoff: nothing is asked for.
    assert!(delivery.plan(1_001).is_empty());

    let later = stdlib_row(&delivery).retry_after_monotonic_ms;
    let effects = delivery.plan(later);
    let fetch = effects
        .first()
        .and_then(|effect| effect.fetch.as_ref())
        .unwrap_or_else(|| unreachable!());
    assert_eq!(fetch.offset_bytes, 4_000_000);
}

#[test]
fn an_explicit_retry_after_exhaustion_starts_at_attempt_one() {
    let mut delivery = port();
    delivery
        .request("python-stdlib", "3.14.2")
        .unwrap_or_else(|_| unreachable!());
    exhaust_required_asset_with_partial(&mut delivery);

    // This is a second request edge for an identity already retained in the
    // adapter's persistent requested set.
    delivery
        .request("python-stdlib", "3.14.2")
        .unwrap_or_else(|_| unreachable!());
    let restarted = stdlib_row(&delivery);
    assert_eq!(restarted.attempts, 0);
    assert_eq!(restarted.refusal, None);

    delivery
        .record_transfer(
            &report(wire::AssetTransferOutcome::Interrupted, 4_000_000),
            10_000,
        )
        .unwrap_or_else(|_| unreachable!());
    let failed = stdlib_row(&delivery);
    assert_eq!(failed.attempts, 1);
    assert_eq!(failed.refusal, None);
}

#[test]
fn repeated_planning_does_not_restart_an_exhausted_series() {
    let mut delivery = port();
    delivery
        .request("python-stdlib", "3.14.2")
        .unwrap_or_else(|_| unreachable!());
    exhaust_required_asset_with_partial(&mut delivery);

    for now in [0, 10_000, u64::MAX] {
        assert!(delivery.plan(now).is_empty());
        let exhausted = stdlib_row(&delivery);
        assert_eq!(
            exhausted.attempts,
            asset_plane::defaults::MAX_TRANSFER_ATTEMPTS
        );
        assert_eq!(
            exhausted.refusal,
            Some(wire::AssetRefusalReason::AttemptsExhausted)
        );
    }
}

#[test]
fn an_explicit_retry_preserves_partial_bytes() {
    let mut delivery = port();
    delivery
        .request("python-stdlib", "3.14.2")
        .unwrap_or_else(|_| unreachable!());
    exhaust_required_asset_with_partial(&mut delivery);

    delivery
        .request("python-stdlib", "3.14.2")
        .unwrap_or_else(|_| unreachable!());
    let fetch = delivery
        .plan(0)
        .first()
        .and_then(|effect| effect.fetch.clone())
        .unwrap_or_else(|| unreachable!());
    assert_eq!(fetch.offset_bytes, 4_000_000);
}

#[test]
fn a_wrong_digest_starts_over_rather_than_resuming_a_file_it_cannot_trust() {
    let mut delivery = port();
    delivery
        .record_transfer(
            &report(wire::AssetTransferOutcome::IntegrityWrongDigest, 12_000_000),
            0,
        )
        .unwrap_or_else(|_| unreachable!());
    let row = stdlib_row(&delivery);
    assert_eq!(row.presence, wire::AssetPresence::Absent);
    assert_eq!(row.written_bytes, 0);
    assert_eq!(row.refusal, Some(wire::AssetRefusalReason::IntegrityFailed));
}

#[test]
fn an_installed_asset_is_planned_no_further_and_reads_as_installed() {
    let mut delivery = port();
    for outcome in [
        wire::AssetTransferOutcome::IntegritySound,
        wire::AssetTransferOutcome::Installed,
    ] {
        delivery
            .record_transfer(&report(outcome, 12_000_000), 0)
            .unwrap_or_else(|_| unreachable!());
    }
    assert!(delivery.plan(0).is_empty());
    assert!(delivery.is_installed("python-stdlib", "3.14.2"));
    let row = stdlib_row(&delivery);
    assert_eq!(row.presence, wire::AssetPresence::Installed);
    assert_eq!(row.written_bytes, 12_000_000);
}

#[test]
fn an_asset_the_catalog_does_not_carry_is_refused_rather_than_invented() {
    let mut delivery = port();
    assert_eq!(
        delivery.request("not-an-asset", "1"),
        Err(AssetDeliveryError::UnknownAsset {
            asset_id: "not-an-asset".to_owned(),
            asset_revision: "1".to_owned(),
        })
    );
    assert!(matches!(
        delivery.request("Not An Asset", "1"),
        Err(AssetDeliveryError::MalformedIdentity { .. })
    ));
}

#[test]
fn an_unpublished_row_is_never_fetched_however_hard_it_is_asked_for() {
    let mut delivery = port();
    delivery
        .request("python-toolkit", "1")
        .unwrap_or_else(|_| unreachable!());
    let effects = delivery.plan(0);
    assert!(
        effects.iter().all(|effect| effect
            .fetch
            .as_ref()
            .is_none_or(|f| f.asset_id != "python-toolkit")),
        "an unpublished variant was planned"
    );
}

#[test]
fn a_restore_refuses_a_record_this_build_does_not_recognise() {
    let mut delivery = port();
    let stranger = wire::AssetOnDisk {
        asset_id: "gone-from-the-catalog".to_owned(),
        asset_revision: "1".to_owned(),
        presence: wire::AssetPresence::Installed,
        written_bytes: 10,
    };
    assert!(matches!(
        delivery.restore(&[stranger]),
        Err(AssetDeliveryError::UnknownAsset { .. })
    ));
}

#[test]
fn a_restored_partial_transfer_resumes_where_the_browser_says_it_stopped() {
    let mut delivery = port();
    delivery
        .restore(&[wire::AssetOnDisk {
            asset_id: "python-stdlib".to_owned(),
            asset_revision: "3.14.2".to_owned(),
            presence: wire::AssetPresence::Partial,
            written_bytes: 7_500_000,
        }])
        .unwrap_or_else(|_| unreachable!());
    let fetch = delivery
        .plan(0)
        .first()
        .and_then(|effect| effect.fetch.clone())
        .unwrap_or_else(|| unreachable!());
    assert_eq!(fetch.offset_bytes, 7_500_000);
}

#[test]
fn removing_an_asset_asks_the_browser_to_delete_it_and_forgets_it_afterwards() {
    let mut delivery = port();
    let effect = delivery
        .remove("python-stdlib", "3.14.2")
        .unwrap_or_else(|_| unreachable!());
    assert!(effect.has_valid_body());
    assert_eq!(
        effect.operation_kind,
        wire::AssetDeliveryOperation::RemoveAsset
    );
    assert!(
        delivery.plan(0).is_empty(),
        "a declined asset is not re-fetched"
    );

    delivery
        .record_removal(&wire::AssetRemovalReport {
            asset_id: "python-stdlib".to_owned(),
            asset_revision: "3.14.2".to_owned(),
            reclaimed_bytes: 12_000_000,
        })
        .unwrap_or_else(|_| unreachable!());
    let row = stdlib_row(&delivery);
    assert_eq!(row.presence, wire::AssetPresence::Absent);
    assert_eq!(
        row.refusal, None,
        "removal forgets the decline rather than keeping it"
    );
}

#[test]
fn generation_jitter_is_a_fraction_and_differs_between_generations() {
    let quiet = GenerationJitter::new([0u8; 32]);
    let loud = GenerationJitter::new([255u8; 32]);
    for attempt in 0..8 {
        assert_eq!(quiet.fraction_percent(attempt), 0);
        assert_eq!(loud.fraction_percent(attempt), 100);
    }
    let mixed = GenerationJitter::new(core::array::from_fn(|index| {
        u8::try_from(index * 8 % 256).unwrap_or(0)
    }));
    let values: Vec<u32> = (0..8).map(|a| mixed.fraction_percent(a)).collect();
    assert!(values.iter().all(|value| *value <= 100));
    assert!(
        values.windows(2).any(|pair| pair.first() != pair.get(1)),
        "jitter did not vary across attempts"
    );
}
