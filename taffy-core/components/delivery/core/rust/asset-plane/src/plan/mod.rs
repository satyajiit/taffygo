// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What should happen next, given what the catalog says and what the device has.
//!
//! Two questions, and nothing else. [`plan_startup`] answers "the profile just
//! started — what should be fetched?" and [`advance`] answers "a transfer just
//! ended — what is the asset's state now?". Both are pure functions of their
//! arguments: no clock, no randomness, no input or output. The browser supplies
//! the monotonic time and the jitter, which is what makes an install
//! reproducible from an audit record.

mod advance;
mod backoff;

pub use advance::{advance, restart_retry_series, TransferOutcome};
pub use backoff::{delay_millis, Jitter, NoJitter};

use crate::catalog::{Catalog, CatalogEntry, Container, Variant};
use crate::defaults;
use crate::digest::Digest;
use crate::platform::Platform;
use crate::state::{AssetState, Presence, RefusalReason};

/// What the device's connection costs.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NetworkCost {
    /// No connection.
    Offline,
    /// A connection a person pays for by the byte.
    Metered,
    /// A connection they do not.
    Unmetered,
}

/// What the device is and what a person allowed.
#[derive(Clone, Copy, Debug)]
pub struct DevicePolicy {
    /// Which bytes this device can use.
    pub platform: Platform,
    /// What its connection costs right now.
    pub network: NetworkCost,
    /// Carried for the wire. Transfer does not read it: a live connection
    /// may carry any artifact, and offline is the only wait.
    pub metered_permitted: bool,
}

impl DevicePolicy {
    /// Whether a transfer may start right now.
    ///
    /// A live connection may carry any catalog row. Offline is the only wait.
    /// Required vs on-demand decides *when* a fetch is planned, not which
    /// connection it may use.
    pub fn may_transfer(&self) -> bool {
        !matches!(self.network, NetworkCost::Offline)
    }
}

/// One thing the browser should do about one asset.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum InstallStep {
    /// Ask the origin for bytes, starting at `offset`.
    Fetch {
        /// Which asset.
        id: String,
        /// Which revision.
        revision: String,
        /// The origin-relative path to ask for.
        path: String,
        /// The first byte wanted; zero for a fresh transfer.
        offset: u64,
        /// How many bytes there are in total.
        total_bytes: u64,
        /// What the whole artifact must hash to.
        digest: Digest,
        /// What the bytes are, and therefore what installing them means.
        ///
        /// Carried on the step rather than looked up again by whoever performs
        /// it: the performer has no catalog, and a second lookup is a second
        /// place for the answer to differ.
        container: Container,
    },
    /// Do nothing until `until_monotonic_ms`, then plan again.
    Wait {
        /// Which asset.
        id: String,
        /// Which revision.
        revision: String,
        /// The monotonic millisecond to try again after.
        until_monotonic_ms: u64,
    },
    /// Do nothing, and say why.
    Refuse {
        /// Which asset.
        id: String,
        /// Which revision.
        revision: String,
        /// Why.
        reason: RefusalReason,
    },
}

/// Everything to do about every asset, in order.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct StartupPlan {
    /// The steps, at most [`defaults::MAX_TRACKED_ASSETS`] of them.
    pub steps: Vec<InstallStep>,
}

impl StartupPlan {
    /// The steps that ask for bytes.
    pub fn fetches(&self) -> impl Iterator<Item = &InstallStep> {
        self.steps
            .iter()
            .filter(|step| matches!(step, InstallStep::Fetch { .. }))
    }

    /// How many transfers this plan would start, which is capped so a profile
    /// starting with nothing installed does not open every socket at once.
    pub fn transfer_count(&self) -> usize {
        self.fetches().count()
    }
}

/// Plans every required asset for a profile that just started.
///
/// `states` is what the device already knows, in any order; an asset absent
/// from it is treated as absent from the device. The answer names every
/// required asset — including the ones it refuses — because a surface that only
/// hears about the assets that worked cannot show a person why the other one
/// did not.
pub fn plan_startup(
    catalog: &Catalog,
    states: &[AssetState],
    policy: DevicePolicy,
    now_monotonic_ms: u64,
) -> StartupPlan {
    let mut steps = Vec::new();
    let mut started = 0usize;
    for (entry, variant) in catalog.required_for(policy.platform) {
        if steps.len() >= defaults::MAX_TRACKED_ASSETS {
            break;
        }
        let existing = states
            .iter()
            .find(|it| it.id.as_str() == entry.id() && it.revision.as_str() == entry.revision());
        let step = plan_one(entry, variant, existing, policy, now_monotonic_ms, started);
        if matches!(step, Some(InstallStep::Fetch { .. })) {
            started += 1;
        }
        if let Some(step) = step {
            steps.push(step);
        }
    }
    StartupPlan { steps }
}

/// Plans one asset a caller named, whatever its necessity.
///
/// This is what an on-demand asset goes through: a person asked for it, or a
/// tool needs it, so the necessity that governs start-up does not apply and
/// everything else does. A caller admitting a new request after exhaustion
/// first applies [`restart_retry_series`] to the state; this planner cannot
/// infer a request edge from repeated pure calls.
pub fn plan_one_asset(
    catalog: &Catalog,
    id: &str,
    revision: &str,
    state: Option<&AssetState>,
    policy: DevicePolicy,
    now_monotonic_ms: u64,
) -> InstallStep {
    let Some(entry) = catalog
        .entries()
        .iter()
        .find(|it| it.id() == id && it.revision() == revision)
    else {
        return refuse(id, revision, RefusalReason::UnknownAsset);
    };
    let Some(variant) = entry.variant(policy.platform) else {
        return refuse(id, revision, RefusalReason::NoVariantForPlatform);
    };
    // A cached state is a fact only about the identity and revision it names.
    // Treat a mismatched record as absent, just as start-up planning does,
    // rather than letting another installed asset suppress this fetch.
    let matching_state = state.filter(|existing| {
        existing.id.as_str() == entry.id() && existing.revision.as_str() == entry.revision()
    });
    plan_one(entry, variant, matching_state, policy, now_monotonic_ms, 0)
        .unwrap_or_else(|| refuse(id, revision, RefusalReason::DeclinedByPerson))
}

/// The one asset decision. `None` means nothing to do and nothing to say —
/// which is only ever true of an asset that is already installed.
fn plan_one(
    entry: &CatalogEntry,
    variant: &Variant,
    state: Option<&AssetState>,
    policy: DevicePolicy,
    now_monotonic_ms: u64,
    already_started: usize,
) -> Option<InstallStep> {
    let id = entry.id();
    let revision = entry.revision();

    if state.is_some_and(AssetState::is_installed) {
        return None;
    }
    if let Some(reason) = catalog_refusal(variant) {
        return Some(refuse(id, revision, reason));
    }
    if let Some(existing) = state {
        if let Some(reason) = existing.refusal.filter(|it| !it.is_retryable()) {
            return Some(refuse(id, revision, reason));
        }
        if existing.attempts >= defaults::MAX_TRANSFER_ATTEMPTS {
            return Some(refuse(id, revision, RefusalReason::AttemptsExhausted));
        }
        if existing.retry_after_monotonic_ms > now_monotonic_ms {
            return Some(InstallStep::Wait {
                id: id.to_owned(),
                revision: revision.to_owned(),
                until_monotonic_ms: existing.retry_after_monotonic_ms,
            });
        }
    }
    if !policy.may_transfer() {
        return Some(refuse(id, revision, RefusalReason::NetworkNotPermitted));
    }
    if already_started >= defaults::MAX_CONCURRENT_TRANSFERS {
        return Some(InstallStep::Wait {
            id: id.to_owned(),
            revision: revision.to_owned(),
            until_monotonic_ms: now_monotonic_ms,
        });
    }
    let digest = variant.digest()?;
    let offset = state.map_or(0, AssetState::resume_offset);
    Some(InstallStep::Fetch {
        id: id.to_owned(),
        revision: revision.to_owned(),
        path: variant.path().to_owned(),
        offset: offset.min(variant.transfer_bytes()),
        total_bytes: variant.transfer_bytes(),
        digest,
        container: entry.container(),
    })
}

/// What is wrong with a catalog row, if anything.
fn catalog_refusal(variant: &Variant) -> Option<RefusalReason> {
    if !matches!(
        variant.publication(),
        crate::catalog::Publication::Published
    ) {
        return Some(RefusalReason::NotPublishedYet);
    }
    if variant.transfer_bytes() > defaults::MAX_VARIANT_BYTES {
        return Some(RefusalReason::VariantTooLarge);
    }
    if !variant.is_fetchable() {
        return Some(RefusalReason::CatalogRowIncomplete);
    }
    None
}

fn refuse(id: &str, revision: &str, reason: RefusalReason) -> InstallStep {
    InstallStep::Refuse {
        id: id.to_owned(),
        revision: revision.to_owned(),
        reason,
    }
}

/// The state a device starts an unknown asset in, so a caller does not have to
/// reach into [`Presence`] to build one.
pub fn initial_state(entry: &CatalogEntry, variant: &Variant) -> Option<AssetState> {
    let id = crate::ids::AssetId::parse(entry.id()).ok()?;
    let revision = crate::ids::AssetRevision::parse(entry.revision()).ok()?;
    let mut state = AssetState::absent(id, revision, variant.transfer_bytes());
    state.presence = Presence::Absent;
    Some(state)
}
