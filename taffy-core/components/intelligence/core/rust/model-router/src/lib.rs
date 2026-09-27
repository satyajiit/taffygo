// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Provider-neutral model contracts and route selection.
//!
//! Authoritative specifications:
//! `docs/architecture/provider-registry-and-model-catalog.md` for what a
//! provider and a model are and how their metadata and credentials reach the
//! device; `docs/decisions/0005-byo-direct-and-managed-gateway.md` for the two
//! routes; `docs/quality/cost-model.md` for what a task's cost means.
//! Owning milestone: M3 (the first workflow).
//!
//! # What this crate is
//!
//! A planner and a set of typed contracts. It performs no network input or
//! output, opens no socket, reads no clock, and holds no page content. Given a
//! catalog snapshot, a device's credential metadata, an entitlement, and a
//! budget, it answers three questions and nothing else: which model, over which
//! route, and what the user is told about it.
//!
//! Transport belongs to the browser's network stack and threading belongs to
//! the C++ side. Keeping this crate a pure function of its inputs is what makes
//! it testable on a host, and what makes a routing decision reproducible from
//! an audit record.
//!
//! # What it deliberately is not
//!
//! It is not an authority. A catalog entry authorizes nothing by itself: every
//! side effect still crosses the policy and capability broker, and a provider
//! appearing in a catalog does not mean a request to it is permitted.
//!
//! It is not a place secrets live. [`credential::CredentialRef`] is metadata
//! and appears in plans, disclosures, and audit records;
//! [`credential::SecretMaterial`] is the secret, has no serde implementation,
//! and never enters any of them.
//!
//! It performs no request. [`wire`] shapes a body and reads a reply, as one
//! table keyed by the protocol family; the browser process holds the network
//! stack and the credential store and makes the call. Streaming is the
//! transport of that reply (decision 0071): frames are live, the first
//! terminal event wins, and this crate still holds no answer text.
//!
//! # Module map
//!
//! | Module | Owns |
//! |---|---|
//! | [`catalog`] | Provider and model descriptors, fail-closed decoding, invariants, three-layer merge, lookups |
//! | [`thinking`] | The internal thinking ladder, per-model mapping, clamping, and budget translation |
//! | [`credential`] | Credential references, secret material, and the metadata-only directory |
//! | [`route`] | Route eligibility, disclosure classes, failover ordering, and the plan |
//! | [`cost`] | Token usage, price snapshots, the task ledger, and budget refusal |
//! | [`request`] | The request contract, the error taxonomy, and the single terminal result |
//! | [`retry`] | Backoff and failover planning with injected jitter |
//! | [`defaults`] | The tuning defaults this layer owns, in one place |
//! | [`json`] | The bounded reader catalog documents are parsed with |
//! | [`normalize`] | Request-side hygiene for a replayed transcript: minted identities, image placeholders, thinking replay |
//! | [`wire`] | The four protocol families, as one table, and the request and reply readers of it |
//!
//! # Three rules that hold everywhere
//!
//! **Nothing fails open.** An unknown provider, an unknown model, an unknown
//! enumeration value, an unknown schema version, a missing kill switch, and a
//! missing credential are all refusals. There is no nearest match and no
//! default provider.
//!
//! **A route never changes underneath the user.** Failure on the direct route
//! produces a refusal the user can act on, never a quiet request to somewhere
//! else. Failover substitutes a model inside one disclosure class and never
//! substitutes the class.
//!
//! **Every decision is reproducible.** No clock, no randomness, no global
//! state, and no iteration order that varies by run: candidate ordering is
//! total, jitter is injected, and time arrives as data.
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

pub mod catalog;
pub mod cost;
pub mod credential;
pub mod defaults;
pub mod ids;
pub mod json;
pub mod money;
pub mod normalize;
pub mod request;
pub mod retry;
pub mod route;
pub mod thinking;
pub mod time;
pub mod wire;

pub use crate::catalog::{
    parse_document, validate, CatalogDocument, MergedCatalog, Model, ModelRole, Provider,
};
pub use crate::cost::{CostReport, TaskBudget, TaskLedger, TokenUsage};
pub use crate::credential::{CredentialRef, CredentialState, SecretMaterial};
pub use crate::ids::{ModelId, ModelKey, ProviderId, TaskId};
pub use crate::request::{ErrorClass, ModelRequest, TerminalResult, TerminalSlot};
pub use crate::route::{Disclosure, Route, RoutePlan, RouteRefusal, RouteSelector};
pub use crate::thinking::{ThinkingLevel, ThinkingPlan};
pub use crate::wire::{
    dialect_for, fold_stream, looks_like_stream, read_reply, write_media_request, write_request,
    FoldedReply, ModelStreamDecoder, ModelStreamDefect, ModelStreamReading, WireMediaAttachment,
    WireRequest, MEDIA_ATTACHMENT_MIME_TYPE,
};

/// The catalog compiled into this build.
///
/// First run and offline operation work from this layer alone. It is the
/// embedded baseline of the three-layer merge; a served overlay is merged over
/// it only when the overlay is newer, and the user's own providers are merged
/// last.
pub const EMBEDDED_BASELINE_CATALOG: &str = include_str!("../catalog/baseline.json");

/// Decodes and validates the embedded baseline.
///
/// Returns the parse result and the invariant violations together, so a caller
/// that wants to fail a build on either can see both. A baseline that does not
/// decode is a build defect, not a runtime condition.
pub fn embedded_baseline(
) -> Result<(catalog::ParsedCatalog, Vec<catalog::CatalogViolation>), json::JsonError> {
    let parsed = parse_document(EMBEDDED_BASELINE_CATALOG)?;
    let violations = validate(&parsed.document);
    Ok((parsed, violations))
}
