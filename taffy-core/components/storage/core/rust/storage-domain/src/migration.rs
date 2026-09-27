// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Numbered, forward-only migrations.
//!
//! Four rules make schema change survivable on a phone, where a process can
//! die mid-upgrade and the user will simply reopen the browser:
//!
//! 1. **Forward only.** Versions are consecutive from one and are never
//!    reordered, renumbered, or edited after release. An applied migration
//!    whose text has changed is refused rather than reconciled, because a
//!    forward-only system has no way to undo the difference.
//! 2. **One migration, one transaction.** A migration either lands whole or
//!    leaves no trace. An interrupted upgrade therefore leaves a database at
//!    the last version that did land — usable, and able to resume.
//! 3. **Checksummed.** Each applied version records a digest of what was
//!    applied, so drift between the database and the build is a startup error
//!    rather than a mystery months later.
//! 4. **Expand, migrate, contract.** Each migration records which phase it is,
//!    so a review can see that a column was added before it was written and
//!    dropped only after nothing read it. The phase is recorded, not inferred;
//!    the discipline lives in review, and this makes it visible there.
//!
//! Running the same head twice is a no-op. That matters more than it sounds:
//! startup calls this unconditionally, so "already current" has to be the
//! cheapest and most common outcome.

use crate::backend::{Connection, Executor, Value};
use crate::clock::Clock;
use crate::error::StorageError;
use crate::retention;
use crate::schema;

/// Which half of an expand-migrate-contract change a migration is.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum MigrationPhase {
    /// Adds structure. Old readers keep working.
    Expand,
    /// Moves data into the structure the expand phase added.
    Migrate,
    /// Removes structure nothing reads any more.
    Contract,
}

impl MigrationPhase {
    /// The stored spelling.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Expand => "EXPAND",
            Self::Migrate => "MIGRATE",
            Self::Contract => "CONTRACT",
        }
    }
}

/// Work a migration does that cannot be expressed as fixed SQL.
///
/// Only used for reference data the crate also reads from code, so that the
/// table and the constant cannot disagree.
pub type SeedFn = fn(&mut dyn Executor) -> Result<(), StorageError>;

/// One migration.
#[derive(Clone, Copy)]
pub struct Migration {
    /// Its version. Consecutive from one.
    pub version: u32,
    /// A short stable name.
    pub name: &'static str,
    /// What it does and why, for the operator reading a failed upgrade.
    pub description: &'static str,
    /// Which phase it is.
    pub phase: MigrationPhase,
    /// The statements, in order.
    pub statements: &'static [&'static str],
    /// Reference data written after the statements.
    pub seed: Option<SeedFn>,
}

impl core::fmt::Debug for Migration {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("Migration")
            .field("version", &self.version)
            .field("name", &self.name)
            .field("phase", &self.phase)
            .field("statements", &self.statements.len())
            .field("seeds", &self.seed.is_some())
            .finish_non_exhaustive()
    }
}

impl Migration {
    /// A digest of everything this migration applies.
    ///
    /// It detects an edited migration, which is the failure this system cannot
    /// recover from. It is not a signature and is not a defence against a
    /// tampered database file; whole-database protection is a separate concern
    /// with its own open decision.
    pub fn checksum(&self) -> String {
        let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
        let mut absorb = |bytes: &[u8]| {
            for byte in bytes {
                hash ^= u64::from(*byte);
                hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
            }
        };
        absorb(self.name.as_bytes());
        absorb(self.description.as_bytes());
        absorb(self.phase.as_str().as_bytes());
        for statement in self.statements {
            absorb(statement.as_bytes());
            absorb(b"\x1e");
        }
        format!("{hash:016x}")
    }
}

/// Every migration, in order. The last one is the head.
pub const MIGRATIONS: &[Migration] = &[
    Migration {
        version: 1,
        name: "workspace_and_source",
        description: "Workspace aggregate, source records, and versioned source membership",
        phase: MigrationPhase::Expand,
        statements: schema::M0001_WORKSPACE_AND_SOURCE,
        seed: None,
    },
    Migration {
        version: 2,
        name: "observation_and_provenance",
        description: "Immutable observations and the provenance locators that cite them",
        phase: MigrationPhase::Expand,
        statements: schema::M0002_OBSERVATION_AND_PROVENANCE,
        seed: None,
    },
    Migration {
        version: 3,
        name: "fact_claim_conflict",
        description: "Normalized facts, model claims and their support, and open conflicts",
        phase: MigrationPhase::Expand,
        statements: schema::M0003_FACT_CLAIM_CONFLICT,
        seed: None,
    },
    Migration {
        version: 4,
        name: "task_and_journal",
        description: "Task aggregate, the append-only event journal, and its projections",
        phase: MigrationPhase::Expand,
        statements: schema::M0004_TASK_AND_JOURNAL,
        seed: None,
    },
    Migration {
        version: 5,
        name: "invocation_and_artifact",
        description: "Model invocation metadata and artifacts with their lineage",
        phase: MigrationPhase::Expand,
        statements: schema::M0005_INVOCATION_AND_ARTIFACT,
        seed: None,
    },
    Migration {
        version: 6,
        name: "assistant_configuration",
        description: "The single assistant configuration record and its versions",
        phase: MigrationPhase::Expand,
        statements: schema::M0006_ASSISTANT_CONFIGURATION,
        seed: None,
    },
    Migration {
        version: 7,
        name: "retention_classes",
        description: "Retention classes and their upper bounds, seeded from the code table",
        phase: MigrationPhase::Expand,
        statements: schema::M0007_RETENTION_CLASSES,
        seed: Some(retention::seed),
    },
    Migration {
        version: 8,
        name: "search_and_deletion_ledger",
        description: "Full-text index, deletion tombstones, and verified deletion receipts",
        phase: MigrationPhase::Expand,
        statements: schema::M0008_SEARCH_AND_DELETION_LEDGER,
        seed: None,
    },
    Migration {
        version: 9,
        name: "credential_and_catalog_cache",
        description: "The per-provider secret store and the cached remote model catalog",
        phase: MigrationPhase::Expand,
        statements: schema::M0009_CREDENTIAL_AND_CATALOG_CACHE,
        seed: None,
    },
    Migration {
        version: 10,
        name: "workspace_lifecycle",
        description: "Exact-revision rename and replay-safe verified workspace deletion receipts",
        phase: MigrationPhase::Expand,
        statements: schema::M0010_WORKSPACE_LIFECYCLE,
        seed: None,
    },
];

/// The version a fully migrated database is at.
pub fn head() -> u32 {
    MIGRATIONS.last().map_or(0, |migration| migration.version)
}

const CREATE_LEDGER: &str = "CREATE TABLE IF NOT EXISTS schema_migration (
        version       INTEGER PRIMARY KEY NOT NULL,
        name          TEXT NOT NULL,
        description   TEXT NOT NULL,
        phase         TEXT NOT NULL,
        checksum      TEXT NOT NULL,
        applied_at_utc TEXT NOT NULL
    )";

/// What one call to [`migrate`] did.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MigrationOutcome {
    /// Versions applied by this call, in order.
    pub applied: Vec<u32>,
    /// The version the database is at afterwards.
    pub head: u32,
    /// Whether the database was already there.
    pub already_current: bool,
}

/// Brings a database to head.
///
/// Safe to call on an empty file, on a current database, and on one left behind
/// by an upgrade that was interrupted. It is not safe to call on a database a
/// newer build wrote, and it says so rather than guessing.
pub fn migrate(
    connection: &mut dyn Connection,
    clock: &dyn Clock,
) -> Result<MigrationOutcome, StorageError> {
    let mut ledger = connection.begin()?;
    ledger.execute(CREATE_LEDGER, &[])?;
    ledger.commit()?;

    let applied_rows = {
        let mut reader = connection.begin()?;
        let rows = reader.query(
            "SELECT version, checksum FROM schema_migration ORDER BY version",
            &[],
        )?;
        reader.rollback()?;
        rows
    };

    let mut applied: Vec<(u32, String)> = Vec::new();
    for row in &applied_rows {
        let version = u32::try_from(row.integer(0)?).map_err(|_| StorageError::Malformed {
            what: "a recorded migration version",
            detail: row.integer(0).unwrap_or_default().to_string(),
        })?;
        applied.push((version, row.text(1)?.to_owned()));
    }

    verify_recorded(&applied)?;

    let mut outcome = MigrationOutcome {
        applied: Vec::new(),
        head: head(),
        already_current: true,
    };
    for migration in MIGRATIONS {
        if applied
            .iter()
            .any(|(version, _)| *version == migration.version)
        {
            continue;
        }
        apply(connection, clock, migration)?;
        outcome.applied.push(migration.version);
        outcome.already_current = false;
    }
    Ok(outcome)
}

fn verify_recorded(applied: &[(u32, String)]) -> Result<(), StorageError> {
    for (version, recorded) in applied {
        let Some(migration) = MIGRATIONS
            .iter()
            .find(|candidate| candidate.version == *version)
        else {
            return Err(StorageError::SchemaFromNewerBuild {
                database: *version,
                build: head(),
            });
        };
        let compiled = migration.checksum();
        if &compiled != recorded {
            return Err(StorageError::MigrationChanged {
                version: *version,
                recorded: recorded.clone(),
                compiled,
            });
        }
    }
    Ok(())
}

fn apply(
    connection: &mut dyn Connection,
    clock: &dyn Clock,
    migration: &Migration,
) -> Result<(), StorageError> {
    let mut transaction = connection.begin()?;
    for statement in migration.statements {
        transaction.execute(statement, &[])?;
    }
    if let Some(seed) = migration.seed {
        seed(transaction.as_mut())?;
    }
    transaction.execute(
        "INSERT INTO schema_migration \
         (version, name, description, phase, checksum, applied_at_utc) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
        &[
            Value::Integer(i64::from(migration.version)),
            Value::text(migration.name),
            Value::text(migration.description),
            Value::text(migration.phase.as_str()),
            Value::text(migration.checksum()),
            Value::text(clock.now_utc().as_str()),
        ],
    )?;
    transaction.commit()
}

/// The version a database is currently at, or zero for an empty one.
pub fn current_version(connection: &mut dyn Connection) -> Result<u32, StorageError> {
    let mut reader = connection.begin()?;
    reader.execute(CREATE_LEDGER, &[])?;
    let rows = reader.query("SELECT IFNULL(MAX(version), 0) FROM schema_migration", &[])?;
    let value = rows
        .first()
        .ok_or(StorageError::RowMissing {
            what: "the migration ledger",
        })?
        .integer(0)?;
    reader.commit()?;
    u32::try_from(value).map_err(|_| StorageError::Malformed {
        what: "the recorded schema version",
        detail: value.to_string(),
    })
}
