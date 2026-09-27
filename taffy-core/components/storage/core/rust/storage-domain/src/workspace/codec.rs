// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Versioned bounded binary snapshot codec used across the core-service seam.

use crate::ids::{FactId, SourceId, WorkspaceId};

use super::model::{
    initial_display_name, FactKind, WorkspaceFact, WorkspaceMediaEvidenceKind,
    WorkspaceMediaFactKind, WorkspaceMediaKind, WorkspaceMediaProvenance, WorkspacePhase,
    WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate, MAX_DISPLAY_NAME_BYTES, MAX_FACTS,
    MAX_FACT_SOURCES, MAX_FIELD_BYTES, MAX_GOAL_BYTES, MAX_HOST_BYTES, MAX_MEDIA_LOCATOR_BYTES,
    MAX_SOURCES, MAX_SOURCE_LOCATOR_BYTES, MAX_TITLE_BYTES, MAX_VALUE_BYTES,
};

const MAGIC: &[u8; 8] = b"TAFFYWS1";
const LEGACY_SCHEMA_VERSION: u32 = 1;
const DISPLAY_NAME_SCHEMA_VERSION: u32 = 2;
const SAVED_STATE_SCHEMA_VERSION: u32 = 3;
const SOURCE_LOCATOR_SCHEMA_VERSION: u32 = 4;
const SCHEMA_VERSION: u32 = 5;
const ID_BYTES: usize = 32;
pub const MAX_SNAPSHOT_BYTES: usize = 262_144;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspaceCodecError {
    InvalidSnapshot,
    SizeLimit,
    LengthOverflow,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidUtf8,
    InvalidEnum,
    InvalidBoolean,
    InvalidIdentifier,
    TrailingBytes,
}

struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    fn raw(&mut self, value: &[u8]) -> Result<(), WorkspaceCodecError> {
        let length = self
            .bytes
            .len()
            .checked_add(value.len())
            .ok_or(WorkspaceCodecError::LengthOverflow)?;
        if length > MAX_SNAPSHOT_BYTES {
            return Err(WorkspaceCodecError::SizeLimit);
        }
        self.bytes.extend_from_slice(value);
        Ok(())
    }

    fn u8(&mut self, value: u8) -> Result<(), WorkspaceCodecError> {
        self.raw(&[value])
    }

    fn u32(&mut self, value: u32) -> Result<(), WorkspaceCodecError> {
        self.raw(&value.to_le_bytes())
    }

    fn u64(&mut self, value: u64) -> Result<(), WorkspaceCodecError> {
        self.raw(&value.to_le_bytes())
    }

    fn len(&mut self, value: usize, limit: usize) -> Result<(), WorkspaceCodecError> {
        if value > limit {
            return Err(WorkspaceCodecError::SizeLimit);
        }
        self.u32(u32::try_from(value).map_err(|_| WorkspaceCodecError::LengthOverflow)?)
    }

    fn string(&mut self, value: &str, limit: usize) -> Result<(), WorkspaceCodecError> {
        self.len(value.len(), limit)?;
        self.raw(value.as_bytes())
    }

    fn optional_string(
        &mut self,
        value: Option<&str>,
        limit: usize,
    ) -> Result<(), WorkspaceCodecError> {
        self.u8(u8::from(value.is_some()))?;
        if let Some(value) = value {
            self.string(value, limit)?;
        }
        Ok(())
    }
}

struct Reader<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Reader<'a> {
    fn take(&mut self, length: usize) -> Result<&'a [u8], WorkspaceCodecError> {
        let end = self
            .offset
            .checked_add(length)
            .ok_or(WorkspaceCodecError::LengthOverflow)?;
        let value = self
            .bytes
            .get(self.offset..end)
            .ok_or(WorkspaceCodecError::Truncated)?;
        self.offset = end;
        Ok(value)
    }

    fn u8(&mut self) -> Result<u8, WorkspaceCodecError> {
        self.take(1)?
            .first()
            .copied()
            .ok_or(WorkspaceCodecError::Truncated)
    }

    fn u32(&mut self) -> Result<u32, WorkspaceCodecError> {
        let bytes = self
            .take(4)?
            .try_into()
            .map_err(|_| WorkspaceCodecError::Truncated)?;
        Ok(u32::from_le_bytes(bytes))
    }

    fn u64(&mut self) -> Result<u64, WorkspaceCodecError> {
        let bytes = self
            .take(8)?
            .try_into()
            .map_err(|_| WorkspaceCodecError::Truncated)?;
        Ok(u64::from_le_bytes(bytes))
    }

    fn len(&mut self, limit: usize) -> Result<usize, WorkspaceCodecError> {
        let value =
            usize::try_from(self.u32()?).map_err(|_| WorkspaceCodecError::LengthOverflow)?;
        (value <= limit)
            .then_some(value)
            .ok_or(WorkspaceCodecError::SizeLimit)
    }

    fn string(&mut self, limit: usize) -> Result<String, WorkspaceCodecError> {
        let length = self.len(limit)?;
        let value = core::str::from_utf8(self.take(length)?)
            .map_err(|_| WorkspaceCodecError::InvalidUtf8)?;
        Ok(value.to_owned())
    }

    fn bool(&mut self) -> Result<bool, WorkspaceCodecError> {
        match self.u8()? {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(WorkspaceCodecError::InvalidBoolean),
        }
    }

    fn optional_string(&mut self, limit: usize) -> Result<Option<String>, WorkspaceCodecError> {
        self.bool()?.then(|| self.string(limit)).transpose()
    }
}

pub fn encode_snapshot(snapshot: &WorkspaceSnapshot) -> Result<Vec<u8>, WorkspaceCodecError> {
    if !snapshot.validate() {
        return Err(WorkspaceCodecError::InvalidSnapshot);
    }
    let mut writer = Writer { bytes: Vec::new() };
    writer.raw(MAGIC)?;
    writer.u32(SCHEMA_VERSION)?;
    writer.string(&snapshot.workspace_id.to_text(), ID_BYTES)?;
    writer.u64(snapshot.revision)?;
    writer.string(&snapshot.goal, MAX_GOAL_BYTES)?;
    writer.string(&snapshot.display_name, MAX_DISPLAY_NAME_BYTES)?;
    writer.u8(snapshot.phase.wire())?;
    writer.u8(u8::from(snapshot.saved))?;
    writer.u64(snapshot.last_updated_epoch_ms)?;
    writer.u8(snapshot.template.wire())?;
    writer.len(snapshot.sources.len(), MAX_SOURCES)?;
    for source in &snapshot.sources {
        writer.string(&source.source_id.to_text(), ID_BYTES)?;
        writer.string(&source.title, MAX_TITLE_BYTES)?;
        writer.string(&source.host, MAX_HOST_BYTES)?;
        writer.optional_string(
            source.canonical_locator.as_deref(),
            MAX_SOURCE_LOCATOR_BYTES,
        )?;
        writer.u64(source.read_at_epoch_ms)?;
        writer.u8(u8::from(source.excluded))?;
    }
    writer.len(snapshot.facts.len(), MAX_FACTS)?;
    for fact in &snapshot.facts {
        writer.string(&fact.fact_id.to_text(), ID_BYTES)?;
        writer.string(&fact.field, MAX_FIELD_BYTES)?;
        writer.string(&fact.value, MAX_VALUE_BYTES)?;
        writer.u8(fact.kind.wire())?;
        writer.len(fact.sources.len(), MAX_FACT_SOURCES)?;
        for source_id in &fact.sources {
            writer.string(&source_id.to_text(), ID_BYTES)?;
        }
        writer.optional_string(fact.correction.as_deref(), MAX_VALUE_BYTES)?;
        writer.u8(u8::from(fact.has_conflict))?;
        write_media_provenance(&mut writer, fact.media_provenance.as_ref())?;
    }
    Ok(writer.bytes)
}

pub fn decode_snapshot(bytes: &[u8]) -> Result<WorkspaceSnapshot, WorkspaceCodecError> {
    if bytes.len() > MAX_SNAPSHOT_BYTES {
        return Err(WorkspaceCodecError::SizeLimit);
    }
    let mut reader = Reader { bytes, offset: 0 };
    if reader.take(MAGIC.len())? != MAGIC {
        return Err(WorkspaceCodecError::InvalidMagic);
    }
    let schema_version = reader.u32()?;
    if !matches!(
        schema_version,
        LEGACY_SCHEMA_VERSION
            | DISPLAY_NAME_SCHEMA_VERSION
            | SAVED_STATE_SCHEMA_VERSION
            | SOURCE_LOCATOR_SCHEMA_VERSION
            | SCHEMA_VERSION
    ) {
        return Err(WorkspaceCodecError::UnsupportedVersion);
    }
    let workspace_id = WorkspaceId::parse(&reader.string(ID_BYTES)?)
        .map_err(|_| WorkspaceCodecError::InvalidIdentifier)?;
    let revision = reader.u64()?;
    let goal = reader.string(MAX_GOAL_BYTES)?;
    let display_name = if schema_version == LEGACY_SCHEMA_VERSION {
        initial_display_name(&goal)
    } else {
        reader.string(MAX_DISPLAY_NAME_BYTES)?
    };
    let phase = WorkspacePhase::from_wire(reader.u8()?).ok_or(WorkspaceCodecError::InvalidEnum)?;
    // Every snapshot written before explicit-save state existed was already
    // treated as a durable saved workspace. Reading it as saved preserves that
    // visible retention choice; only newly-created v3 task drafts start false.
    let saved = if schema_version < SAVED_STATE_SCHEMA_VERSION {
        true
    } else {
        reader.bool()?
    };
    let last_updated_epoch_ms = reader.u64()?;
    let template =
        WorkspaceTemplate::from_wire(reader.u8()?).ok_or(WorkspaceCodecError::InvalidEnum)?;
    let source_count = reader.len(MAX_SOURCES)?;
    let mut sources = Vec::with_capacity(source_count);
    for _ in 0..source_count {
        sources.push(read_source(&mut reader, schema_version)?);
    }
    let fact_count = reader.len(MAX_FACTS)?;
    let mut facts = Vec::with_capacity(fact_count);
    for _ in 0..fact_count {
        facts.push(read_fact(&mut reader, schema_version)?);
    }
    if reader.offset != bytes.len() {
        return Err(WorkspaceCodecError::TrailingBytes);
    }
    let snapshot = WorkspaceSnapshot {
        workspace_id,
        revision,
        display_name,
        goal,
        phase,
        saved,
        last_updated_epoch_ms,
        template,
        sources,
        facts,
    };
    snapshot
        .validate()
        .then_some(snapshot)
        .ok_or(WorkspaceCodecError::InvalidSnapshot)
}

fn read_source(
    reader: &mut Reader<'_>,
    schema_version: u32,
) -> Result<WorkspaceSource, WorkspaceCodecError> {
    Ok(WorkspaceSource {
        source_id: SourceId::parse(&reader.string(ID_BYTES)?)
            .map_err(|_| WorkspaceCodecError::InvalidIdentifier)?,
        title: reader.string(MAX_TITLE_BYTES)?,
        host: reader.string(MAX_HOST_BYTES)?,
        canonical_locator: if schema_version >= SOURCE_LOCATOR_SCHEMA_VERSION {
            reader.optional_string(MAX_SOURCE_LOCATOR_BYTES)?
        } else {
            None
        },
        read_at_epoch_ms: reader.u64()?,
        excluded: reader.bool()?,
    })
}

fn read_fact(
    reader: &mut Reader<'_>,
    schema_version: u32,
) -> Result<WorkspaceFact, WorkspaceCodecError> {
    let fact_id = FactId::parse(&reader.string(ID_BYTES)?)
        .map_err(|_| WorkspaceCodecError::InvalidIdentifier)?;
    let field = reader.string(MAX_FIELD_BYTES)?;
    let value = reader.string(MAX_VALUE_BYTES)?;
    let kind = FactKind::from_wire(reader.u8()?).ok_or(WorkspaceCodecError::InvalidEnum)?;
    let source_count = reader.len(MAX_FACT_SOURCES)?;
    let mut sources = Vec::with_capacity(source_count);
    for _ in 0..source_count {
        sources.push(
            SourceId::parse(&reader.string(ID_BYTES)?)
                .map_err(|_| WorkspaceCodecError::InvalidIdentifier)?,
        );
    }
    Ok(WorkspaceFact {
        fact_id,
        field,
        value,
        kind,
        sources,
        correction: reader.optional_string(MAX_VALUE_BYTES)?,
        has_conflict: reader.bool()?,
        media_provenance: if schema_version >= SCHEMA_VERSION {
            read_media_provenance(reader)?
        } else {
            None
        },
    })
}

fn write_media_provenance(
    writer: &mut Writer,
    value: Option<&WorkspaceMediaProvenance>,
) -> Result<(), WorkspaceCodecError> {
    writer.u8(u8::from(value.is_some()))?;
    let Some(value) = value else {
        return Ok(());
    };
    writer.u8(value.media_kind.wire())?;
    writer.u8(value.fact_kind.wire())?;
    writer.u8(value.evidence_kind.wire())?;
    writer.string(&value.source_locator, MAX_MEDIA_LOCATOR_BYTES)?;
    writer.u32(value.source_start)?;
    writer.u32(value.source_end)?;
    writer.u32(value.page_index_plus_one)?;
    writer.u64(value.timestamp_start_ms)?;
    writer.u64(value.timestamp_end_ms)?;
    writer.u32(value.row_index_plus_one)?;
    writer.u32(value.confidence_ppm)?;
    writer.u8(u8::from(value.truncated))
}

fn read_media_provenance(
    reader: &mut Reader<'_>,
) -> Result<Option<WorkspaceMediaProvenance>, WorkspaceCodecError> {
    if !reader.bool()? {
        return Ok(None);
    }
    Ok(Some(WorkspaceMediaProvenance {
        media_kind: WorkspaceMediaKind::from_wire(reader.u8()?)
            .ok_or(WorkspaceCodecError::InvalidEnum)?,
        fact_kind: WorkspaceMediaFactKind::from_wire(reader.u8()?)
            .ok_or(WorkspaceCodecError::InvalidEnum)?,
        evidence_kind: WorkspaceMediaEvidenceKind::from_wire(reader.u8()?)
            .ok_or(WorkspaceCodecError::InvalidEnum)?,
        source_locator: reader.string(MAX_MEDIA_LOCATOR_BYTES)?,
        source_start: reader.u32()?,
        source_end: reader.u32()?,
        page_index_plus_one: reader.u32()?,
        timestamp_start_ms: reader.u64()?,
        timestamp_end_ms: reader.u64()?,
        row_index_plus_one: reader.u32()?,
        confidence_ppm: reader.u32()?,
        truncated: reader.bool()?,
    }))
}
