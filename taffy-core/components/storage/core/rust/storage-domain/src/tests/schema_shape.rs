// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the schema is allowed to contain.
//!
//! Two boundaries and one table. Nothing here duplicates a store Chromium
//! already owns, nothing belonging to a later milestone exists yet, and the
//! retention table in the code and the one in the database cannot disagree.

use super::support::*;

#[test]
fn no_table_or_column_duplicates_a_chromium_store() {
    let database = migrated();
    let tables = database
        .raw_query_text("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name")
        .unwrap();
    for table in &tables {
        for banned in CHROMIUM_OWNED_NAMES {
            assert!(
                !table.contains(banned),
                "table {table} looks like a copy of a Chromium store"
            );
        }
        let columns = database
            .raw_query_text(&format!(
                "SELECT name FROM pragma_table_info('{table}') ORDER BY name"
            ))
            .unwrap();
        for column in columns {
            for banned in CHROMIUM_OWNED_NAMES {
                assert!(
                    !column.contains(banned),
                    "column {table}.{column} looks like a copy of a Chromium store"
                );
            }
        }
    }
}

#[test]
fn aggregates_owned_by_later_milestones_are_absent() {
    let database = migrated();
    let tables = database
        .raw_query_text("SELECT name FROM sqlite_master WHERE type = 'table'")
        .unwrap();
    for deferred in DEFERRED_AGGREGATES {
        assert!(
            !tables.iter().any(|name| name == deferred.table_name),
            "{} exists, but {} owns it",
            deferred.table_name,
            deferred.milestone
        );
        assert!(!deferred.reason.is_empty());
    }
}

#[test]
fn the_retention_table_and_the_code_table_cannot_disagree() {
    let mut database = migrated();
    let mut transaction = database.begin().unwrap();
    let rows = transaction
        .query(
            "SELECT name, durable, bound_kind, register_entry FROM retention_class ORDER BY name",
            &[],
        )
        .unwrap();
    assert_eq!(rows.len(), RETENTION_CLASSES.len());
    for row in &rows {
        let name = row.text(0).unwrap();
        let entry = retention::class(name).expect("the code table knows this class");
        assert_eq!(row.boolean(1).unwrap(), entry.durable);
        assert_eq!(row.text(2).unwrap(), entry.bound.kind());
        match entry.bound {
            RetentionBound::Unset { register_entry } => {
                assert_eq!(row.maybe_text(3).unwrap(), Some(register_entry));
            }
            _ => assert_eq!(row.maybe_text(3).unwrap(), None),
        }
    }
    transaction.rollback().unwrap();
}

#[test]
fn no_retention_class_invents_a_duration_nobody_published() {
    for entry in RETENTION_CLASSES {
        if let RetentionBound::Unset { register_entry } = entry.bound {
            assert!(
                register_entry.starts_with("OD-"),
                "{} owes a bound to nobody in particular",
                entry.name
            );
        }
    }
}
