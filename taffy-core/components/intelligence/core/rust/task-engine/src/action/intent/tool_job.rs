// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed intent for one registered isolated Python operation.

use bip_types::identity::{SemanticNodeId, TabId};

use super::OpaqueOperandRef;
use crate::artifact::ArtifactKind;
use crate::tool::{IdempotencyClass, ToolRuntime};

/// Python entrypoints the task surface can name.
///
/// These are operation identifiers, not import paths. The utility binary owns
/// the matching frozen dispatch table and no caller can add another member.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PythonEntrypoint {
    DocumentBuild,
    SpreadsheetBuild,
}

impl PythonEntrypoint {
    pub const fn from_choice(value: &str) -> Option<Self> {
        match value.as_bytes() {
            b"document" => Some(Self::DocumentBuild),
            b"spreadsheet" => Some(Self::SpreadsheetBuild),
            _ => None,
        }
    }

    pub const fn label(self) -> &'static str {
        match self {
            Self::DocumentBuild => "document.build",
            Self::SpreadsheetBuild => "spreadsheet.build",
        }
    }

    /// Exact artifact format produced by this fixed builder.
    pub const fn artifact_kind(self) -> ArtifactKind {
        match self {
            Self::DocumentBuild => ArtifactKind::Docx,
            Self::SpreadsheetBuild => ArtifactKind::Xlsx,
        }
    }

    pub(crate) const fn wire_tag(self) -> u8 {
        match self {
            Self::DocumentBuild => 0,
            Self::SpreadsheetBuild => 1,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::DocumentBuild),
            1 => Some(Self::SpreadsheetBuild),
            _ => None,
        }
    }
}

/// One isolated tool job. Its model-authored operands remain in turn
/// residency; the durable proposal carries only their derived references.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ToolJobIntent {
    pub tool_name: String,
    pub runtime: ToolRuntime,
    pub entrypoint: PythonEntrypoint,
    pub title: OpaqueOperandRef,
    pub content: OpaqueOperandRef,
    pub tab: TabId,
    pub node: Option<SemanticNodeId>,
    pub idempotency: IdempotencyClass,
}
