// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Corrupt and unresolved model-state replay tests.

use super::*;

#[test]
fn restore_republishes_an_unresolved_model_call() {
    let mut creation = creation();
    let mut encoded = wire::decode_transaction_batch(&creation.batch.transaction_batch)
        .unwrap_or_else(|_| unreachable!());
    encoded
        .effect_intents
        .push(wire::PersistedEffectIntent::CallModel {
            call_id: "model-call-1".to_owned(),
        });
    creation.batch.transaction_batch =
        wire::encode_transaction_batch(&encoded).unwrap_or_else(|_| unreachable!());

    let restored = decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()]))
        .unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        restored.unresolved_effects,
        Some(super::super::RestoredTaskEffectBatch { effects, .. })
            if effects == vec![Effect::CallModel {
                call_id: task_engine::ModelCallId::new("model-call-1"),
            }]
    ));
}

#[test]
fn restore_refuses_a_model_call_with_no_identity() {
    let mut creation = creation();
    let mut encoded = wire::decode_transaction_batch(&creation.batch.transaction_batch)
        .unwrap_or_else(|_| unreachable!());
    encoded
        .effect_intents
        .push(wire::PersistedEffectIntent::CallModel {
            call_id: String::new(),
        });
    creation.batch.transaction_batch =
        wire::encode_transaction_batch(&encoded).unwrap_or_else(|_| unreachable!());

    assert!(matches!(
        decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()])),
        Err(RestoreDecodeError::InvalidDomain)
    ));
}

/// A creation batch that also carries a model turn asked for while the task
/// is still a draft — decodable, and refused `not_started` by a replay.
fn with_a_model_turn_in_draft() -> Creation {
    let mut creation = creation();
    let mut encoded = wire::decode_transaction_batch(&creation.batch.transaction_batch)
        .unwrap_or_else(|_| unreachable!());
    let next_sequence = u64::try_from(encoded.journal_entries.len())
        .unwrap_or_else(|_| unreachable!())
        .saturating_add(1);
    encoded
        .journal_entries
        .push(wire::PersistedJournalEntry::Command {
            record: wire::PersistedCommandRecord {
                sequence: next_sequence,
                envelope: wire::PersistedCommandEnvelope {
                    idempotency_key: "model-turn-1".to_owned(),
                    expected_revision: 1,
                    trace_id: "trace-1".to_owned(),
                    command: wire::PersistedCommand::RequestModelTurn {
                        call_id: "model-call-1".to_owned(),
                    },
                },
                recorded_at_utc_ms: 1,
            },
        });
    creation.batch.transaction_batch =
        wire::encode_transaction_batch(&encoded).unwrap_or_else(|_| unreachable!());
    creation
}

#[test]
fn restore_replays_a_model_turn_command_rather_than_refusing_it() {
    let creation = with_a_model_turn_in_draft();
    let restored = decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()]));
    let Ok(super::super::DecodedTaskRestore {
        load: TaskEngineLoad::Replay { journal, .. },
        ..
    }) = restored
    else {
        unreachable!("a committed batch replays: {restored:?}")
    };
    assert!(journal.commands().any(|record| record.envelope.command
        == task_engine::Command::RequestModelTurn {
            call_id: task_engine::ModelCallId::new("model-call-1"),
        }));
}

#[test]
fn restore_replays_a_context_eviction_command() {
    let mut creation = creation();
    let mut encoded = wire::decode_transaction_batch(&creation.batch.transaction_batch)
        .unwrap_or_else(|_| unreachable!());
    let next_sequence = u64::try_from(encoded.journal_entries.len())
        .unwrap_or_else(|_| unreachable!())
        .saturating_add(1);
    encoded
        .journal_entries
        .push(wire::PersistedJournalEntry::Command {
            record: wire::PersistedCommandRecord {
                sequence: next_sequence,
                envelope: wire::PersistedCommandEnvelope {
                    idempotency_key: "context-eviction-1".to_owned(),
                    expected_revision: 1,
                    trace_id: "trace-1".to_owned(),
                    command: wire::PersistedCommand::RecordContextEviction { through_turn: 3 },
                },
                recorded_at_utc_ms: 1,
            },
        });
    creation.batch.transaction_batch =
        wire::encode_transaction_batch(&encoded).unwrap_or_else(|_| unreachable!());

    let restored = decode_task_restore(&restore_record(&creation, vec![creation.batch.clone()]));
    let Ok(super::super::DecodedTaskRestore {
        load: TaskEngineLoad::Replay { journal, .. },
        ..
    }) = restored
    else {
        unreachable!("a committed batch replays: {restored:?}")
    };
    assert!(journal.commands().any(|record| record.envelope.command
        == task_engine::Command::RecordContextEviction { through_turn: 3 }));
}

#[test]
fn restore_rejects_a_trailing_byte_before_reducer_replay() {
    let record = wire::TaskRestoreRecord {
        task_id: "task".to_owned(),
        batches: vec![wire::CommittedTaskBatch {
            effect_id: "commit".to_owned(),
            expected_revision: 0,
            resulting_revision: 1,
            transaction_batch: b"TAFFYTXN\0".to_vec(),
        }],
        task_id_seed: core::array::from_fn(|index| u8::try_from(index).unwrap_or_default()),
    };
    assert!(matches!(
        decode_task_restore(&record),
        Err(RestoreDecodeError::Codec)
    ));
}

fn profile_runtime() -> crate::ProfileServiceRuntime {
    crate::create_profile_service_runtime(
        crate::ProfileRuntimeConfiguration {
            generation: crate::ServiceGeneration::INITIAL,
            generation_capability_entropy: core::array::from_fn(|index| {
                u8::try_from(index).unwrap_or_default()
            }),
            initial_utc_millis: 1_000,
            private_profile: false,
            browser_profile_id: "profile-round-trip".to_owned(),
            browser_session_id: "browser-session-1".to_owned(),
            available_account_methods: Vec::new(),
            skills: Vec::new(),
            recall: Vec::new(),
            assistant_configuration: None,
        },
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!())
}

/// The record `rust_core_initialization_unittest.cc` refuses, byte for byte,
/// named the way that test now expects its log line to name it. On the phone
/// the same line read `label=` with nothing after it (decision 0235).
#[test]
fn a_restore_that_does_not_decode_names_the_decoder_refusal() {
    let record = wire::TaskRestoreRecord {
        task_id: "task-1".to_owned(),
        batches: vec![wire::CommittedTaskBatch {
            effect_id: "task-effect-1".to_owned(),
            expected_revision: 0,
            resulting_revision: 1,
            transaction_batch: b"TAFFYTXN\0".to_vec(),
        }],
        task_id_seed: core::array::from_fn(|index| {
            u8::try_from(index.saturating_add(1)).unwrap_or_default()
        }),
    };
    assert_eq!(
        profile_runtime().task_restore_refusal(&record),
        Some("restore_decode_codec")
    );
}

/// A journal that decodes and that this build's reducer refuses names the
/// reducer's refusal — the rule of this build the journal ran into.
#[test]
fn a_restore_whose_journal_is_refused_names_the_refusal() {
    let creation = with_a_model_turn_in_draft();
    let record = restore_record(&creation, vec![creation.batch.clone()]);
    assert!(decode_task_restore(&record).is_ok());
    assert_eq!(
        profile_runtime().task_restore_refusal(&record),
        Some(task_engine::RefusalReason::NotStarted.label())
    );
}

/// And a record that decodes and replays has nothing to name, so a refusal
/// past the replay keeps the name its own branch gives it.
#[test]
fn a_restore_that_replays_has_no_refusal_to_name() {
    let creation = creation();
    let record = restore_record(&creation, vec![creation.batch.clone()]);
    assert_eq!(profile_runtime().task_restore_refusal(&record), None);
}
