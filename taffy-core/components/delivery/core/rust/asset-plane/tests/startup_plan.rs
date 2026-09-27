// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Start-up planning against a catalog built for the test.
//!
//! The product catalog publishes nothing yet, so a test that used it could only
//! ever assert refusals. These entries are what a published catalog looks like,
//! which is the only way to assert the half of the plane that fetches — and
//! building one here rather than in the crate is also the proof that a
//! consumer needs no code of the catalog's: `Catalog::from_entries` takes rows
//! it has never seen and answers every question about them.

use asset_plane::catalog::{
    ArtifactRole, Container, Kind, ModelFacts, ModelFormat, Necessity, Publication,
};
use asset_plane::plan::{
    plan_one_asset, plan_startup, DevicePolicy, InstallStep, NetworkCost, NoJitter,
};
use asset_plane::{AssetState, Catalog, CatalogEntry, Platform, RefusalReason, Variant};

const DIGEST: &str = "1111111111111111111111111111111111111111111111111111111111111111";

static PUBLISHED: &[Variant] = &[
    Variant::new(
        Platform::AndroidArm64,
        Publication::Published,
        "python/3.14.2/stdlib-android-arm64.zip",
        1_000,
        1_000,
        DIGEST,
    ),
    Variant::new(Platform::MacosArm64, Publication::Unpublished, "", 0, 0, ""),
];

static ON_DEMAND: &[Variant] = &[Variant::new(
    Platform::AndroidArm64,
    Publication::Published,
    "python/1/toolkit-android-arm64.zip",
    500,
    500,
    DIGEST,
)];

static ENTRIES: &[CatalogEntry] = &[
    CatalogEntry::new(
        "python-stdlib",
        "3.14.2",
        Kind::PythonStdlib,
        Necessity::Required,
        Container::Zip,
        None,
        PUBLISHED,
    ),
    CatalogEntry::new(
        "python-toolkit",
        "1",
        Kind::PythonPackages,
        Necessity::OnDemand,
        Container::Zip,
        None,
        ON_DEMAND,
    ),
];

fn catalog() -> Catalog {
    Catalog::from_entries(ENTRIES, 7)
}

fn policy(network: NetworkCost, metered_permitted: bool) -> DevicePolicy {
    DevicePolicy {
        platform: Platform::AndroidArm64,
        network,
        metered_permitted,
    }
}

#[test]
fn a_fresh_profile_fetches_what_is_required_and_leaves_the_rest() {
    let plan = plan_startup(&catalog(), &[], policy(NetworkCost::Unmetered, false), 0);
    assert_eq!(plan.transfer_count(), 1);
    let Some(InstallStep::Fetch {
        id,
        path,
        offset,
        total_bytes,
        ..
    }) = plan.steps.first()
    else {
        unreachable!("expected a fetch, got {:?}", plan.steps)
    };
    assert_eq!(id, "python-stdlib");
    assert_eq!(path, "python/3.14.2/stdlib-android-arm64.zip");
    assert_eq!(*offset, 0);
    assert_eq!(*total_bytes, 1_000);
}

#[test]
fn a_live_connection_may_transfer_and_offline_may_not() {
    assert!(policy(NetworkCost::Metered, false).may_transfer());
    assert!(policy(NetworkCost::Unmetered, false).may_transfer());
    assert!(!policy(NetworkCost::Offline, true).may_transfer());
}

#[test]
fn a_required_asset_fetches_on_a_live_connection_without_being_asked() {
    let plan = plan_startup(&catalog(), &[], policy(NetworkCost::Metered, false), 0);
    assert_eq!(plan.transfer_count(), 1);
    let Some(InstallStep::Fetch { id, .. }) = plan.steps.first() else {
        unreachable!("expected a fetch, got {:?}", plan.steps)
    };
    assert_eq!(id, "python-stdlib");
}

#[test]
fn an_on_demand_asset_fetches_on_a_live_connection_when_asked_for() {
    let asked = plan_one_asset(
        &catalog(),
        "python-toolkit",
        "1",
        None,
        policy(NetworkCost::Metered, false),
        0,
    );
    assert!(matches!(asked, InstallStep::Fetch { .. }));
}

#[test]
fn being_offline_refuses_rather_than_failing() {
    let plan = plan_startup(&catalog(), &[], policy(NetworkCost::Offline, true), 0);
    assert!(matches!(
        plan.steps.first(),
        Some(InstallStep::Refuse {
            reason: RefusalReason::NetworkNotPermitted,
            ..
        })
    ));
}

#[test]
fn a_partial_transfer_resumes_from_where_it_stopped() {
    let entry = ENTRIES.first().unwrap_or_else(|| unreachable!());
    let variant = entry
        .variant(Platform::AndroidArm64)
        .unwrap_or_else(|| unreachable!());
    let mut state =
        asset_plane::plan::initial_state(entry, variant).unwrap_or_else(|| unreachable!());
    state = asset_plane::plan::advance(
        &state,
        asset_plane::plan::TransferOutcome::Interrupted { written_bytes: 400 },
        0,
        &NoJitter,
    );
    let later = state.retry_after_monotonic_ms;
    let plan = plan_startup(
        &catalog(),
        core::slice::from_ref(&state),
        policy(NetworkCost::Unmetered, false),
        later,
    );
    let Some(InstallStep::Fetch { offset, .. }) = plan.steps.first() else {
        unreachable!("expected a fetch, got {:?}", plan.steps)
    };
    assert_eq!(*offset, 400);
}

#[test]
fn a_backoff_that_has_not_elapsed_waits_rather_than_asking_again() {
    let entry = ENTRIES.first().unwrap_or_else(|| unreachable!());
    let variant = entry
        .variant(Platform::AndroidArm64)
        .unwrap_or_else(|| unreachable!());
    let state = asset_plane::plan::advance(
        &asset_plane::plan::initial_state(entry, variant).unwrap_or_else(|| unreachable!()),
        asset_plane::plan::TransferOutcome::Interrupted { written_bytes: 400 },
        10_000,
        &NoJitter,
    );
    let plan = plan_startup(
        &catalog(),
        core::slice::from_ref(&state),
        policy(NetworkCost::Unmetered, false),
        10_001,
    );
    assert!(matches!(plan.steps.first(), Some(InstallStep::Wait { .. })));
}

#[test]
fn an_installed_asset_is_planned_no_further() {
    let entry = ENTRIES.first().unwrap_or_else(|| unreachable!());
    let variant = entry
        .variant(Platform::AndroidArm64)
        .unwrap_or_else(|| unreachable!());
    let state = asset_plane::plan::advance(
        &asset_plane::plan::initial_state(entry, variant).unwrap_or_else(|| unreachable!()),
        asset_plane::plan::TransferOutcome::Installed,
        0,
        &NoJitter,
    );
    let plan = plan_startup(
        &catalog(),
        core::slice::from_ref(&state),
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert!(plan.steps.is_empty(), "{:?}", plan.steps);
}

#[test]
fn a_platform_with_no_bytes_is_absent_from_the_plan_rather_than_refused_at_fetch() {
    let plan = plan_startup(
        &catalog(),
        &[],
        DevicePolicy {
            platform: Platform::WindowsX64,
            network: NetworkCost::Unmetered,
            metered_permitted: false,
        },
        0,
    );
    assert!(plan.steps.is_empty(), "{:?}", plan.steps);
}

#[test]
fn an_unpublished_variant_refuses_with_the_reason_a_person_can_read() {
    let plan = plan_one_asset(
        &catalog(),
        "python-stdlib",
        "3.14.2",
        None,
        DevicePolicy {
            platform: Platform::MacosArm64,
            network: NetworkCost::Unmetered,
            metered_permitted: false,
        },
        0,
    );
    assert!(matches!(
        plan,
        InstallStep::Refuse {
            reason: RefusalReason::NotPublishedYet,
            ..
        }
    ));
}

#[test]
fn an_on_demand_asset_is_not_fetched_at_start_up_and_is_when_asked_for() {
    let startup = plan_startup(&catalog(), &[], policy(NetworkCost::Unmetered, false), 0);
    assert!(!startup
        .steps
        .iter()
        .any(|step| matches!(step, InstallStep::Fetch { id, .. } if id == "python-toolkit")));

    let asked = plan_one_asset(
        &catalog(),
        "python-toolkit",
        "1",
        None,
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert!(matches!(asked, InstallStep::Fetch { .. }));
}

#[test]
fn an_asset_nobody_cataloged_is_refused_by_identity() {
    let plan = plan_one_asset(
        &catalog(),
        "not-an-asset",
        "1",
        None,
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert!(matches!(
        plan,
        InstallStep::Refuse {
            reason: RefusalReason::UnknownAsset,
            ..
        }
    ));
}

#[test]
fn one_transfer_runs_at_a_time_and_the_rest_wait() {
    // Two required assets, one radio. The second is a wait, not a second
    // socket — and it is named in the plan rather than dropped from it, so a
    // surface can show a person that it is queued.
    static SECOND: &[Variant] = &[Variant::new(
        Platform::AndroidArm64,
        Publication::Published,
        "second.zip",
        10,
        10,
        DIGEST,
    )];
    static TWO: &[CatalogEntry] = &[
        CatalogEntry::new(
            "python-stdlib",
            "3.14.2",
            Kind::PythonStdlib,
            Necessity::Required,
            Container::Zip,
            None,
            PUBLISHED,
        ),
        CatalogEntry::new(
            "model-weights-small",
            "1",
            Kind::ModelWeights,
            Necessity::Required,
            Container::Raw,
            Some(ModelFacts::new(
                ModelFormat::OnnxRuntime,
                ArtifactRole::Whole,
            )),
            SECOND,
        ),
    ];
    let plan = plan_startup(
        &Catalog::from_entries(TWO, 9),
        &[],
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert_eq!(plan.transfer_count(), 1);
    assert_eq!(plan.steps.len(), 2);
    assert!(matches!(plan.steps.get(1), Some(InstallStep::Wait { .. })));
}

#[test]
fn a_state_for_another_revision_does_not_satisfy_this_one() {
    // The install of 3.14.1 says nothing about 3.14.2. Matching on identity
    // alone would make an upgrade look already done.
    let entry = ENTRIES.first().unwrap_or_else(|| unreachable!());
    let variant = entry
        .variant(Platform::AndroidArm64)
        .unwrap_or_else(|| unreachable!());
    let mut state = asset_plane::plan::advance(
        &asset_plane::plan::initial_state(entry, variant).unwrap_or_else(|| unreachable!()),
        asset_plane::plan::TransferOutcome::Installed,
        0,
        &NoJitter,
    );
    state.revision = asset_plane::AssetRevision::parse("3.14.1").unwrap_or_else(|_| unreachable!());
    let plan = plan_startup(
        &catalog(),
        core::slice::from_ref(&state),
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert_eq!(plan.transfer_count(), 1);
    let _ = AssetState::absent(state.id.clone(), state.revision.clone(), 0);
}

#[test]
fn one_asset_planning_ignores_state_for_another_asset() {
    let entry = ENTRIES.first().unwrap_or_else(|| unreachable!());
    let variant = entry
        .variant(Platform::AndroidArm64)
        .unwrap_or_else(|| unreachable!());
    let installed_other = asset_plane::plan::advance(
        &asset_plane::plan::initial_state(entry, variant).unwrap_or_else(|| unreachable!()),
        asset_plane::plan::TransferOutcome::Installed,
        0,
        &NoJitter,
    );

    let step = plan_one_asset(
        &catalog(),
        "python-toolkit",
        "1",
        Some(&installed_other),
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert!(
        matches!(step, InstallStep::Fetch { .. }),
        "state for another identity cannot suppress this fetch: {step:?}"
    );
}

#[test]
fn asking_again_after_exhaustion_starts_a_fresh_transfer() {
    let entry = ENTRIES.first().unwrap_or_else(|| unreachable!());
    let variant = entry
        .variant(Platform::AndroidArm64)
        .unwrap_or_else(|| unreachable!());
    let mut exhausted =
        asset_plane::plan::initial_state(entry, variant).unwrap_or_else(|| unreachable!());
    for _ in 0..asset_plane::defaults::MAX_TRANSFER_ATTEMPTS {
        exhausted = asset_plane::plan::advance(
            &exhausted,
            asset_plane::plan::TransferOutcome::OriginRefused { permanent: false },
            0,
            &NoJitter,
        );
    }
    assert_eq!(exhausted.refusal, Some(RefusalReason::AttemptsExhausted));

    let startup = plan_startup(
        &catalog(),
        core::slice::from_ref(&exhausted),
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert!(matches!(
        startup.steps.first(),
        Some(InstallStep::Refuse {
            reason: RefusalReason::AttemptsExhausted,
            ..
        })
    ));

    let restarted = asset_plane::plan::restart_retry_series(&exhausted);
    assert_eq!(restarted.attempts, 0);
    assert_eq!(restarted.refusal, None);

    let step = plan_one_asset(
        &catalog(),
        entry.id(),
        entry.revision(),
        Some(&restarted),
        policy(NetworkCost::Unmetered, false),
        0,
    );
    assert!(
        matches!(step, InstallStep::Fetch { .. }),
        "a retryable exhaustion must admit a fresh explicit request: {step:?}"
    );

    let failed_fresh_transfer = asset_plane::plan::advance(
        &restarted,
        asset_plane::plan::TransferOutcome::Interrupted { written_bytes: 100 },
        10_000,
        &NoJitter,
    );
    assert_eq!(failed_fresh_transfer.attempts, 1);
    assert_eq!(failed_fresh_transfer.refusal, None);
}
