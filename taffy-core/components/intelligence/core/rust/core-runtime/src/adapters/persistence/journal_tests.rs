// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Transaction-codec and unresolved-effect recovery tests.

use std::rc::Rc;

use bip_types::identity::{
    ApprovalReceiptReference, DispatchId, MonotonicMillis, ProfileId, TabId, TaskId,
};
use bip_types::ActionResultCode;
use core_service_types as wire;
use task_engine::{
    ActionOutcome, ArtifactId, ArtifactKind, BrowserSessionId, BudgetDefaults, BudgetKind,
    BuiltinSkillId, BuiltinSkillReference, Command, CommandEnvelope, CommandKind, ConsentedSource,
    ControlMode, Effect, EventKind, EventSubject, IdempotencyKey, JournalEntry, ManualClock,
    Milestone, PolicyVersion, ProviderRouteId, Reducer, SequentialIds, SourceId, SourceScope,
    TaskBudgets, TaskEvent, TaskKind, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId, UtcMillis,
};

use super::{
    decode_task_restore, supports_transaction_codec, ProductionStorage, RestoreDecodeError,
};
use crate::account::crypto::ReferenceSha256;
use crate::adapters::audit::ProductionAudit;
use crate::adapters::persistence::action_value::{outcome, unoutcome};
use crate::adapters::persistence::command::{command, uncommand};
use crate::adapters::persistence::effect::{effect, uneffect};
use crate::adapters::persistence::event::{journal_entry, unjournal_entry};
use crate::adapters::persistence::value::{seed as persisted_seed, unseed};
use crate::ports::{
    AuditPort, StorageCommit, StorageDomainPort, TaskCreationAudit, TaskCreationCommit,
    TaskEngineLoad, TaskIdEntropy,
};

type TestReducer = Reducer<ManualClock, SequentialIds>;

#[path = "journal_tests/discovery.rs"]
mod discovery;
#[path = "journal_tests/field_values.rs"]
mod field_values;
#[path = "journal_tests/replay.rs"]
mod replay;

struct Creation {
    seed: TaskSeed,
    reducer: TestReducer,
    entropy: TaskIdEntropy,
    batch: wire::CommittedTaskBatch,
}

fn entropy() -> TaskIdEntropy {
    let bytes = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    TaskIdEntropy::new(bytes).unwrap_or_else(|_| unreachable!())
}

#[test]
fn production_storage_rejects_every_transaction_codec_mismatch() {
    assert!(supports_transaction_codec(
        wire::TRANSACTION_BATCH_SCHEMA_VERSION,
        wire::TRANSACTION_BATCH_MAGIC
    ));
    assert!(!supports_transaction_codec(
        wire::TRANSACTION_BATCH_SCHEMA_VERSION.saturating_sub(1),
        wire::TRANSACTION_BATCH_MAGIC
    ));
    assert!(!supports_transaction_codec(
        wire::TRANSACTION_BATCH_SCHEMA_VERSION,
        b"TAFFYBAD"
    ));
}

fn seed() -> TaskSeed {
    TaskSeed {
        task_id: TaskId::new("task-round-trip"),
        workspace_id: None,
        browser_profile_id: ProfileId::new("profile-round-trip"),
        kind: TaskKind::Research,
        user_goal: "bounded goal".to_owned(),
        control_mode: ControlMode::Assistant,
        snapshot: TaskSnapshot {
            template_id: TaskTemplateId::CompareProducts,
            assistant_config_version: 1,
            skill_version_id: None,
            builtin_skill: None,
            tool_allowlist: Vec::new(),
            capability_policy_version: PolicyVersion(1),
            provider_route: None,
            consented_sources: Vec::new(),
            source_discovery_enabled: false,
            remaining_new_source_cap: 0,
            discovery_tab_id: None,
            library_refresh: None,
            browser_session_id: task_engine::BrowserSessionId::new("browser-session-1")
                .unwrap_or_else(|_| unreachable!()),
            milestone: Milestone::M3,
        },
        budgets: TaskBudgets::none(),
        deadline: None,
        deadline_utc: None,
        predecessor_task_id: None,
    }
}

fn creation() -> Creation {
    creation_from_seed(seed())
}

fn creation_from_seed(seed: TaskSeed) -> Creation {
    let key = IdempotencyKey::new("create-round-trip");
    let trace = TraceId::new("trace-round-trip");
    let reducer = Reducer::create(
        seed.clone(),
        BudgetDefaults::uniform(0),
        ManualClock::at(1_000),
        SequentialIds::new(),
        key.clone(),
        trace.clone(),
    );
    let entries = reducer.journal().entries().to_vec();
    let entropy = entropy();
    let commit = TaskCreationCommit {
        seed: &seed,
        creation_key: &key,
        trace_id: &trace,
        id_entropy: &entropy,
        resulting_revision: reducer.task().revision(),
        journal_entries: &entries,
        effect_intents: &[],
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
        effect_id: "commit-create".to_owned(),
        expected_revision: 0,
        resulting_revision: reducer.task().revision(),
        transaction_batch: payload.as_bytes().to_vec(),
    };
    Creation {
        seed,
        reducer,
        entropy,
        batch,
    }
}

fn restore_record(
    creation: &Creation,
    batches: Vec<wire::CommittedTaskBatch>,
) -> wire::TaskRestoreRecord {
    wire::TaskRestoreRecord {
        task_id: creation.seed.task_id.0.clone(),
        batches,
        task_id_seed: *creation.entropy.as_bytes(),
    }
}

#[test]
fn committed_creation_round_trips_through_the_contract_codec() {
    let creation = creation();
    let restored = decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()]));
    assert!(
        matches!(
            &restored,
            Ok(super::DecodedTaskRestore {
                load: TaskEngineLoad::Replay { journal, .. },
                unresolved_effects: None,
            }) if journal == creation.reducer.journal()
        ),
        "restore verdict: {restored:?}"
    );
}

#[test]
fn built_in_binding_round_trips_through_creation_and_restore() {
    let mut seed = seed();
    seed.snapshot.builtin_skill = Some(BuiltinSkillReference {
        skill_id: BuiltinSkillId::WebsiteSummarizer,
        version: 1,
    });
    seed.snapshot.tool_allowlist = vec!["browser.dom.read".to_owned(), "user.handover".to_owned()];
    let creation = creation_from_seed(seed);
    let restored = decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()]))
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        restored.load.builtin_skill_reference(),
        Some(BuiltinSkillReference {
            skill_id: BuiltinSkillId::WebsiteSummarizer,
            version: 1,
        })
    );
    assert!(restored.load.skill_version_id().is_none());
}

#[test]
fn persisted_snapshot_refuses_saved_and_built_in_bindings_together() {
    let mut persisted = persisted_seed(&seed());
    persisted.snapshot.skill_version_id = Some("saved@1".to_owned());
    persisted.snapshot.builtin_skill = Some(wire::PersistedBuiltinSkillReference {
        skill_id: wire::PersistedBuiltinSkillId::WebsiteSummarizer,
        version: 1,
    });
    assert!(unseed(persisted).is_err());
}

#[test]
fn artifact_command_and_effect_persistence_preserve_exact_revision_and_format() {
    let artifact_id = ArtifactId::new("artifact-turn-1-call-0");
    let request = Command::RequestArtifact {
        artifact_id: artifact_id.clone(),
        format: ArtifactKind::Markdown,
        workspace_revision: 7,
    };
    let persisted = command(&request).unwrap_or_else(|_| unreachable!());
    assert_eq!(uncommand(persisted), Ok(request));

    for intent in [
        Effect::GenerateArtifact {
            artifact_id: artifact_id.clone(),
            kind: ArtifactKind::Markdown,
            workspace_revision: 7,
        },
        Effect::ExportArtifact {
            artifact_id: artifact_id.clone(),
            kind: ArtifactKind::Markdown,
            workspace_revision: 7,
        },
    ] {
        assert_eq!(uneffect(effect(&intent)), Ok(intent));
    }

    assert!(uneffect(wire::PersistedEffectIntent::GenerateArtifact {
        artifact_id: artifact_id.as_str().to_owned(),
        kind: wire::PersistedArtifactKind::Markdown,
        workspace_revision: 0,
    })
    .is_err());
    assert!(uncommand(wire::PersistedCommand::RequestArtifact {
        artifact_id: String::new(),
        format: wire::PersistedArtifactKind::Markdown,
        workspace_revision: 7,
    })
    .is_err());
}

#[test]
fn model_subattempt_persistence_preserves_exact_accounting_shape() {
    for kind in [
        task_engine::ModelAttemptKind::Retry,
        task_engine::ModelAttemptKind::Failover,
    ] {
        let request = Command::RequestModelAttempt {
            call_id: task_engine::ModelCallId::new("model-call-1"),
            attempt_ordinal: 2,
            candidate_ordinal: u32::from(kind == task_engine::ModelAttemptKind::Failover),
            kind,
        };
        let persisted = command(&request).unwrap_or_else(|_| unreachable!());
        assert_eq!(uncommand(persisted), Ok(request));
    }
    assert!(uncommand(wire::PersistedCommand::RequestModelAttempt {
        call_id: String::new(),
        attempt_ordinal: 1,
        candidate_ordinal: 0,
        kind: wire::PersistedModelAttemptKind::Retry,
    })
    .is_err());
}

#[test]
fn restore_republishes_only_the_final_committed_nonempty_effect_batch() {
    let mut creation = creation();
    let mut encoded = wire::decode_transaction_batch(&creation.batch.transaction_batch)
        .unwrap_or_else(|_| unreachable!());
    encoded
        .effect_intents
        .push(effect(&Effect::ReleaseTaskTabs));
    creation.batch.transaction_batch =
        wire::encode_transaction_batch(&encoded).unwrap_or_else(|_| unreachable!());

    let restored = decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()]))
        .unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        restored.unresolved_effects,
        Some(super::RestoredTaskEffectBatch {
            parent_operation_id,
            task_revision,
            effects,
        }) if parent_operation_id == "commit-create"
            && task_revision == creation.reducer.task().revision()
            && effects == vec![Effect::ReleaseTaskTabs]
    ));
}

#[test]
fn later_committed_empty_effect_batch_proves_the_prior_effect_was_observed() {
    let mut creation = creation();
    let mut encoded = wire::decode_transaction_batch(&creation.batch.transaction_batch)
        .unwrap_or_else(|_| unreachable!());
    encoded
        .effect_intents
        .push(effect(&Effect::ReleaseTaskTabs));
    creation.batch.transaction_batch =
        wire::encode_transaction_batch(&encoded).unwrap_or_else(|_| unreachable!());

    let previous_revision = creation.reducer.task().revision();
    let previous_len = creation.reducer.journal().len();
    let command = CommandEnvelope::new(
        IdempotencyKey::new("cancel-round-trip"),
        previous_revision,
        TraceId::new("trace-cancel-round-trip"),
        Command::CancelTask,
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
        effect_id: "commit-cancel".to_owned(),
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
    assert!(matches!(
        restored.load,
        TaskEngineLoad::Replay { journal, .. } if journal == *creation.reducer.journal()
    ));
}
