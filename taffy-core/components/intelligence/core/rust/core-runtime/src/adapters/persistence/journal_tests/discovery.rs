// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Discovery-tab authority persistence and replay.

use super::*;

fn discovery_creation() -> (Creation, Effect) {
    let mut seed = seed();
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.milestone = Milestone::M5;
    seed.budgets = TaskBudgets::none()
        .with(BudgetKind::MaxSources, 4)
        .with(BudgetKind::MaxModelRequests, 64);
    let key = IdempotencyKey::new("create-discovery");
    let trace = TraceId::new("trace-create-discovery");
    let mut reducer = Reducer::create(
        seed.clone(),
        BudgetDefaults::uniform(0),
        ManualClock::at(1_000),
        SequentialIds::new(),
        key.clone(),
        trace.clone(),
    );
    let preview = task_engine::ScopePreview {
        scope: SourceScope::new(),
        sources: Vec::new(),
        source_discovery_enabled: true,
        new_source_cap: 4,
        provider_route: ProviderRouteId::new("direct_user_key").ok(),
        budgets: seed.budgets.clone(),
    };
    let start = reducer
        .apply(CommandEnvelope::new(
            IdempotencyKey::new("start-discovery"),
            reducer.task().revision(),
            TraceId::new("trace-start-discovery"),
            Command::StartTask(preview),
        ))
        .unwrap_or_else(|_| unreachable!());
    assert!(start.effects.is_empty());
    let consented = reducer
        .apply(CommandEnvelope::new(
            IdempotencyKey::new("accept-discovery"),
            reducer.task().revision(),
            TraceId::new("trace-accept-discovery"),
            Command::AcceptInitialConsent(ApprovalReceiptReference::new("consent-discovery")),
        ))
        .unwrap_or_else(|_| unreachable!());
    let prepare = Effect::PrepareDiscoveryTab {
        browser_session_id: seed.snapshot.browser_session_id.clone(),
        remaining_new_source_cap: 4,
    };
    assert_eq!(consented.effects, vec![prepare.clone()]);

    let entries = reducer.journal().entries().to_vec();
    let entropy = entropy();
    let commit = TaskCreationCommit {
        seed: &seed,
        creation_key: &key,
        trace_id: &trace,
        id_entropy: &entropy,
        resulting_revision: reducer.task().revision(),
        journal_entries: &entries,
        effect_intents: &consented.effects,
    };
    let audit = ProductionAudit::new(Rc::new(ReferenceSha256));
    let records = audit
        .encode_task_creation(TaskCreationAudit {
            task_id: &seed.task_id,
            creation_key: &key,
            trace_id: &trace,
            resulting_revision: reducer.task().revision(),
            journal_entries: &entries,
        })
        .unwrap_or_else(|_| unreachable!());
    let payload = ProductionStorage::new()
        .and_then(|storage| storage.encode_task_creation(&commit, &records))
        .unwrap_or_else(|_| unreachable!());
    let batch = wire::CommittedTaskBatch {
        effect_id: "commit-create-discovery".to_owned(),
        expected_revision: 0,
        resulting_revision: reducer.task().revision(),
        transaction_batch: payload.as_bytes().to_vec(),
    };
    (
        Creation {
            seed,
            reducer,
            entropy,
            batch,
        },
        prepare,
    )
}

#[test]
fn committed_zero_source_creation_republishes_exactly_one_prepare_after_a_crash() {
    let (creation, prepare) = discovery_creation();
    let persisted = wire::decode_transaction_batch(&creation.batch.transaction_batch)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        persisted
            .effect_intents
            .into_iter()
            .map(uneffect)
            .collect::<Result<Vec<_>, _>>(),
        Ok(vec![prepare.clone()])
    );

    let restored = decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()]))
        .unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        restored.unresolved_effects,
        Some(super::super::RestoredTaskEffectBatch {
            parent_operation_id,
            task_revision,
            effects,
        }) if parent_operation_id == "commit-create-discovery"
            && task_revision == creation.reducer.task().revision()
            && effects == vec![prepare]
    ));
}

#[test]
fn committed_discovery_tab_terminal_suppresses_prepare_and_replays_the_exact_tab() {
    let (mut creation, _) = discovery_creation();
    let previous_revision = creation.reducer.task().revision();
    let previous_len = creation.reducer.journal().len();
    let command = CommandEnvelope::new(
        IdempotencyKey::new("record-discovery-tab"),
        previous_revision,
        TraceId::new("trace-record-discovery-tab"),
        Command::RecordDiscoveryTab {
            discovery_tab_id: TabId::new("discovery-tab-1"),
            browser_session_id: creation.seed.snapshot.browser_session_id.clone(),
        },
    );
    let accepted = creation
        .reducer
        .apply(command.clone())
        .unwrap_or_else(|_| unreachable!());
    assert!(accepted.effects.is_empty());
    let entries = creation
        .reducer
        .journal()
        .entries()
        .get(previous_len..)
        .unwrap_or_else(|| unreachable!());
    let commit = StorageCommit {
        task_id: &creation.seed.task_id,
        workspace_id: None,
        command: &command,
        previous_revision,
        resulting_revision: accepted.revision,
        journal_entries: entries,
        events: &accepted.events,
        effect_intents: &accepted.effects,
        idempotency_key: &command.idempotency_key,
        trace_id: &command.trace_id,
    };
    let audit = ProductionAudit::new(Rc::new(ReferenceSha256));
    let records = audit
        .encode_record(&commit)
        .unwrap_or_else(|_| unreachable!());
    let payload = ProductionStorage::new()
        .and_then(|storage| storage.encode_commit(&commit, &records))
        .unwrap_or_else(|_| unreachable!());
    let second = wire::CommittedTaskBatch {
        effect_id: "commit-record-discovery-tab".to_owned(),
        expected_revision: previous_revision,
        resulting_revision: accepted.revision,
        transaction_batch: payload.as_bytes().to_vec(),
    };

    let restored = decode_task_restore(&restore_record(
        &creation,
        vec![creation.batch.clone(), second],
    ))
    .unwrap_or_else(|_| unreachable!());
    assert!(restored.unresolved_effects.is_none());
    let TaskEngineLoad::Replay { seed, journal, .. } = restored.load else {
        unreachable!("restore always returns replay")
    };
    let (replayed, _) = Reducer::replay(
        *seed,
        BudgetDefaults::uniform(0),
        ManualClock::at(1_000),
        SequentialIds::new(),
        &journal,
    )
    .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        replayed.task().snapshot().discovery_tab_id,
        Some(TabId::new("discovery-tab-1"))
    );
}

#[test]
fn discovery_command_effect_terminal_snapshot_and_audit_round_trip_exactly() {
    let browser_session_id =
        BrowserSessionId::new("browser-session-discovery").unwrap_or_else(|_| unreachable!());
    let discovery_tab_id = TabId::new("discovery-tab-1");

    let record_tab = Command::RecordDiscoveryTab {
        discovery_tab_id: discovery_tab_id.clone(),
        browser_session_id: browser_session_id.clone(),
    };
    assert_eq!(
        uncommand(command(&record_tab).unwrap_or_else(|_| unreachable!())),
        Ok(record_tab)
    );

    let prepare_tab = Effect::PrepareDiscoveryTab {
        browser_session_id: browser_session_id.clone(),
        remaining_new_source_cap: 4,
    };
    assert_eq!(uneffect(effect(&prepare_tab)), Ok(prepare_tab));

    let discovered_source = ConsentedSource {
        source_id: SourceId::from_bytes([41; 16]),
        tab_id: discovery_tab_id.clone(),
        normalized_origin: "https://search.example".to_owned(),
        canonical_locator: None,
    };
    let terminal = ActionOutcome {
        code: ActionResultCode::Verified,
        dispatch_id: Some(DispatchId::new("dispatch-discovery")),
        observed_at: MonotonicMillis(2_000),
        observation: None,
        discovered_source: Some(discovered_source.clone()),
    };
    assert_eq!(unoutcome(outcome(&terminal)), Ok(terminal));

    let mut discovery_seed = seed();
    discovery_seed.snapshot.template_id = TaskTemplateId::WebErrand;
    discovery_seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    discovery_seed.snapshot.consented_sources = vec![discovered_source];
    discovery_seed.snapshot.source_discovery_enabled = true;
    discovery_seed.snapshot.remaining_new_source_cap = 3;
    discovery_seed.snapshot.discovery_tab_id = Some(discovery_tab_id.clone());
    discovery_seed.snapshot.milestone = Milestone::M5;
    discovery_seed.budgets = TaskBudgets::none()
        .with(BudgetKind::MaxSources, 4)
        .with(BudgetKind::MaxModelRequests, 64);
    assert_eq!(unseed(persisted_seed(&discovery_seed)), Ok(discovery_seed));

    let event = JournalEntry::Event(task_engine::journal::EventRecord {
        sequence: 1,
        event: TaskEvent::record(
            EventKind::DiscoveryTabPrepared,
            CommandKind::RecordDiscoveryTab,
        )
        .about(EventSubject::DiscoveryTab(discovery_tab_id)),
        revision: 1,
        trace_id: TraceId::new("trace-discovery"),
        causation_key: IdempotencyKey::new("record-discovery-tab"),
        recorded_at: UtcMillis(3_000),
    });
    assert_eq!(
        unjournal_entry(journal_entry(&event).unwrap_or_else(|_| unreachable!())),
        Ok(event.clone())
    );

    let audit = ProductionAudit::new(Rc::new(ReferenceSha256));
    let creation_key = IdempotencyKey::new("audit-discovery");
    let trace_id = TraceId::new("trace-discovery");
    let records = audit
        .encode_task_creation(TaskCreationAudit {
            task_id: &TaskId::new("task-discovery"),
            creation_key: &creation_key,
            trace_id: &trace_id,
            resulting_revision: 1,
            journal_entries: &[event],
        })
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(records.len(), 1);
    assert_eq!(
        records[0].event_type,
        wire::PersistedAuditEventType::DiscoveryTabPrepared
    );
    assert_eq!(
        records[0].subject_kind,
        Some(wire::PersistedAuditSubjectKind::DiscoveryTab)
    );
}

#[test]
fn invalid_persisted_discovery_authority_never_rehydrates() {
    for remaining_new_source_cap in [0, 9] {
        assert!(uneffect(wire::PersistedEffectIntent::PrepareDiscoveryTab {
            browser_session_id: "browser-session".to_owned(),
            remaining_new_source_cap,
        })
        .is_err());
    }
    assert!(uneffect(wire::PersistedEffectIntent::PrepareDiscoveryTab {
        browser_session_id: String::new(),
        remaining_new_source_cap: 4,
    })
    .is_err());
    assert!(uncommand(wire::PersistedCommand::RecordDiscoveryTab {
        discovery_tab_id: "not/a/tab".to_owned(),
        browser_session_id: "browser-session".to_owned(),
    })
    .is_err());
    assert!(uncommand(wire::PersistedCommand::RecordDiscoveryTab {
        discovery_tab_id: "discovery-tab".to_owned(),
        browser_session_id: String::new(),
    })
    .is_err());
}
