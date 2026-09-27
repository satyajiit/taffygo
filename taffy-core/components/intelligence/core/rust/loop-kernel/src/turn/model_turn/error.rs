// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed refusals produced while composing one model turn.

use model_router::wire::{ManagedWireRefusal, WireRefusal};
use model_router::RouteRefusal;

/// Why a turn could not be composed.
///
/// Every member is a refusal to send something nobody planned. None of them
/// falls back to another route, another provider or a smaller request: a call
/// that differs from the one that was refused is a call nobody consented to.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModelTurnError {
    /// The task disclosed no provider route, so nothing may be sent.
    NoDisclosedRoute,
    /// The task disclosed the reviewed no-model route.
    NoModelRoute,
    /// The router refused to plan.
    Route(RouteRefusal),
    /// The chosen provider has no usable credential handle on this device.
    CredentialMissing,
    /// The body could not be written.
    Body(WireRefusal),
    /// The managed canonical body could not be written.
    ManagedBody(ManagedWireRefusal),
    /// The catalog endpoint was not an `https` origin this core may name.
    EndpointUnusable,
    /// A compiled-in tool had no compiled-in argument schema.
    ToolSchemaUnavailable,
    /// The candidate came from the person's own layer and carried no address
    /// to send to.
    ///
    /// Its own member rather than a second use of the one above, because the
    /// two addresses are refused by two rules that are deliberately not
    /// written in terms of each other (decision 0096 section 2). A refusal
    /// that named the catalog rule for a person's own server would send
    /// whoever reads it looking at the wrong half of the product.
    OwnEndpointUnusable,
    /// The local SHA-256 adapter was unavailable, so no stable identity could
    /// be derived for the call.
    DigestUnavailable,
    /// Durable accounting says a paid sub-attempt was authorized, but its
    /// exact transient route plan died before dispatch. Recomposition is not
    /// allowed to turn that authorization into a different paid request.
    SubattemptPlanLost,
    /// A bounded identity or byte count did not fit the contract.
    Overflow,
}

impl ModelTurnError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::NoDisclosedRoute => "no_disclosed_route",
            Self::NoModelRoute => "no_model_route",
            Self::Route(_) => "route_refused",
            Self::CredentialMissing => "credential_missing",
            Self::Body(_) => "body_refused",
            Self::ManagedBody(_) => "managed_body_refused",
            Self::EndpointUnusable => "endpoint_unusable",
            Self::ToolSchemaUnavailable => "tool_schema_unavailable",
            Self::OwnEndpointUnusable => "own_endpoint_unusable",
            Self::DigestUnavailable => "digest_unavailable",
            Self::SubattemptPlanLost => "subattempt_plan_lost",
            Self::Overflow => "overflow",
        }
    }
}
