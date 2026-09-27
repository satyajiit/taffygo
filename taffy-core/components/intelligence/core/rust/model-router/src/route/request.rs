// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the router is being asked for, and which placement it may use.
//!
//! [`Route`] deliberately has two members. Generation on the device itself is
//! planned behind its own open decision (OD-037) and is absent here: a variant
//! would read as an implementation that does not exist.

use crate::catalog::{InputModality, ModelRole};
use crate::ids::{ModelKey, TaskId};
use crate::request::{ContextManifest, RequestPurpose};
use crate::thinking::ThinkingLevel;
/// A decided placement mode.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum Route {
    /// Device straight to the user's provider, on a device-held credential.
    ByoDirect,
    /// Device to the product's edge worker, then the gateway, then a pinned
    /// provider.
    Managed,
}

/// The route the user selected.
///
/// There is no automatic mode. A router that picks the route picks the privacy
/// boundary, and that is the user's choice to make.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum RoutePreference {
    /// The direct route.
    ByoDirect,
    /// The managed route.
    Managed,
}

impl RoutePreference {
    /// The route this preference resolves to.
    pub fn route(self) -> Route {
        match self {
            Self::ByoDirect => Route::ByoDirect,
            Self::Managed => Route::Managed,
        }
    }
}
/// What the router is being asked for.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RouteRequest {
    /// The task the request belongs to.
    pub task_id: TaskId,
    /// The role to fill.
    pub role: ModelRole,
    /// The route the user selected.
    pub preference: RoutePreference,
    /// What the request is for.
    pub purpose: RequestPurpose,
    /// What it is about.
    pub context: ContextManifest,
    /// Modalities the request needs.
    pub required_modalities: Vec<InputModality>,
    /// Whether this call will carry a tool vocabulary.
    ///
    /// Asked per call rather than derived from the role, because a role says
    /// what kind of work this is and only the caller knows whether *this*
    /// step hands the model tools. Deriving it would refuse a model for a
    /// reason that does not apply to the call in front of it.
    pub requires_tool_calling: bool,
    /// The requested rung of the thinking ladder.
    pub thinking: ThinkingLevel,
    /// Tokens the answer may use.
    pub answer_tokens: u64,
    /// Expected output size, for the estimate.
    pub estimated_output_tokens: u64,
    /// A model the user pinned. A pin is not an invitation to substitute, so a
    /// pinned request plans no failover.
    pub pinned_model: Option<ModelKey>,
}
