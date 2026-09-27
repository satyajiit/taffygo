// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider registry and the model catalog.
//!
//! A provider is an identity and a model is a descriptor under it; both are
//! data, and neither is code. The only per-provider code in the model router is the
//! protocol-family adapter, so a new provider or a new model on a family the
//! model router already speaks is a catalog change rather than a release.
//!
//! The pipeline is three steps and each has its own module:
//!
//! | Step | Module | Answers |
//! |---|---|---|
//! | Decode | [`decode`] | Is this entry well-formed, bounded, and a version we know? |
//! | Validate | [`validate`] | Do these entries describe a world the router can act in? |
//! | Merge | [`merge`] | Which layer's version of an entry wins? |

pub mod decode;
pub mod merge;
pub mod sanitize;
pub mod types;
pub mod validate;

pub use decode::{parse_document, CatalogDefect, DefectLocation, DefectReason, ParsedCatalog};
pub use merge::{
    CatalogLayer, LookupError, MergedCatalog, ModelEntry, ProviderEntry, ResolvedModel,
};
pub use sanitize::{sanitize_overlay, EndpointChangeRefusal, SanitizedOverlay};
pub use types::{
    AuthMethod, CatalogDocument, CatalogHeader, Endpoint, EndpointError, InputModality,
    LongContextTier, Model, ModelRole, ModelSource, PriceBasis, PriceSnapshot, Provider, Rates,
    UserEndpointError, WireApi, SUPPORTED_SCHEMA_VERSION,
};
pub use validate::{validate, CatalogViolation, ViolationRule};
