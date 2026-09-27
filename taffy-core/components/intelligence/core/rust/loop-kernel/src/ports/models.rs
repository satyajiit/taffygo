// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The route-planning port and its box blanket.

use model_router::catalog::Endpoint;
use model_router::route::{ManagedEntitlement, ModelPolicy, RouteRequest};
use model_router::{CredentialRef, MergedCatalog, ModelKey, RoutePlan, RouteRefusal, TaskLedger};

/// Provider-neutral route planning with no transport or credential material.
///
/// The two installers are why this port is stateful. Route selection is a pure
/// function of the catalog, the credential directory and the endpoint
/// registry, and until something replaced those the directory was permanently
/// empty and every direct route was refused for want of a credential. They
/// carry metadata only: `CredentialRef` has no field that could hold a secret,
/// and an `Endpoint` is a validated `https` base URL.
///
/// `model_router::RouteSelector` deliberately does not implement this port. A
/// selector borrows the catalog and directory it selects over, so it cannot
/// replace either; the impl that used to be here satisfied the routing half
/// and had no caller.
pub trait ModelRouterPort {
    /// Selects one disclosure-preserving route.
    fn route(
        &mut self,
        request: &RouteRequest,
        ledger: &TaskLedger,
    ) -> Result<RoutePlan, RouteRefusal>;

    /// The context window the catalog gives `model`, in tokens.
    ///
    /// On the port rather than read off a plan because a `RouteCandidate`
    /// deliberately does not carry it: a plan is a placement decision, and the
    /// window is a fact about the entry it was decided from. Reading the reply
    /// needs the number — an overflow verdict is a comparison against the
    /// window the model actually has — and a caller that carried its own copy
    /// would be a second place for it to go stale. Catalog data only: nothing
    /// reachable through this method is secret.
    fn context_window(&self, model: &ModelKey) -> Option<u64>;

    /// Installs the credential metadata the provider plane now holds.
    ///
    /// Replaces the directory whole rather than merging: a credential the
    /// person removed has to disappear from routing, and a merge would leave
    /// it behind.
    fn replace_credentials(&mut self, entries: Vec<CredentialRef>);

    /// Installs the endpoints a person runs themselves.
    fn replace_local_endpoints(&mut self, endpoints: Vec<Endpoint>);

    /// Installs a rebuilt catalog merge after an accepted overlay refresh.
    ///
    /// Whole, not patched: the merge already decided which layer won each
    /// entry, and routing must read exactly that decision. Credentials, the
    /// entitlement, the policy and the local endpoints all survive — they are
    /// the person's and the account's state, not the catalog's.
    fn install_catalog(&mut self, catalog: MergedCatalog);

    /// Installs the backend-issued managed-route entitlement.
    ///
    /// Whole, like the credential directory, and for the same reason: an
    /// entitlement that lapsed has to disappear from routing, and the summary
    /// it came from is the account plane's answer, not an edit this port may
    /// merge with a previous one. Counts and identifiers only — the token the
    /// summary arrived beside never reaches this port, by construction of the
    /// contract struct that carries the summary.
    fn set_entitlement(&mut self, entitlement: ManagedEntitlement);

    /// Installs the frozen per-role routing preference.
    ///
    /// Called with the honest-empty policy until a preference surface exists:
    /// the call is the seam, and an empty policy means candidate order is
    /// catalog key order alone.
    fn set_policy(&mut self, policy: ModelPolicy);
}

impl<T> ModelRouterPort for Box<T>
where
    T: ModelRouterPort + ?Sized,
{
    fn route(
        &mut self,
        request: &RouteRequest,
        ledger: &TaskLedger,
    ) -> Result<RoutePlan, RouteRefusal> {
        self.as_mut().route(request, ledger)
    }

    fn context_window(&self, model: &ModelKey) -> Option<u64> {
        self.as_ref().context_window(model)
    }

    fn replace_credentials(&mut self, entries: Vec<CredentialRef>) {
        self.as_mut().replace_credentials(entries);
    }

    fn replace_local_endpoints(&mut self, endpoints: Vec<Endpoint>) {
        self.as_mut().replace_local_endpoints(endpoints);
    }

    fn install_catalog(&mut self, catalog: MergedCatalog) {
        self.as_mut().install_catalog(catalog);
    }

    fn set_entitlement(&mut self, entitlement: ManagedEntitlement) {
        self.as_mut().set_entitlement(entitlement);
    }

    fn set_policy(&mut self, policy: ModelPolicy) {
        self.as_mut().set_policy(policy);
    }
}
