// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Delivery policy for the product's own artifacts.
//!
//! Authoritative specifications:
//! `docs/decisions/0045-taffy-assets-are-a-delivery-plane.md` for what an asset
//! is and why its bytes are pinned rather than served;
//! `docs/decisions/0007-sandboxed-python-utility-process.md` for the first
//! client of this plane. Owning milestone: M6 for the plane, M7 for Python.
//!
//! # What an asset is, and what it is not
//!
//! An **asset** is a first-party artifact the product needs and the installer
//! did not carry: a Python standard library, a local model, a tokenizer, a
//! filter list. It has an identity, one revision, a platform, and a digest that
//! was decided when the product was built.
//!
//! A **download** is a person's file, fetched from a page they were on, written
//! where they can find it. It has none of those things, and nothing in this
//! crate applies to one. The two share a screen and share nothing else — the
//! separation is decision 0045 section 3.
//!
//! # Why the catalog is compiled in
//!
//! Every asset's digest, byte length and path is a constant of the build. A
//! catalog fetched at run time would mean a service that can point a device at
//! different bytes than the ones the product was tested against, and the repository
//! installs nothing unpinned at run time. So [`catalog`] is generated from a
//! committed source file into Rust, and the plane can refuse an artifact by
//! comparing it against a number nobody can move without shipping a build.
//!
//! The cost is honest and small: publishing a new asset revision is a product
//! change. The benefit is that a compromised delivery origin can withhold bytes
//! and can corrupt bytes, and can do nothing else — both of which end in the
//! same refusal.
//!
//! # What this crate does not do
//!
//! It opens no socket, touches no file, reads no clock and holds no bytes. It
//! is a planner: given what the catalog says, what the device already has, and
//! what just happened to a transfer, it answers what should happen next. The
//! browser process performs it, because the browser process is the only one
//! that may. That split is what makes an install replayable from an audit
//! record, and what lets one plane serve Android, macOS and Windows without a
//! `cfg` in this crate.
//!
//! # Module map
//!
//! | Module | Owns |
//! |---|---|
//! | [`ids`] | Bounded asset identity and revision strings |
//! | [`digest`] | The 32-byte content digest and the integrity verdict |
//! | [`platform`] | The platform matrix an asset is published for |
//! | [`catalog`] | The compiled-in catalog, its entries and its lookups |
//! | [`defaults`] | The tuning this layer owns, in one place |
//! | [`state`] | Where one asset has got to, and what may follow |
//! | [`plan`] | What to do at start-up, and after a transfer ends |

#![forbid(unsafe_code)]

pub mod catalog;
pub mod defaults;
pub mod digest;
pub mod ids;
pub mod plan;
pub mod platform;
pub mod state;

pub use catalog::{
    ArtifactRole, Catalog, CatalogEntry, Container, Kind, ModelFacts, ModelFormat, Necessity,
    Publication, Variant,
};
pub use digest::{Digest, IntegrityVerdict};
pub use ids::{AssetId, AssetRevision, IdentityError};
pub use plan::{restart_retry_series, InstallStep, StartupPlan, TransferOutcome};
pub use platform::Platform;
pub use state::{AssetState, Presence, RefusalReason, TransferProgress};
