// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded residency for model-authored action operands.
//!
//! These bytes live with one model reply and are never part of reducer state.
//! A durable proposal carries only [`OpaqueOperandRef`], whose derived handle
//! identifies the turn-call slot and whose digest detects substitution.

use std::collections::BTreeMap;

use crate::action::{OpaqueOperandKind, OpaqueOperandRef};
use crate::tool::{ArgumentValue, MAX_ARGUMENT_VALUE_BYTES};
use crate::workflow::WorkflowDigest;

use super::reply::ModelReply;

/// Why transient bytes could not be bound or recovered.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OperandResolveError {
    Missing,
    ReferenceMismatch,
    DigestUnavailable,
    DigestMismatch,
}

/// Content that remains resident only for the lifetime of one model turn.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ActionOperands {
    values: BTreeMap<(u32, OpaqueOperandKind), String>,
}

impl ActionOperands {
    pub(super) fn from_reply(reply: &ModelReply) -> Self {
        let mut values = BTreeMap::new();
        for (index, call) in reply.tool_calls.iter().enumerate() {
            let Ok(sequence) = u32::try_from(index) else {
                break;
            };
            let slots: &[(&str, OpaqueOperandKind)] = match call.tool_name.as_str() {
                "browser.search" => &[("query", OpaqueOperandKind::SearchQuery)],
                "browser.dom.query" => &[("text", OpaqueOperandKind::DomQueryText)],
                "library.search" => &[("query", OpaqueOperandKind::LibraryQuery)],
                "memory.search" => &[("query", OpaqueOperandKind::MemoryQuery)],
                "history.search" | "bookmarks.search" => {
                    &[("query", OpaqueOperandKind::StoreQuery)]
                }
                "memory.save" | "memory.update" => {
                    &[("statement", OpaqueOperandKind::MemoryStatement)]
                }
                "python.execute" => &[
                    ("title", OpaqueOperandKind::PythonTitle),
                    ("content", OpaqueOperandKind::PythonContent),
                ],
                _ => &[],
            };
            for (name, kind) in slots {
                let Some(ArgumentValue::Text(value)) = call
                    .arguments
                    .iter()
                    .find(|argument| argument.name == *name)
                    .map(|argument| &argument.value)
                else {
                    continue;
                };
                if !value.is_empty() && value.len() <= MAX_ARGUMENT_VALUE_BYTES {
                    values.insert((sequence, *kind), value.clone());
                }
            }
        }
        Self { values }
    }

    /// Binds one resident value to a content-free durable reference.
    pub fn bind(
        &self,
        ordinal: u64,
        sequence: u32,
        kind: OpaqueOperandKind,
        digest: &dyn WorkflowDigest,
    ) -> Result<OpaqueOperandRef, OperandResolveError> {
        let value = self
            .values
            .get(&(sequence, kind))
            .ok_or(OperandResolveError::Missing)?;
        let digest = digest
            .sha256(value.as_bytes())
            .map_err(|_| OperandResolveError::DigestUnavailable)?;
        Ok(OpaqueOperandRef::for_call(ordinal, sequence, kind, digest))
    }

    /// Resolves only the exact turn-call/kind and verifies its digest.
    pub fn resolve<'a>(
        &'a self,
        ordinal: u64,
        sequence: u32,
        reference: &OpaqueOperandRef,
        digest: &dyn WorkflowDigest,
    ) -> Result<&'a str, OperandResolveError> {
        if !reference.matches_call(ordinal, sequence) {
            return Err(OperandResolveError::ReferenceMismatch);
        }
        let value = self
            .values
            .get(&(sequence, reference.kind()))
            .ok_or(OperandResolveError::Missing)?;
        let actual = digest
            .sha256(value.as_bytes())
            .map_err(|_| OperandResolveError::DigestUnavailable)?;
        if &actual != reference.digest() {
            return Err(OperandResolveError::DigestMismatch);
        }
        Ok(value)
    }
}

#[cfg(test)]
mod tests;
