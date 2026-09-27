// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing an observation.
//!
//! One function, because observations are never updated and never read back
//! through this module: they are the immutable evidence a fact points at.

use super::types::Observation;
use super::RECORD_SCHEMA_VERSION;
use crate::backend::{Executor, Value};
use crate::error::StorageError;
/// Writes an observation. Observations are never updated.
pub fn insert_observation(
    executor: &mut dyn Executor,
    observation: &Observation,
) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO observation (observation_id, source_id, task_id, captured_at_utc, \
         bip_schema_version, page_epoch, graph_revision, content_fingerprint, scope, truncation, \
         redaction_summary, provenance_root, retention_class, encrypted_payload_ref, schema_version) \
         VALUES (?1, ?2, ?3, ?4, NULL, NULL, NULL, NULL, ?5, ?6, ?7, ?8, ?9, ?10, ?11)",
        &[
            Value::text(observation.observation_id.to_text()),
            Value::text(observation.source_id.to_text()),
            Value::maybe_text(observation.task_id.clone()),
            Value::text(observation.captured_at.as_str()),
            Value::text(observation.scope.clone()),
            Value::text(observation.truncation.clone()),
            Value::text(observation.redaction_summary.clone()),
            Value::text(observation.provenance_root.clone()),
            Value::text(observation.retention_class.clone()),
            Value::maybe_text(observation.encrypted_payload_ref.clone()),
            Value::Integer(RECORD_SCHEMA_VERSION),
        ],
    )?;
    Ok(())
}
