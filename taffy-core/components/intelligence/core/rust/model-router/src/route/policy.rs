// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the device is entitled to, what the user prefers, and who answers
//! "is this endpoint one the user runs".
//!
//! Three inputs to selection that are not the request. They are grouped
//! because they are all *standing* configuration — issued by the backend,
//! configured by the user, or answered by the host — while everything in
//! `super::request` changes per call.

use std::collections::{BTreeMap, BTreeSet};

use crate::money::Micros;

use crate::catalog::{Endpoint, ModelRole};
use crate::ids::ModelKey;
/// Decides whether an endpoint is a server the user runs.
///
/// The model router does not resolve names, so it does not guess. The host that owns
/// the network stack answers, and the answer only selects a disclosure class.
pub trait EndpointClassifier {
    /// Whether `endpoint` is one of the addresses this person registered for a
    /// server they run themselves.
    ///
    /// A membership question and not a shape one. A registered address is a
    /// base URL with the port and path the person's server has, and it may be
    /// reached over either scheme, so nothing about how it is spelled decides
    /// this — only whether it is one of theirs.
    fn is_local_endpoint(&self, endpoint: &Endpoint) -> bool;
}

/// A classifier for hosts with no local providers configured.
#[derive(Clone, Copy, Debug, Default)]
pub struct NoLocalEndpoints;

impl EndpointClassifier for NoLocalEndpoints {
    fn is_local_endpoint(&self, _endpoint: &Endpoint) -> bool {
        false
    }
}

/// What the backend has entitled this device to on the managed route.
///
/// The model set is issued, not chosen: the edge worker never accepts an
/// arbitrary upstream, so the catalog cannot widen what managed access reaches.
#[derive(Clone, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct ManagedEntitlement {
    /// Whether managed access is available at all.
    pub available: bool,
    /// The models it covers.
    pub entitled_models: BTreeSet<ModelKey>,
    /// Amount left in the entitlement, when it is metered.
    pub remaining_micros: Option<Micros>,
    /// Calls left in the entitlement, when it is counted.
    pub remaining_calls: Option<u32>,
    /// The edge worker host, for disclosure.
    pub worker_host: String,
    /// The gateway host, for disclosure.
    pub gateway_host: String,
}

/// Per-role model preference, as assistant configuration.
///
/// Per-role selection is configuration of the one assistant. It creates no
/// second assistant identity, and it is the first term of candidate ordering.
#[derive(Clone, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(transparent)]
pub struct ModelPolicy(BTreeMap<ModelRole, Vec<ModelKey>>);

impl ModelPolicy {
    /// An empty policy: candidate order is catalog key order alone.
    pub fn new() -> Self {
        Self(BTreeMap::new())
    }

    /// Sets the preference order for a role.
    pub fn set(&mut self, role: ModelRole, order: Vec<ModelKey>) {
        self.0.insert(role, order);
    }

    /// The preference order for a role.
    pub fn preferred(&self, role: ModelRole) -> &[ModelKey] {
        self.0.get(&role).map_or(&[], Vec::as_slice)
    }
}
