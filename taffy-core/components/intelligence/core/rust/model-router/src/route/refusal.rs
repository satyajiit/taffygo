// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Why the router refused.
//!
//! A closed enumeration, and the module that carries the rule that gives this
//! crate its shape: **a route never changes under a failure.** Every member
//! here is a refusal. There is no member that means "fell back", because a
//! silent fallback would move the user's page content across a boundary they
//! chose not to cross.

use super::request::Route;
use crate::catalog::{InputModality, LookupError, ModelRole, WireApi};
use crate::cost::BudgetRefusal;
use crate::credential::AuthType;
use crate::credential::CredentialState;
use crate::ids::{ModelKey, ProviderId};
use crate::money::Micros;
use crate::thinking::ThinkingError;
/// Why the router refused.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RouteRefusal {
    /// The request carries material that is never transmittable.
    ProhibitedMaterial,
    /// A pinned model did not resolve.
    Lookup(LookupError),
    /// Nothing in the catalog can serve this role on this route.
    NoCandidate {
        /// The role asked for.
        role: ModelRole,
        /// The route asked for.
        route: Route,
    },
    /// The provider has no stored credential.
    CredentialMissing {
        /// The provider.
        provider_id: ProviderId,
    },
    /// The stored credential needs the user's attention. The route does not
    /// change; the user fixes the credential.
    CredentialNeedsAttention {
        /// The provider.
        provider_id: ProviderId,
        /// What is wrong.
        state: CredentialState,
    },
    /// The provider does not accept the stored credential's method.
    AuthMethodUnsupported {
        /// The provider.
        provider_id: ProviderId,
        /// The stored method.
        auth_type: AuthType,
    },
    /// Managed access is not available on this device.
    ManagedUnavailable,
    /// The managed entitlement does not cover this model.
    ModelNotEntitled {
        /// The model.
        model: ModelKey,
    },
    /// The managed entitlement is used up.
    QuotaExhausted {
        /// Amount left, when it is metered.
        remaining_micros: Option<Micros>,
        /// Calls left, when they are counted.
        remaining_calls: Option<u32>,
    },
    /// The task budget will not cover the call.
    Budget(BudgetRefusal),
    /// The model does not accept a modality the request needs.
    ModalityUnsupported {
        /// The model.
        model: ModelKey,
        /// The modality.
        modality: InputModality,
    },
    /// The request carries tools and the model cannot answer with a tool call.
    ///
    /// A refusal rather than a request sent without its tools: a model asked
    /// to act, given no way to act, answers with prose that reads like a plan.
    /// The task would then loop on plausible text and never do anything, which
    /// is far harder to see than a refusal naming the model.
    ToolCallingUnsupported {
        /// The model.
        model: ModelKey,
    },
    /// The request carries tools and the managed wire this build speaks cannot
    /// carry them.
    ///
    /// About the wire, not the model: it states a fact about a schema version
    /// rather than a property of the route, and it is read off the same
    /// predicate the managed writer reads — `tools_travel_at` over
    /// `MANAGED_TOOL_SCHEMA_VERSION` (decision 0092). So it holds for every
    /// entitled model equally, which is why it names none of them, and a build
    /// whose wire carries tools does not raise it at all. Refused rather than
    /// sent without its tools, for the same reason as
    /// [`Self::ToolCallingUnsupported`]: that would trade a visible refusal
    /// for a silent loop.
    ManagedToolCallingUnsupported,
    /// The request carries tools and the wire family it resolved to replays a
    /// tool loop only alongside sealed reasoning this build cannot carry.
    ///
    /// About the wire and the transcript together, which is why it names the
    /// family rather than the model: the subscription responses family seals
    /// the reasoning a turn produced and pairs each tool call positionally
    /// with the reasoning item that preceded it. A second turn that replays
    /// the call without the item breaks that pairing, and the endpoint's
    /// answer to a broken pairing is to close the connection before headers —
    /// which arrives here as a stream that never started rather than as an
    /// error anything could explain.
    ///
    /// The durable transcript is rebuilt from the task ledger and carries no
    /// reasoning at all, so the item is not merely dropped in one place; there
    /// is nowhere for it to have been kept. Until there is, a tool-carrying
    /// call on this family is refused by name. Decision 0111 section 5 is
    /// where the two acceptable outcomes are written down, and this is the one
    /// that does not require a durable record that does not exist: a visible
    /// refusal rather than a loop that stops answering on its second turn.
    ToolLoopCarriageMissing {
        /// The family that resolved.
        wire_api: WireApi,
    },
    /// A thinking plan could not be built.
    Thinking(ThinkingError),
}

impl core::fmt::Display for RouteRefusal {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::ProhibitedMaterial => f.write_str("the request carries prohibited material"),
            Self::Lookup(error) => write!(f, "{error}"),
            Self::NoCandidate { role, route } => {
                write!(f, "no model serves {role:?} on {route:?}")
            }
            Self::CredentialMissing { provider_id } => {
                write!(f, "no credential stored for {provider_id}")
            }
            Self::CredentialNeedsAttention { provider_id, state } => {
                write!(f, "credential for {provider_id} is {state:?}")
            }
            Self::AuthMethodUnsupported {
                provider_id,
                auth_type,
            } => write!(f, "{provider_id} does not accept {auth_type:?}"),
            Self::ManagedUnavailable => f.write_str("managed access is unavailable"),
            Self::ModelNotEntitled { model } => write!(f, "{model} is not entitled"),
            Self::QuotaExhausted { .. } => f.write_str("the managed entitlement is used up"),
            Self::Budget(refusal) => write!(f, "{refusal}"),
            Self::ModalityUnsupported { model, modality } => {
                write!(f, "{model} does not accept {modality:?}")
            }
            Self::ToolCallingUnsupported { model } => write!(f, "{model} cannot call tools"),
            Self::ManagedToolCallingUnsupported => {
                f.write_str("the managed wire this build speaks cannot carry tools")
            }
            Self::ToolLoopCarriageMissing { wire_api } => write!(
                f,
                "{wire_api:?} replays a tool loop only with sealed reasoning this build cannot carry"
            ),
            Self::Thinking(error) => write!(f, "{error}"),
        }
    }
}
