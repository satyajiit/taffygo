// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What leaves the device, to whom, and what the user is told about it.
//!
//! The disclosure is derived from the provider descriptor, the stored
//! credential, and the route — never stored as its own flag, because a stored
//! copy is a copy that can drift away from the thing it describes.
//!
//! Nothing here carries a secret. A [`Disclosure`] names a
//! [`CredentialRef`][crate::credential::CredentialRef] and the header the
//! adapter will fill; the value is fetched at request-build time and never
//! enters a disclosure, a plan, or a report.

use super::request::Route;
use crate::catalog::PriceBasis;
use crate::credential::CredentialRef;
use crate::request::DataSensitivity;

/// The user-visible grouping of a request's data handling.
///
/// Derived from the provider descriptor, the stored credential, and the route —
/// never stored as its own flag, because a stored copy is a copy that can drift
/// away from the thing it describes.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DisclosureClass {
    /// Direct route, authorized access backed by the user's own plan.
    SubscriptionDirect,
    /// Direct route, metered access on the user's own key.
    ApiKeyDirect,
    /// Direct route, a server the user runs.
    ///
    /// Its own class rather than a general third-party one, because what
    /// differs about a person's own endpoint is who receives the data, not
    /// whether it leaves the device (decision 0096 section 6). Which addresses
    /// may be reached, and over what scheme, is settled in the browser against
    /// the register it holds; this crate only names the class.
    LocalEndpointDirect,
    /// Managed route.
    Managed,
}

/// Who receives the request.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum Recipient {
    /// The product's edge worker.
    EdgeWorker {
        /// Host, for a surface that names it.
        host: String,
    },
    /// The gateway in front of the pinned provider.
    Gateway {
        /// Host, for a surface that names it.
        host: String,
    },
    /// The model provider.
    Provider {
        /// Name a surface may render.
        display_name: String,
        /// Host, without a path or a query.
        host: String,
    },
}

/// What leaves the device, and to whom.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct EgressStatement {
    /// In the order the request passes through them.
    pub recipients: Vec<Recipient>,
    /// The classes present in the request.
    pub classes: Vec<DataSensitivity>,
    /// How many sources contributed.
    pub source_count: u32,
}

/// Everything a surface needs to say what this request will do.
///
/// A value, not a rendering: the wording belongs to the surface and the voice
/// guide, and the facts belong here.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct Disclosure {
    /// The placement.
    pub route: Route,
    /// The data-handling class.
    pub class: DisclosureClass,
    /// The provider.
    pub provider_display_name: String,
    /// The model.
    pub model_display_name: String,
    /// Where the request goes.
    pub egress: EgressStatement,
    /// Whether the price is billed or imputed.
    pub price_basis: PriceBasis,
    /// Which price snapshot values it.
    pub price_snapshot_version: String,
    /// Which credential applies, by reference only.
    pub credential: Option<CredentialRef>,
}
