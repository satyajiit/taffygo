// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The decided plan: one primary model and the ordered substitutes for it.
//!
//! The invariant this module carries is that **failover stays inside one
//! disclosure class.** `RoutePlan::crosses_disclosure_boundary` is the
//! predicate a test walks, and it is here rather than in the selector so that
//! a plan can be checked without building one.

use super::disclosure::{Disclosure, DisclosureClass};
use super::request::Route;
use crate::catalog::{CatalogLayer, Endpoint, ModelRole, WireApi};
use crate::cost::CostAmount;
use crate::credential::AuthAttachment;
use crate::ids::ModelKey;
use crate::thinking::ThinkingPlan;
/// One model the plan may use.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct RouteCandidate {
    /// Which model.
    pub model: ModelKey,
    /// The protocol family after provider and model overrides.
    pub wire_api: WireApi,
    /// The endpoint after provider and model overrides.
    pub endpoint: Endpoint,
    /// Which layer of the merged catalog named [`RouteCandidate::endpoint`].
    ///
    /// Carried for the same reason as `tool_calling` below: a plan outlives
    /// the snapshot it was built from, and the party that composes a request
    /// has to know which authority named the address it is about to send to
    /// (decision 0096 section 2). Re-reading the catalog there would be a
    /// second answer, and reading the address itself would be a guess.
    pub endpoint_layer: CatalogLayer,
    /// The thinking control, already clamped to this model's ladder.
    pub thinking: ThinkingPlan,
    /// Whether this model may be handed tools.
    ///
    /// Carried on the candidate so the request encoder can refuse a tool
    /// vocabulary without resolving the catalog again. A plan outlives the
    /// snapshot it was built from, and re-reading the descriptor later is how
    /// the encoder and the selector come to disagree about one model.
    pub tool_calling: bool,
    /// Which credential applies and which headers carry it.
    pub auth: Option<AuthAttachment>,
    /// What the user is told.
    pub disclosure: Disclosure,
}

/// A route, a primary model, and the ordered substitutes for it.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct RoutePlan {
    /// The placement.
    pub route: Route,
    /// The role being filled.
    pub role: ModelRole,
    /// The model to call.
    pub primary: RouteCandidate,
    /// Substitutes, in order. Every one shares the primary's disclosure class.
    pub failover: Vec<RouteCandidate>,
    /// What the primary call is expected to cost.
    pub estimate: CostAmount,
}

impl RoutePlan {
    /// The disclosure class every candidate in the plan shares.
    pub fn disclosure_class(&self) -> DisclosureClass {
        self.primary.disclosure.class
    }

    /// Whether any candidate departs from the primary's route or class.
    ///
    /// Always `false` for a plan this module built. It is public so a caller
    /// that assembles a plan by other means can assert the same property.
    pub fn crosses_disclosure_boundary(&self) -> bool {
        self.failover.iter().any(|candidate| {
            candidate.disclosure.class != self.primary.disclosure.class
                || candidate.disclosure.route != self.route
        })
    }
}
