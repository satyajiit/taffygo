// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Contract-owned batch encoding and browser-committed task reconstruction.

use std::collections::BTreeSet;

use audit_engine::redaction::{optional_identifier, required_identifier};
use core_service_types as wire;
use task_engine::{JournalEntry, TaskJournal};

use crate::contract::{BoundedPayload, PayloadLimit, MAX_PAYLOAD_BYTES};
use crate::ports::{
    PortError, StorageCommit, StorageDomainPort, TaskCreationCommit, TaskEngineLoad, TaskIdEntropy,
};

use super::effect::{effect, uneffect};
use super::event::{journal_entry, unjournal_entry};
use super::restore::{DecodedTaskRestore, RestoredTaskEffectBatch};
use super::value::{seed, unseed};
use super::ConversionError;

const SUPPORTED_TRANSACTION_SCHEMA_VERSION: u32 = 32;
const SUPPORTED_TRANSACTION_MAGIC: &[u8] = b"TAFFYTXN";

/// Why committed browser records could not reconstruct one reducer.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RestoreDecodeError {
    Empty,
    InvalidIdentity,
    DuplicateEffect,
    RevisionGap,
    SeedPlacement,
    Codec,
    InvalidDomain,
    InvalidAudit,
    InvalidJournal,
}

impl RestoreDecodeError {
    /// A short, compiled-in name for why the records did not decode.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Empty => "restore_decode_empty",
            Self::InvalidIdentity => "restore_decode_identity",
            Self::DuplicateEffect => "restore_decode_duplicate_effect",
            Self::RevisionGap => "restore_decode_revision_gap",
            Self::SeedPlacement => "restore_decode_seed_placement",
            Self::Codec => "restore_decode_codec",
            Self::InvalidDomain => "restore_decode_domain",
            Self::InvalidAudit => "restore_decode_audit",
            Self::InvalidJournal => "restore_decode_journal",
        }
    }
}

/// Pure canonical transaction adapter. It owns no physical storage handle.
#[derive(Clone, Copy, Debug)]
pub struct ProductionStorage {
    schema_version: u32,
}

impl ProductionStorage {
    /// Verifies the one generated codec revision compiled into this service.
    pub fn new() -> Result<Self, PortError> {
        if !supports_transaction_codec(
            wire::TRANSACTION_BATCH_SCHEMA_VERSION,
            wire::TRANSACTION_BATCH_MAGIC,
        ) {
            return Err(PortError::InvalidInput);
        }
        Ok(Self {
            schema_version: wire::TRANSACTION_BATCH_SCHEMA_VERSION,
        })
    }

    fn encode(
        self,
        expected_revision: u64,
        resulting_revision: u64,
        seed: Option<wire::PersistedTaskSeed>,
        entries: &[JournalEntry],
        effects: &[task_engine::Effect],
        audit_records: &[wire::PersistedAuditRecord],
    ) -> Result<BoundedPayload, PortError> {
        if self.schema_version != wire::TRANSACTION_BATCH_SCHEMA_VERSION {
            return Err(PortError::InvalidInput);
        }
        validate_audit(entries, audit_records)?;
        let mut persisted_entries = Vec::with_capacity(entries.len());
        for entry in entries {
            persisted_entries.push(journal_entry(entry).map_err(map_conversion)?);
        }
        let persisted_effects = effects.iter().map(effect).collect::<Vec<_>>();
        let event_count = entries
            .iter()
            .filter(|entry| matches!(entry, JournalEntry::Event(_)))
            .count();
        taffy_storage::core_journal::validate_commit_shape(
            taffy_storage::core_journal::CommitShape {
                expected_revision,
                resulting_revision,
                has_seed: seed.is_some(),
                journal_entry_count: persisted_entries.len(),
                event_count,
                effect_intent_count: persisted_effects.len(),
                audit_record_count: audit_records.len(),
            },
        )
        .map_err(|_| PortError::InvalidInput)?;
        let batch = wire::TaskTransactionBatch {
            schema_version: wire::TRANSACTION_BATCH_SCHEMA_VERSION,
            seed,
            journal_entries: persisted_entries,
            effect_intents: persisted_effects,
            audit_records: audit_records.to_vec(),
        };
        let bytes = wire::encode_transaction_batch(&batch).map_err(|_| PortError::InvalidInput)?;
        let limit = PayloadLimit::new(MAX_PAYLOAD_BYTES).map_err(|_| PortError::InvalidInput)?;
        BoundedPayload::new(bytes, limit).map_err(|_| PortError::InvalidInput)
    }
}

fn supports_transaction_codec(schema_version: u32, magic: &[u8]) -> bool {
    schema_version == SUPPORTED_TRANSACTION_SCHEMA_VERSION && magic == SUPPORTED_TRANSACTION_MAGIC
}

impl StorageDomainPort for ProductionStorage {
    fn encode_task_creation(
        &self,
        commit: &TaskCreationCommit<'_>,
        audit_records: &[wire::PersistedAuditRecord],
    ) -> Result<BoundedPayload, PortError> {
        (*self).encode(
            0,
            commit.resulting_revision,
            Some(seed(commit.seed)),
            commit.journal_entries,
            commit.effect_intents,
            audit_records,
        )
    }

    fn encode_commit(
        &self,
        commit: &StorageCommit<'_>,
        audit_records: &[wire::PersistedAuditRecord],
    ) -> Result<BoundedPayload, PortError> {
        (*self).encode(
            commit.previous_revision,
            commit.resulting_revision,
            None,
            commit.journal_entries,
            commit.effect_intents,
            audit_records,
        )
    }
}

/// Decodes the browser single-writer's ordered batches into canonical replay.
pub fn decode_task_restore(
    record: &wire::TaskRestoreRecord,
) -> Result<DecodedTaskRestore, RestoreDecodeError> {
    if record.task_id.is_empty() || record.batches.is_empty() {
        return Err(RestoreDecodeError::Empty);
    }
    let id_entropy =
        TaskIdEntropy::new(record.task_id_seed).map_err(|_| RestoreDecodeError::InvalidIdentity)?;
    let mut expected_revision = 0_u64;
    let mut effect_ids = BTreeSet::new();
    let mut task_seed = None;
    let mut entries = Vec::new();
    let mut unresolved_effects = None;
    for (index, committed) in record.batches.iter().enumerate() {
        if committed.effect_id.is_empty() || !effect_ids.insert(&committed.effect_id) {
            return Err(RestoreDecodeError::DuplicateEffect);
        }
        if committed.expected_revision != expected_revision
            || committed.resulting_revision <= committed.expected_revision
        {
            return Err(RestoreDecodeError::RevisionGap);
        }
        let batch = wire::decode_transaction_batch(&committed.transaction_batch)
            .map_err(|_| RestoreDecodeError::Codec)?;
        let has_seed = batch.seed.is_some();
        if has_seed != (index == 0) {
            return Err(RestoreDecodeError::SeedPlacement);
        }
        let event_count = batch
            .journal_entries
            .iter()
            .filter(|entry| matches!(entry, wire::PersistedJournalEntry::Event { .. }))
            .count();
        taffy_storage::core_journal::validate_commit_shape(
            taffy_storage::core_journal::CommitShape {
                expected_revision: committed.expected_revision,
                resulting_revision: committed.resulting_revision,
                has_seed,
                journal_entry_count: batch.journal_entries.len(),
                event_count,
                effect_intent_count: batch.effect_intents.len(),
                audit_record_count: batch.audit_records.len(),
            },
        )
        .map_err(|_| RestoreDecodeError::InvalidDomain)?;
        validate_persisted_audit(
            &record.task_id,
            &batch.journal_entries,
            &batch.audit_records,
        )?;
        let decoded_effects = batch
            .effect_intents
            .into_iter()
            .map(|effect| uneffect(effect).map_err(|_| RestoreDecodeError::InvalidDomain))
            .collect::<Result<Vec<_>, _>>()?;
        unresolved_effects = (!decoded_effects.is_empty()).then(|| RestoredTaskEffectBatch {
            parent_operation_id: committed.effect_id.clone(),
            task_revision: committed.resulting_revision,
            effects: decoded_effects,
        });
        if let Some(seed) = batch.seed {
            let decoded = unseed(seed).map_err(|_| RestoreDecodeError::InvalidDomain)?;
            if decoded.task_id.as_str() != record.task_id {
                return Err(RestoreDecodeError::InvalidIdentity);
            }
            task_seed = Some(decoded);
        }
        for entry in batch.journal_entries {
            entries.push(unjournal_entry(entry).map_err(|_| RestoreDecodeError::InvalidDomain)?);
        }
        expected_revision = committed.resulting_revision;
    }
    let seed = task_seed.ok_or(RestoreDecodeError::SeedPlacement)?;
    let journal =
        TaskJournal::from_entries(&entries).map_err(|_| RestoreDecodeError::InvalidJournal)?;
    if journal.revision() != expected_revision {
        return Err(RestoreDecodeError::RevisionGap);
    }
    Ok(DecodedTaskRestore {
        load: TaskEngineLoad::replay(seed, journal, id_entropy),
        unresolved_effects,
    })
}

fn validate_audit(
    entries: &[JournalEntry],
    audit: &[wire::PersistedAuditRecord],
) -> Result<(), PortError> {
    let events = entries.iter().filter_map(JournalEntry::as_event);
    if events.count() != audit.len() {
        return Err(PortError::InvalidInput);
    }
    for (event, audit) in entries.iter().filter_map(JournalEntry::as_event).zip(audit) {
        if audit.task_id.is_empty()
            || audit.revision != event.revision
            || audit.sequence != event.sequence
            || audit.trace_id != required_identifier(event.trace_id.as_str())
            || audit.occurred_at_utc_ms != event.recorded_at.0
            || audit.content_values_retained
        {
            return Err(PortError::InvalidInput);
        }
    }
    Ok(())
}

fn validate_persisted_audit(
    task_id: &str,
    entries: &[wire::PersistedJournalEntry],
    audit: &[wire::PersistedAuditRecord],
) -> Result<(), RestoreDecodeError> {
    let expected_task_id = optional_identifier(task_id).ok_or(RestoreDecodeError::InvalidAudit)?;
    let events = entries.iter().filter_map(|entry| match entry {
        wire::PersistedJournalEntry::Event { record } => Some(record),
        wire::PersistedJournalEntry::Command { .. } => None,
    });
    if events.clone().count() != audit.len()
        || audit.iter().any(|record| {
            record.task_id != expected_task_id
                || record.event_id.is_empty()
                || record.trace_id.is_empty()
                || record.content_values_retained
                || record.subject_kind.is_some() != record.subject_id.is_some()
        })
    {
        return Err(RestoreDecodeError::InvalidAudit);
    }
    for (event, audit) in events.zip(audit) {
        if audit.revision != event.revision
            || audit.sequence != event.sequence
            || audit.trace_id != required_identifier(&event.trace_id)
            || audit.occurred_at_utc_ms != event.recorded_at_utc_ms
        {
            return Err(RestoreDecodeError::InvalidAudit);
        }
    }
    Ok(())
}

const fn map_conversion(_error: ConversionError) -> PortError {
    PortError::InvalidInput
}

#[cfg(test)]
#[path = "journal_tests.rs"]
mod tests;
