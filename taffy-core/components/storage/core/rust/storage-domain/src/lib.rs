// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Portable storage-domain types and reference algorithms for assistant data.
//!
//! Authoritative specifications:
//! `docs/decisions/0006-separate-ai-storage-single-writer.md` for the ownership
//! rule, and `docs/architecture/domain-model.md` for the aggregates, the
//! journal, retention, deletion, and schema evolution. Owning milestone: M3
//! (the first workflow), with the Library and Memory aggregates arriving at M6.
//!
//! # The one rule everything else follows from
//!
//! Chromium owns browser data, and the browser storage broker is the only
//! physical writer of assistant data. This crate owns the portable schema and
//! deterministic domain algorithms. It owns no connection, filesystem path,
//! raw SQL input, or service lifetime. A test walks the schema and fails if a
//! table or column has drifted toward duplicating a Chromium profile store.
//!
//! The isolated core encodes typed durable intents; the browser broker commits
//! them and returns a typed completion. Reducer effects stay withheld until
//! that completion says the command, events, audit record, and effect intents
//! are durable.
//!
//! # Module map
//!
//! | Module | Owns |
//! |---|---|
//! | [`backend`] | The database boundary: values, rows, executors, transactions |
//! | [`migration`] | Numbered forward-only migrations, checksums, and the runner |
//! | [`schema`] | The statements each migration applies |
//! | [`records`] | Typed records, closed enumerations, and structured lookups |
//! | [`search`] | The derived full-text index and its queries |
//! | [`journal`] | The append-only event journal and its projections |
//! | [`deletion`] | End-to-end deletion of a source, verified before it commits |
//! | [`retention`] | Retention classes and their accountable upper bounds |
//! | [`deferred`] | Aggregates this schema deliberately does not create yet |
//! | [`backup`] | Canonical encrypted-backup manifest and restore planning |
//! | [`clock`] | Injected time; nothing here reads a system clock |
//!
//! # What runs where
//!
//! [`backend`] is a portable reference boundary used to prove the algorithms.
//! Its library implementation exists only under `cfg(test)`. The production
//! Chromium SQL adapter lives in the browser storage broker, not this crate,
//! and the isolated core never receives its connection or profile path.
//!
//! # No panics
//!
//! Every fallible operation returns [`error::StorageError`], nothing indexes,
//! and corrupt broker input fails closed inside the isolated core service.
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

pub mod backend;
pub mod backup;
pub mod clock;
pub mod core_journal;
pub mod deferred;
pub mod deletion;
pub mod error;
pub mod ids;
pub mod journal;
pub mod library;
pub mod memory;
pub mod migration;
pub mod records;
pub mod retention;
pub mod schema;
pub mod search;
pub mod workspace;
pub mod workspace_lifecycle;

pub use crate::backend::{Connection, Executor, Row, Transaction, Value};
pub use crate::clock::{Clock, FixedClock, Timestamp};
pub use crate::deletion::{DeletionReceipt, DeletionRequest};
pub use crate::error::StorageError;
pub use crate::migration::{head, migrate, MigrationOutcome, MIGRATIONS};

#[cfg(test)]
mod sqlite;

#[cfg(test)]
mod tests;
