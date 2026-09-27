// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The migration ladder: that it is well formed, that it reaches head, and
//! that every way it can go wrong leaves a database somebody can still open.

use super::support::*;

#[test]
fn every_migration_is_numbered_consecutively_and_described() {
    for (expected, migration) in (1..).zip(MIGRATIONS.iter()) {
        assert_eq!(
            migration.version, expected,
            "{} is out of order",
            migration.name
        );
        assert!(!migration.name.is_empty());
        assert!(!migration.description.is_empty());
        assert!(!migration.statements.is_empty());
        for statement in migration.statements {
            assert!(!statement.trim().is_empty());
        }
    }
    assert_eq!(
        migration::head(),
        u32::try_from(MIGRATIONS.len()).expect("a sane migration count")
    );
    // Everything so far only adds structure. When a migrate or contract phase
    // arrives it will be visible here and in the ledger, which is the point of
    // recording the phase at all.
    assert!(MIGRATIONS
        .iter()
        .all(|migration| migration.phase == MigrationPhase::Expand));
}

#[test]
fn an_empty_database_migrates_to_head() {
    let mut database = empty_database();
    assert_eq!(migration::current_version(&mut database).unwrap(), 0);

    let outcome = migration::migrate(&mut database, &clock()).unwrap();
    assert_eq!(outcome.head, migration::head());
    assert_eq!(outcome.applied.len(), MIGRATIONS.len());
    assert!(!outcome.already_current);
    assert_eq!(
        migration::current_version(&mut database).unwrap(),
        migration::head()
    );

    let mut transaction = database.begin().unwrap();
    let recorded = count(
        transaction.as_mut(),
        "SELECT COUNT(*) FROM schema_migration",
        &[],
    )
    .unwrap();
    assert_eq!(usize::try_from(recorded).unwrap(), MIGRATIONS.len());
    transaction.rollback().unwrap();
}

#[test]
fn migrating_a_current_database_changes_nothing() {
    let mut database = migrated();
    let outcome = migration::migrate(&mut database, &clock()).unwrap();
    assert!(outcome.applied.is_empty());
    assert!(outcome.already_current);
    assert_eq!(outcome.head, migration::head());
}

#[test]
fn an_applied_migration_that_changed_is_refused() {
    let mut database = migrated();
    database
        .raw_execute("UPDATE schema_migration SET checksum = 'ffffffffffffffff' WHERE version = 1")
        .unwrap();
    let error = migration::migrate(&mut database, &clock()).unwrap_err();
    assert!(matches!(
        error,
        StorageError::MigrationChanged { version: 1, .. }
    ));
}

#[test]
fn a_database_written_by_a_newer_build_stops_this_one() {
    let mut database = migrated();
    database
        .raw_execute(
            "INSERT INTO schema_migration \
             (version, name, description, phase, checksum, applied_at_utc) \
             VALUES (9999, 'future', 'from a newer build', 'EXPAND', 'x', '2027-01-01T00:00:00Z')",
        )
        .unwrap();
    let error = migration::migrate(&mut database, &clock()).unwrap_err();
    assert!(matches!(
        error,
        StorageError::SchemaFromNewerBuild { database: 9999, .. }
    ));
}

#[test]
fn a_migration_that_fails_partway_leaves_the_database_usable() {
    let mut database = empty_database();
    // Something already owns a name a later migration needs, so that migration
    // fails after some of its statements have already run.
    database
        .raw_execute("CREATE TABLE conflict_fact (unrelated TEXT)")
        .unwrap();

    let error = migration::migrate(&mut database, &clock()).unwrap_err();
    assert!(matches!(error, StorageError::Backend { .. }));

    // The failed migration left no trace: not its ledger row, and not the
    // tables its earlier statements had already created.
    assert_eq!(migration::current_version(&mut database).unwrap(), 2);
    let tables = database
        .raw_query_text("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name")
        .unwrap();
    assert!(tables.iter().any(|name| name == "source"));
    assert!(!tables.iter().any(|name| name == "fact"));
    assert!(!tables.iter().any(|name| name == "claim"));

    // The database is still usable, and finishes the upgrade once the obstacle
    // is gone.
    database.raw_execute("DROP TABLE conflict_fact").unwrap();
    let outcome = migration::migrate(&mut database, &clock()).unwrap();
    assert_eq!(outcome.head, migration::head());
    assert_eq!(outcome.applied.first().copied(), Some(3));

    let mut transaction = database.begin().unwrap();
    records::insert_workspace(transaction.as_mut(), &sample_workspace()).unwrap();
    transaction.commit().unwrap();
    let mut transaction = database.begin().unwrap();
    assert!(
        records::load_workspace(transaction.as_mut(), workspace_id())
            .unwrap()
            .is_some()
    );
    transaction.rollback().unwrap();
}
