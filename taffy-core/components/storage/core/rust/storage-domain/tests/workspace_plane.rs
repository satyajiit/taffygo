// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    encode_snapshot, FactKind, MutationError, WorkspaceFact, WorkspaceMutation,
    WorkspacePageFactReplacement, WorkspacePageFactScope, WorkspacePhase, WorkspaceSnapshot,
    WorkspaceSource, WorkspaceTemplate, MAX_FACTS,
};

fn id<T>(byte: u8, wrap: impl FnOnce([u8; 16]) -> T) -> T {
    wrap([byte; 16])
}

fn snapshot() -> WorkspaceSnapshot {
    let source_id = id(2, SourceId::from_bytes);
    WorkspaceSnapshot {
        workspace_id: id(1, WorkspaceId::from_bytes),
        revision: 3,
        display_name: "Evidence comparison".to_owned(),
        goal: "Compare the retained evidence".to_owned(),
        phase: WorkspacePhase::PartlyDone,
        saved: true,
        last_updated_epoch_ms: 40,
        template: WorkspaceTemplate::CompareProducts,
        sources: vec![WorkspaceSource {
            source_id,
            title: "Independent evidence".to_owned(),
            host: "evidence.example".to_owned(),
            canonical_locator: Some("https://evidence.example/report".to_owned()),
            read_at_epoch_ms: 30,
            excluded: false,
        }],
        facts: vec![WorkspaceFact {
            fact_id: id(3, FactId::from_bytes),
            field: "warranty".to_owned(),
            value: "two years".to_owned(),
            kind: FactKind::FromPage,
            sources: vec![source_id],
            correction: None,
            has_conflict: false,
            media_provenance: None,
        }],
    }
}

#[test]
fn correction_and_exclusion_are_revision_checked() {
    let value = snapshot();
    let fact_id = value
        .facts
        .first()
        .map_or_else(|| id(9, FactId::from_bytes), |fact| fact.fact_id);
    let corrected = value
        .apply(
            3,
            50,
            WorkspaceMutation::CorrectFact {
                fact_id,
                value: "three years".to_owned(),
            },
        )
        .unwrap_or_else(|_| snapshot());
    assert_eq!(corrected.revision, 4);
    assert_eq!(
        corrected
            .facts
            .first()
            .and_then(|fact| fact.correction.as_deref()),
        Some("three years")
    );
    assert_eq!(
        corrected.apply(
            3,
            60,
            WorkspaceMutation::ExcludeSource {
                source_id: id(2, SourceId::from_bytes),
            },
        ),
        Err(MutationError::StaleRevision)
    );
    let excluded = corrected
        .apply(
            4,
            60,
            WorkspaceMutation::ExcludeSource {
                source_id: id(2, SourceId::from_bytes),
            },
        )
        .unwrap_or(corrected);
    assert!(excluded
        .facts
        .first()
        .is_some_and(|fact| excluded.fact_needs_new_source(fact)));
}

#[test]
fn rename_changes_only_the_display_name_and_revision() {
    let value = snapshot();
    let renamed = value
        .apply(
            3,
            50,
            WorkspaceMutation::Rename {
                display_name: "A clearer name".to_owned(),
            },
        )
        .unwrap_or_else(|_| snapshot());
    assert_eq!(renamed.display_name, "A clearer name");
    assert_eq!(renamed.goal, value.goal);
    assert_eq!(renamed.revision, 4);
    assert_eq!(
        renamed.apply(
            4,
            60,
            WorkspaceMutation::Rename {
                display_name: "A clearer name".to_owned(),
            },
        ),
        Err(MutationError::UnchangedDisplayName)
    );
    assert_eq!(
        renamed.apply(
            4,
            60,
            WorkspaceMutation::Rename {
                display_name: " leading space".to_owned(),
            },
        ),
        Err(MutationError::InvalidDisplayName)
    );
}

#[test]
fn only_terminal_task_drafts_can_be_explicitly_saved_once() {
    let mut draft = snapshot();
    draft.saved = false;
    let saved = draft
        .apply(3, 50, WorkspaceMutation::Save)
        .unwrap_or_else(|_| unreachable!("a partly-done task result can be saved"));
    assert!(saved.saved);
    assert_eq!(saved.revision, 4);
    assert_eq!(saved.last_updated_epoch_ms, 50);
    assert_eq!(
        saved.apply(4, 60, WorkspaceMutation::Save),
        Err(MutationError::AlreadySaved)
    );

    draft.phase = WorkspacePhase::Running;
    assert_eq!(
        draft.apply(3, 50, WorkspaceMutation::Save),
        Err(MutationError::NotSavable)
    );
}

#[test]
fn raw_url_shape_is_rejected_from_display_host() {
    let mut value = snapshot();
    if let Some(source) = value.sources.first_mut() {
        source.host = "https://evidence.example/path?secret=1".to_owned();
    }
    assert!(!value.validate());
    assert!(encode_snapshot(&value).is_err());
}

#[test]
fn page_fact_replacement_is_exact_source_sorted_and_revision_checked() {
    let value = snapshot();
    let source_id = id(2, SourceId::from_bytes);
    let facts = vec![
        page_fact(FactId::from_bytes([5; 16]), source_id, "second"),
        page_fact(FactId::from_bytes([4; 16]), source_id, "first"),
    ];
    let replaced = value
        .apply(
            3,
            50,
            WorkspaceMutation::ReplacePageFacts { source_id, facts },
        )
        .unwrap_or_else(|_| unreachable!("exact cited facts are valid"));
    assert_eq!(replaced.revision, 4);
    assert_eq!(
        replaced
            .facts
            .iter()
            .map(|fact| (fact.fact_id, fact.value.as_str(), fact.sources.clone()))
            .collect::<Vec<_>>(),
        vec![
            (FactId::from_bytes([4; 16]), "first", vec![source_id]),
            (FactId::from_bytes([5; 16]), "second", vec![source_id]),
        ]
    );
    assert_eq!(
        replaced
            .sources
            .first()
            .map(|source| source.read_at_epoch_ms),
        Some(50)
    );
}

#[test]
fn task_phase_and_page_facts_are_one_revision_and_an_unchanged_phase_is_no_mutation() {
    let value = snapshot();
    assert_eq!(
        value.apply(
            3,
            50,
            WorkspaceMutation::SyncTask {
                source: None,
                phase: WorkspacePhase::PartlyDone,
                page_facts: None,
            },
        ),
        Err(MutationError::UnchangedTask)
    );

    let source_id = id(2, SourceId::from_bytes);
    let updated = value
        .apply(
            3,
            60,
            WorkspaceMutation::SyncTask {
                source: None,
                phase: WorkspacePhase::Done,
                page_facts: Some(WorkspacePageFactReplacement {
                    source_id,
                    scope: WorkspacePageFactScope::Dom,
                    facts: vec![page_fact(
                        FactId::from_bytes([7; 16]),
                        source_id,
                        "accepted result",
                    )],
                }),
            },
        )
        .unwrap_or_else(|_| unreachable!("the combined task update is valid"));
    assert_eq!(updated.revision, 4);
    assert_eq!(updated.phase, WorkspacePhase::Done);
    assert_eq!(updated.last_updated_epoch_ms, 60);
    assert_eq!(updated.facts.len(), 1);
    assert_eq!(
        updated.facts.first().map(|fact| fact.value.as_str()),
        Some("accepted result")
    );
    assert_eq!(
        updated
            .sources
            .first()
            .map(|source| source.read_at_epoch_ms),
        Some(60)
    );
}

#[test]
fn empty_excluded_uncited_and_oversized_page_replacements_fail_closed() {
    let value = snapshot();
    let source_id = id(2, SourceId::from_bytes);
    assert_eq!(
        value.apply(
            3,
            50,
            WorkspaceMutation::ReplacePageFacts {
                source_id,
                facts: Vec::new(),
            },
        ),
        Err(MutationError::EmptyPageFacts)
    );
    let uncited = WorkspaceFact {
        sources: Vec::new(),
        ..page_fact(FactId::from_bytes([4; 16]), source_id, "uncited")
    };
    assert_eq!(
        value.apply(
            3,
            50,
            WorkspaceMutation::ReplacePageFacts {
                source_id,
                facts: vec![uncited],
            },
        ),
        Err(MutationError::InvalidSnapshot)
    );
    let oversized = (0..=MAX_FACTS)
        .map(|ordinal| {
            page_fact(
                FactId::from_bytes(u128::try_from(ordinal).unwrap_or(u128::MAX).to_be_bytes()),
                source_id,
                "bounded",
            )
        })
        .collect();
    assert_eq!(
        value.apply(
            3,
            50,
            WorkspaceMutation::ReplacePageFacts {
                source_id,
                facts: oversized,
            },
        ),
        Err(MutationError::TooManyFacts)
    );

    let mut excluded = value;
    if let Some(source) = excluded.sources.first_mut() {
        source.excluded = true;
    }
    assert_eq!(
        excluded.apply(
            3,
            50,
            WorkspaceMutation::ReplacePageFacts {
                source_id,
                facts: vec![page_fact(
                    FactId::from_bytes([4; 16]),
                    source_id,
                    "excluded",
                )],
            },
        ),
        Err(MutationError::SourceExcluded)
    );
}

fn page_fact(fact_id: FactId, source_id: SourceId, value: &str) -> WorkspaceFact {
    WorkspaceFact {
        fact_id,
        field: "page".to_owned(),
        value: value.to_owned(),
        kind: FactKind::FromPage,
        sources: vec![source_id],
        correction: None,
        has_conflict: false,
        media_provenance: None,
    }
}

#[test]
fn new_task_source_and_its_facts_are_one_revision_without_rewriting_history() {
    let value = snapshot();
    let original = value
        .sources
        .first()
        .unwrap_or_else(|| unreachable!("fixture source"));
    let mut source = original.clone();
    source.source_id = SourceId::from_bytes([9; 16]);
    source.read_at_epoch_ms = 0;
    let replacement = WorkspacePageFactReplacement {
        source_id: source.source_id,
        scope: WorkspacePageFactScope::Dom,
        facts: vec![page_fact(
            FactId::from_bytes([9; 16]),
            source.source_id,
            "new page",
        )],
    };
    let updated = value
        .apply(
            value.revision,
            60,
            WorkspaceMutation::SyncTask {
                phase: WorkspacePhase::Done,
                source: Some(source.clone()),
                page_facts: Some(replacement.clone()),
            },
        )
        .unwrap_or_else(|_| unreachable!("valid task source and page facts"));
    assert_eq!(updated.revision, value.revision + 1);
    assert_eq!(updated.source(original.source_id), Some(original));
    assert_eq!(
        updated
            .source(source.source_id)
            .map(|row| row.read_at_epoch_ms),
        Some(60)
    );
    for mutation in 0..4 {
        let mut invalid = source.clone();
        match mutation {
            0 => invalid.source_id = original.source_id,
            1 => invalid.excluded = true,
            2 => invalid.read_at_epoch_ms = 1,
            _ => invalid.canonical_locator = Some("https://evidence.example/report#private".into()),
        }
        assert!(value
            .apply(
                value.revision,
                60,
                WorkspaceMutation::SyncTask {
                    phase: WorkspacePhase::Done,
                    source: Some(invalid),
                    page_facts: Some(replacement.clone()),
                }
            )
            .is_err());
    }
}
