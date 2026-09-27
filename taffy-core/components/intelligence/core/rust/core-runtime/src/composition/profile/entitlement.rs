// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The managed entitlement's composition surface (decision 0082).
//!
//! Browser-poked planning and delivery of the one mint in flight. Everything
//! that gates and holds lives in [`crate::entitlement_refresh`]; this file is
//! where an installed summary becomes routing state — the router's
//! [`ManagedEntitlement`] — because a summary the router has not heard about
//! is the invisible-state class the delivery bridge shipped once already.
//!
//! Two installers call [`ProfileServiceRuntime::install_managed_entitlement`]
//! and they are the whole list: delivery of a fetched summary, and the one
//! function every change to the merge goes through
//! ([`ProfileServiceRuntime::reinstall_catalog_state`]) — the entitled model
//! set is resolved *against the merge*, so a merge that changed has to
//! re-resolve it or routing spends yesterday's model keys over today's
//! catalog.

use std::collections::BTreeSet;

use model_router::money::Micros;
use model_router::route::ManagedEntitlement;
use model_router::MergedCatalog;

use crate::entitlement_refresh::EntitlementFetchVerdict;
use crate::wire;

use super::ProfileServiceRuntime;

/// Builds the router's entitlement from the held summary, over one merge.
///
/// Fail-closed in every direction: no summary, a definitive absence, or a
/// summary that names no worker host each produce the default — unavailable —
/// entitlement, because a managed dispatch none of them could serve must be
/// refused at selection rather than composed and bounced.
pub(super) fn managed_entitlement_from(
    summary: Option<&wire::EntitlementSummaryResult>,
    merged: &MergedCatalog,
) -> ManagedEntitlement {
    let Some(summary) = summary else {
        return ManagedEntitlement::default();
    };
    if summary.definitive_absent || summary.worker_host.is_empty() {
        return ManagedEntitlement::default();
    }
    // The summary names models by id alone — the worker's catalog does not
    // key by provider — so each id is resolved against the merge and an id
    // the merge does not carry entitles nothing on this device. That is the
    // honest reading: the worker may serve a model this build cannot name,
    // and a key invented for it would be a routable claim about a catalog
    // entry that does not exist.
    let mut entitled_models = BTreeSet::new();
    for entry in merged.models() {
        if summary
            .model_ids
            .iter()
            .any(|model_id| model_id == entry.model.model_id.as_str())
        {
            entitled_models.insert(model_router::ModelKey {
                provider_id: entry.model.provider_id.clone(),
                model_id: entry.model.model_id.clone(),
            });
        }
    }
    ManagedEntitlement {
        available: true,
        entitled_models,
        remaining_micros: Some(Micros::new(
            summary
                .credits_remaining
                .saturating_mul(summary.credit_unit_micros),
        )),
        remaining_calls: Some(u32::try_from(summary.requests_remaining).unwrap_or(u32::MAX)),
        worker_host: summary.worker_host.clone(),
        gateway_host: summary.gateway_host.clone(),
    }
}

impl ProfileServiceRuntime {
    /// Plans one entitlement fetch if `reason` warrants it (decision 0082).
    ///
    /// The browser pokes — at bootstrap, after a sign-in, on its cadence,
    /// after a quota-refused managed dispatch — and the core decides. A
    /// private profile has no account to mint for and plans nothing, ever.
    pub fn plan_entitlement_refresh(
        &mut self,
        reason: wire::EntitlementFetchReason,
        now_utc_ms: u64,
    ) -> Option<wire::EffectEnvelope> {
        if self.private_profile {
            return None;
        }
        let generation = self.core.service_generation().value();
        self.entitlement_refresh
            .begin_refresh(generation, reason, now_utc_ms)
    }

    /// Judges one delivered entitlement fetch and installs what it accepts.
    ///
    /// An installed summary — a definitive absence included — rebuilds the
    /// router's entitlement over the current merge. The caller republishes
    /// status exactly when this answers `Installed`, which is how a surface
    /// hears the plan row change; a transport failure changes nothing and
    /// publishes nothing.
    pub fn deliver_entitlement_fetch_result(
        &mut self,
        summary: Option<&wire::EntitlementSummaryResult>,
        now_utc_ms: u64,
    ) -> EntitlementFetchVerdict {
        let verdict = self
            .entitlement_refresh
            .deliver_fetch_result(summary, now_utc_ms);
        if matches!(verdict, EntitlementFetchVerdict::Installed { .. }) {
            self.install_managed_entitlement();
        }
        verdict
    }

    /// Recomputes and installs the router's entitlement over the current
    /// merge.
    ///
    /// Whole-replacement on the port, so an entitlement that lapsed
    /// disappears from routing in the same call that would have renewed it.
    pub(super) fn install_managed_entitlement(&mut self) {
        let merged = self.merged_catalog();
        let entitlement = managed_entitlement_from(self.entitlement_refresh.summary(), &merged);
        self.core.models_mut().set_entitlement(entitlement);
    }

    /// Forgets the entitlement with the account it belonged to.
    ///
    /// Called on the ordered sequence when the signed-in session goes away:
    /// the summary was that account's answer, and routing must stop spending
    /// it in the same breath rather than on the next fetch.
    pub(super) fn clear_managed_entitlement(&mut self) {
        self.entitlement_refresh.clear();
        self.core
            .models_mut()
            .set_entitlement(ManagedEntitlement::default());
    }
}
