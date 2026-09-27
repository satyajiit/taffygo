// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_api_types::{MemoryAvailability, MemorySensitivity as ViewSensitivity, MemorySourceKind};
use core_runtime::{
    DigestError, MemoryPort, MemoryScopeInput, MemorySensitivityInput, MemoryStoreError,
    MemoryUserUpsert, ProductionMemory, Sha256Port,
};

struct TestDigest;

impl Sha256Port for TestDigest {
    fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], DigestError> {
        Ok([7; 32])
    }
}

fn upsert(
    memory_id: Option<String>,
    statement: &str,
    sensitivity: MemorySensitivityInput,
    expected_memory_revision: u64,
    expected_record_revision: u64,
    approved_at_epoch_ms: u64,
) -> MemoryUserUpsert {
    MemoryUserUpsert {
        memory_id,
        statement: statement.to_owned(),
        scope: MemoryScopeInput::AllTasks,
        sensitivity,
        expected_memory_revision,
        expected_record_revision,
        expires_at_epoch_ms: None,
        approved_at_epoch_ms,
    }
}

#[test]
fn exact_person_crud_and_search_publish_only_after_browser_commit() {
    let mut memory = ProductionMemory::new(false);
    let save = memory
        .begin_user_upsert(
            "memory-create".to_owned(),
            upsert(
                None,
                "Prefer nonstop flights",
                MemorySensitivityInput::Standard,
                0,
                0,
                100,
            ),
            &TestDigest,
        )
        .unwrap_or_else(|error| unreachable!("valid create: {error:?}"))
        .unwrap_or_else(|| unreachable!("new Memory needs a durable write"));
    assert!(memory.project_core_api().records.is_empty());
    assert_eq!(
        memory.complete("memory-create", 2),
        Err(MemoryStoreError::WrongCompletion)
    );
    assert_eq!(memory.complete("memory-create", 1), Ok(()));

    let projected = memory.project_core_api();
    assert_eq!(projected.availability, MemoryAvailability::Available);
    assert_eq!(projected.revision, 1);
    let record = projected
        .records
        .first()
        .unwrap_or_else(|| unreachable!("committed Memory is published"));
    assert_eq!(record.memory_id, save.record.memory_id);
    assert_eq!(record.source_kind, MemorySourceKind::YouWrote);
    assert_eq!(record.statement, "Prefer nonstop flights");

    memory
        .search_for_person("memory-search", "NONSTOP", 8, 200)
        .unwrap_or_else(|error| unreachable!("valid person search: {error:?}"));
    let search = memory
        .project_core_api()
        .search
        .unwrap_or_else(|| unreachable!("explicit search is published"));
    assert_eq!(search.memory_revision, 1);
    assert_eq!(search.hits.len(), 1);

    let task_search = memory
        .search_for_task("flights", 8, None, 200)
        .unwrap_or_else(|error| unreachable!("valid task retrieval: {error:?}"));
    assert!(task_search
        .result_pieces()
        .join("\n")
        .contains("You wrote this"));

    let update = memory
        .begin_user_upsert(
            "memory-update".to_owned(),
            upsert(
                Some(save.record.memory_id.clone()),
                "Prefer daytime nonstop flights",
                MemorySensitivityInput::Sensitive,
                1,
                1,
                300,
            ),
            &TestDigest,
        )
        .unwrap_or_else(|error| unreachable!("valid update: {error:?}"))
        .unwrap_or_else(|| unreachable!("changed Memory needs a durable write"));
    assert_eq!(update.record.source_kind, save.record.source_kind);
    assert_eq!(memory.complete("memory-update", 2), Ok(()));
    assert_eq!(
        memory
            .project_core_api()
            .records
            .first()
            .map(|value| value.sensitivity),
        Some(ViewSensitivity::Sensitive)
    );
    assert_eq!(
        memory
            .search_for_task("daytime", 8, None, 400)
            .unwrap_or_else(|error| unreachable!("valid task retrieval: {error:?}"))
            .result_pieces(),
        &["No active Memory matched."]
    );

    let deletion = memory
        .begin_delete(
            "memory-delete".to_owned(),
            &save.record.memory_id,
            2,
            2,
            500,
        )
        .unwrap_or_else(|error| unreachable!("valid exact deletion: {error:?}"));
    assert_eq!(deletion.resulting_memory_revision, 3);
    assert_eq!(memory.project_core_api().records.len(), 1);
    assert_eq!(memory.complete("memory-delete", 3), Ok(()));
    assert!(memory.project_core_api().records.is_empty());
    assert!(memory.project_core_api().search.is_none());
}

#[test]
fn private_profiles_restore_publish_and_persist_no_memory_content() {
    let mut regular = ProductionMemory::new(false);
    let saved = regular
        .begin_user_upsert(
            "regular-create".to_owned(),
            upsert(
                None,
                "Remember me",
                MemorySensitivityInput::Standard,
                0,
                0,
                100,
            ),
            &TestDigest,
        )
        .unwrap_or_else(|error| unreachable!("valid fixture: {error:?}"))
        .unwrap_or_else(|| unreachable!("fixture is new"));

    let mut private = ProductionMemory::new(true);
    assert_eq!(private.restore(0, Vec::new()), Ok(()));
    assert_eq!(
        private.restore(1, vec![saved.record]),
        Err(MemoryStoreError::PrivateProfile)
    );
    assert_eq!(
        private.search_for_person("private-search", "remember", 8, 200),
        Err(MemoryStoreError::PrivateProfile)
    );
    assert_eq!(
        private.begin_user_upsert(
            "private-create".to_owned(),
            upsert(
                None,
                "Do not save",
                MemorySensitivityInput::Standard,
                0,
                0,
                300
            ),
            &TestDigest,
        ),
        Err(MemoryStoreError::PrivateProfile)
    );
    let projected = private.project_core_api();
    assert_eq!(projected.availability, MemoryAvailability::PrivateProfile);
    assert_eq!(projected.revision, 0);
    assert!(projected.records.is_empty());
    assert!(projected.search.is_none());
}
