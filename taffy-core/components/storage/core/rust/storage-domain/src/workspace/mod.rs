// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical portable workspace snapshots and deterministic mutations.

mod codec;
mod model;
mod reducer;

pub use codec::{decode_snapshot, encode_snapshot, WorkspaceCodecError, MAX_SNAPSHOT_BYTES};
pub use model::{
    initial_display_name, FactKind, WorkspaceFact, WorkspaceMediaEvidenceKind,
    WorkspaceMediaFactKind, WorkspaceMediaKind, WorkspaceMediaProvenance, WorkspacePageFactScope,
    WorkspacePhase, WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate, MAX_CONFIDENCE_PPM,
    MAX_DISPLAY_NAME_BYTES, MAX_FACTS, MAX_FACT_SOURCES, MAX_FIELD_BYTES, MAX_HOST_BYTES,
    MAX_MEDIA_LOCATOR_BYTES, MAX_SOURCES, MAX_SOURCE_LOCATOR_BYTES, MAX_TITLE_BYTES,
    MAX_VALUE_BYTES,
};
pub use reducer::{MutationError, WorkspaceMutation, WorkspacePageFactReplacement};
