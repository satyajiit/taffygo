// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing and reading a fact, and the evidence cited for it.
//!
//! The rule this module enforces is written once, here: **an externally
//! derived accepted fact must cite at least one locator.** The fact and its
//! evidence are therefore written in one call, so no caller can be the place
//! the rule was forgotten.

use super::row::{id, timestamp};
use super::types::{Fact, ProvenanceLocator};
use super::vocabulary::{FactClassification, FactStatus, ProvenanceKind, Sensitivity};
use super::RECORD_SCHEMA_VERSION;
use crate::backend::{Executor, Row, Value};
use crate::error::StorageError;
use crate::ids::{FactId, ObservationId, ProvenanceId, SourceId, WorkspaceId};
/// Writes a fact together with the evidence for it.
///
/// An externally derived accepted fact must cite at least one locator, so the
/// two are written in one call and the rule is checked here rather than left to
/// each caller.
pub fn insert_fact(
    executor: &mut dyn Executor,
    fact: &Fact,
    provenance: &[ProvenanceLocator],
) -> Result<(), StorageError> {
    if fact.classification != FactClassification::UserEntered
        && fact.status == FactStatus::Accepted
        && provenance.is_empty()
    {
        return Err(StorageError::Malformed {
            what: "an accepted derived fact",
            detail: "no provenance locator".to_owned(),
        });
    }
    executor.execute(
        "INSERT INTO fact (fact_id, workspace_id, subject_key, predicate, typed_value, unit, \
         classification, confidence_basis_points, observation_time_utc, validity_start_utc, \
         validity_end_utc, sensitivity, status, supersedes_fact_id, retention_class, schema_version) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, NULL, NULL, ?10, ?11, ?12, ?13, ?14)",
        &[
            Value::text(fact.fact_id.to_text()),
            Value::text(fact.workspace_id.to_text()),
            Value::text(fact.subject_key.clone()),
            Value::text(fact.predicate.clone()),
            Value::text(fact.typed_value.clone()),
            Value::maybe_text(fact.unit.clone()),
            Value::text(fact.classification.as_str()),
            fact.confidence_basis_points
                .map_or(Value::Null, Value::Integer),
            Value::text(fact.observation_time.as_str()),
            Value::text(fact.sensitivity.as_str()),
            Value::text(fact.status.as_str()),
            Value::maybe_text(fact.supersedes_fact_id.map(FactId::to_text)),
            Value::text(fact.retention_class.clone()),
            Value::Integer(RECORD_SCHEMA_VERSION),
        ],
    )?;
    for locator in provenance {
        insert_provenance(executor, locator)?;
    }
    Ok(())
}

/// Writes one provenance locator.
pub fn insert_provenance(
    executor: &mut dyn Executor,
    locator: &ProvenanceLocator,
) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO provenance_locator (provenance_id, fact_id, source_id, observation_id, \
         source_kind, location_descriptor, extraction_rule_version, transformation_chain, \
         captured_at_utc) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
        &[
            Value::text(locator.provenance_id.to_text()),
            Value::text(locator.fact_id.to_text()),
            Value::text(locator.source_id.to_text()),
            Value::maybe_text(locator.observation_id.map(ObservationId::to_text)),
            Value::text(locator.kind.as_str()),
            Value::maybe_text(locator.location_descriptor.clone()),
            Value::maybe_text(locator.extraction_rule_version.clone()),
            Value::text(locator.transformation_chain.clone()),
            Value::text(locator.captured_at.as_str()),
        ],
    )?;
    Ok(())
}

const FACT_COLUMNS: &str = "fact_id, workspace_id, subject_key, predicate, typed_value, unit, \
     classification, confidence_basis_points, observation_time_utc, sensitivity, status, \
     supersedes_fact_id, retention_class";

fn read_fact(row: &Row) -> Result<Fact, StorageError> {
    let confidence = match row.value(7)? {
        Value::Null => None,
        Value::Integer(value) => Some(*value),
        _ => {
            return Err(StorageError::ColumnType {
                column: 7,
                expected: "an integer or null",
            })
        }
    };
    let supersedes = match row.maybe_text(11)? {
        None => None,
        Some(raw) => Some(FactId::parse(raw).map_err(|_| StorageError::Malformed {
            what: "a fact id",
            detail: raw.to_owned(),
        })?),
    };
    Ok(Fact {
        fact_id: id(row, 0, |raw| FactId::parse(raw).ok(), "a fact id")?,
        workspace_id: id(row, 1, |raw| WorkspaceId::parse(raw).ok(), "a workspace id")?,
        subject_key: row.text(2)?.to_owned(),
        predicate: row.text(3)?.to_owned(),
        typed_value: row.text(4)?.to_owned(),
        unit: row.maybe_text(5)?.map(str::to_owned),
        classification: FactClassification::read(row, 6)?,
        confidence_basis_points: confidence,
        observation_time: timestamp(row, 8)?,
        sensitivity: Sensitivity::read(row, 9)?,
        status: FactStatus::read(row, 10)?,
        supersedes_fact_id: supersedes,
        retention_class: row.text(12)?.to_owned(),
    })
}

/// Reads one fact.
pub fn load_fact(
    executor: &mut dyn Executor,
    fact_id: FactId,
) -> Result<Option<Fact>, StorageError> {
    let sql = format!("SELECT {FACT_COLUMNS} FROM fact WHERE fact_id = ?1");
    let rows = executor.query(&sql, &[Value::text(fact_id.to_text())])?;
    rows.first().map(read_fact).transpose()
}

/// Every fact in a workspace that has not been removed, in identity order.
pub fn live_facts(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
) -> Result<Vec<Fact>, StorageError> {
    let sql = format!(
        "SELECT {FACT_COLUMNS} FROM fact WHERE workspace_id = ?1 AND status <> 'REMOVED' \
         ORDER BY fact_id"
    );
    let rows = executor.query(&sql, &[Value::text(workspace_id.to_text())])?;
    rows.iter().map(read_fact).collect()
}

/// The evidence cited for a fact, in identity order.
pub fn fact_provenance(
    executor: &mut dyn Executor,
    fact_id: FactId,
) -> Result<Vec<ProvenanceLocator>, StorageError> {
    let rows = executor.query(
        "SELECT provenance_id, fact_id, source_id, observation_id, source_kind, \
         location_descriptor, extraction_rule_version, transformation_chain, captured_at_utc \
         FROM provenance_locator WHERE fact_id = ?1 ORDER BY provenance_id",
        &[Value::text(fact_id.to_text())],
    )?;
    rows.iter()
        .map(|row| {
            let observation = match row.maybe_text(3)? {
                None => None,
                Some(raw) => {
                    Some(
                        ObservationId::parse(raw).map_err(|_| StorageError::Malformed {
                            what: "an observation id",
                            detail: raw.to_owned(),
                        })?,
                    )
                }
            };
            Ok(ProvenanceLocator {
                provenance_id: id(
                    row,
                    0,
                    |raw| ProvenanceId::parse(raw).ok(),
                    "a provenance id",
                )?,
                fact_id: id(row, 1, |raw| FactId::parse(raw).ok(), "a fact id")?,
                source_id: id(row, 2, |raw| SourceId::parse(raw).ok(), "a source id")?,
                observation_id: observation,
                kind: ProvenanceKind::read(row, 4)?,
                location_descriptor: row.maybe_text(5)?.map(str::to_owned),
                extraction_rule_version: row.maybe_text(6)?.map(str::to_owned),
                transformation_chain: row.text(7)?.to_owned(),
                captured_at: timestamp(row, 8)?,
            })
        })
        .collect()
}
