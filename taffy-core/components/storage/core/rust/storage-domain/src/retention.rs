// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Retention classes and their accountable upper bounds.
//!
//! Every data-bearing record carries a retention class, and every class carries
//! a bound that says how it ends. The bounds are data — a table in code, seeded
//! into a table in the database, with a test that compares the two — because a
//! retention rule that lives as a literal in whichever function happened to
//! need it is a rule nobody can audit.
//!
//! One kind of bound is deliberately not a number. The class names are decided;
//! their absolute durations are not, and are owed by the open decision each
//! [`RetentionBound::Unset`] cites. Writing a plausible duration here would look
//! like a published upper bound and would be nothing of the kind, so the
//! unresolved ones say so in the schema, where an auditor reading the database
//! can see exactly which classes still owe a number and who owes it.

use crate::backend::{Executor, Value};
use crate::error::StorageError;

/// A runtime lifetime a class ends with.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum LifetimeScope {
    /// The incognito context. Destroyed with it; never durable.
    IncognitoContext,
    /// One operation, plus a bounded cleanup.
    Operation,
}

impl LifetimeScope {
    /// The stored spelling.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::IncognitoContext => "INCOGNITO_CONTEXT",
            Self::Operation => "OPERATION",
        }
    }
}

/// How a retention class ends.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RetentionBound {
    /// It ends with a runtime lifetime and never becomes durable.
    Lifetime(LifetimeScope),
    /// It ends when the user removes the thing it belongs to.
    UserManaged,
    /// A duration that has been decided and published.
    PublishedDuration {
        /// The bound, in milliseconds.
        millis: u64,
    },
    /// A duration that has not been decided.
    ///
    /// The cited register entry owns it. Collection under this class is not
    /// supposed to begin before the entry resolves.
    Unset {
        /// The open-decision entry that owes the number.
        register_entry: &'static str,
    },
}

impl RetentionBound {
    /// The stored spelling of the bound kind.
    pub fn kind(self) -> &'static str {
        match self {
            Self::Lifetime(_) => "LIFETIME",
            Self::UserManaged => "USER_MANAGED",
            Self::PublishedDuration { .. } => "PUBLISHED_DURATION",
            Self::Unset { .. } => "UNSET",
        }
    }
}

/// One retention class.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RetentionClass {
    /// The stored name.
    pub name: &'static str,
    /// Whether records in this class survive a process restart.
    pub durable: bool,
    /// How it ends.
    pub bound: RetentionBound,
    /// What it covers.
    pub description: &'static str,
}

/// Every retention class, in the order the data inventory lists them.
///
/// This is the only place they are written down in this crate.
pub const RETENTION_CLASSES: &[RetentionClass] = &[
    RetentionClass {
        name: "INCOGNITO_MEMORY",
        durable: false,
        bound: RetentionBound::Lifetime(LifetimeScope::IncognitoContext),
        description: "Live observations and transient results inside one incognito context",
    },
    RetentionClass {
        name: "OPERATION_BUFFER",
        durable: false,
        bound: RetentionBound::Lifetime(LifetimeScope::Operation),
        description: "Snapshots and provider stream buffers held for one operation",
    },
    RetentionClass {
        name: "TASK_SESSION",
        durable: true,
        bound: RetentionBound::Unset {
            register_entry: "OD-042",
        },
        description: "Temporary observations and unaccepted drafts, until terminal task cleanup",
    },
    RetentionClass {
        name: "WORKSPACE_MANAGED",
        durable: true,
        bound: RetentionBound::UserManaged,
        description: "Accepted facts, minimum provenance, accepted results, workspace audit",
    },
    RetentionClass {
        name: "USER_PINNED",
        durable: true,
        bound: RetentionBound::UserManaged,
        description: "Material the user deliberately kept",
    },
    RetentionClass {
        name: "ACCOUNT_LIFETIME",
        durable: true,
        bound: RetentionBound::Unset {
            register_entry: "OD-042",
        },
        description: "Account profile and entitlement configuration; not browsing content",
    },
    RetentionClass {
        name: "OPERATIONAL_LIMITED",
        durable: true,
        bound: RetentionBound::Unset {
            register_entry: "OD-042",
        },
        description: "Aggregated reliability and performance metadata",
    },
    RetentionClass {
        name: "SECURITY_LIMITED",
        durable: true,
        bound: RetentionBound::Unset {
            register_entry: "OD-042",
        },
        description: "Approved incident and abuse metadata, content-free by default",
    },
    RetentionClass {
        name: "DELETION_TOMBSTONE",
        durable: true,
        bound: RetentionBound::Unset {
            register_entry: "OD-042",
        },
        description: "Opaque deleted record identity, version, and time; no content",
    },
    RetentionClass {
        name: "EXTERNAL_PROVIDER_POLICY",
        durable: false,
        bound: RetentionBound::Unset {
            register_entry: "OD-005",
        },
        description: "Retention outside the device, governed by the provider's own terms",
    },
];

/// Looks a class up by its stored name.
pub fn class(name: &str) -> Option<&'static RetentionClass> {
    RETENTION_CLASSES
        .iter()
        .find(|candidate| candidate.name == name)
}

/// Writes the class table into the database.
///
/// Called by the migration that creates the table. Seeding from the same list
/// the code reads is what lets a test assert the two cannot disagree.
pub fn seed(executor: &mut dyn Executor) -> Result<(), StorageError> {
    for entry in RETENTION_CLASSES {
        let (scope, millis, register_entry) = match entry.bound {
            RetentionBound::Lifetime(scope) => {
                (Value::text(scope.as_str()), Value::Null, Value::Null)
            }
            RetentionBound::UserManaged => (Value::Null, Value::Null, Value::Null),
            RetentionBound::PublishedDuration { millis } => (
                Value::Null,
                Value::Integer(i64::try_from(millis).unwrap_or(i64::MAX)),
                Value::Null,
            ),
            RetentionBound::Unset { register_entry } => {
                (Value::Null, Value::Null, Value::text(register_entry))
            }
        };
        executor.execute(
            "INSERT INTO retention_class \
             (name, durable, bound_kind, bound_scope, bound_millis, register_entry, description) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            &[
                Value::text(entry.name),
                Value::boolean(entry.durable),
                Value::text(entry.bound.kind()),
                scope,
                millis,
                register_entry,
                Value::text(entry.description),
            ],
        )?;
    }
    Ok(())
}
