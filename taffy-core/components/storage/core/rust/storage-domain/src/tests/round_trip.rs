// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing records and reading them back.
//!
//! Includes the two refusals that make the round trip trustworthy: an
//! externally derived accepted fact with no evidence, and a second writer at
//! the same journal revision.

use super::support::*;

#[test]
fn a_workspace_with_sources_and_facts_round_trips() {
    let mut database = migrated();
    seed_workspace(&mut database);

    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    assert_eq!(
        records::load_workspace(executor, workspace_id()).unwrap(),
        Some(sample_workspace())
    );
    let sources = records::readable_sources(executor, workspace_id()).unwrap();
    assert_eq!(sources.len(), 2);
    assert_eq!(
        sources.first().map(|source| source.source_id),
        Some(source_id())
    );
    assert_eq!(
        records::load_source(executor, source_id()).unwrap(),
        Some(sample_source(source_id(), "https://reports.example/a"))
    );

    let facts = records::live_facts(executor, workspace_id()).unwrap();
    assert_eq!(facts.len(), 2);
    assert_eq!(facts.first(), Some(&sample_fact(fact_id())));
    let evidence = records::fact_provenance(executor, shared_fact_id()).unwrap();
    assert_eq!(evidence.len(), 2);
    transaction.rollback().unwrap();
}

#[test]
fn an_accepted_derived_fact_without_evidence_is_refused() {
    let mut database = migrated();
    seed_workspace(&mut database);
    let mut transaction = database.begin().unwrap();
    let mut unsupported = sample_fact(FactId::from_bytes([0x77; 16]));
    unsupported.classification = FactClassification::Summarized;
    let error = records::insert_fact(transaction.as_mut(), &unsupported, &[]).unwrap_err();
    assert!(matches!(error, StorageError::Malformed { .. }));
    transaction.rollback().unwrap();
}

#[test]
fn the_journal_refuses_a_second_writer_at_the_same_revision() {
    let mut database = migrated();
    let mut transaction = database.begin().unwrap();
    let first = event(EventId::from_bytes([0x91; 16]), 1);
    let mut second = event(EventId::from_bytes([0x92; 16]), 1);
    second.event_type = "TaskCancelled".to_owned();
    journal::append(transaction.as_mut(), &first).unwrap();
    let error = journal::append(transaction.as_mut(), &second).unwrap_err();
    assert!(matches!(error, StorageError::Backend { .. }));
    transaction.rollback().unwrap();
}
