// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable identity and evidence revision of one generated artifact.

use crate::ids::ArtifactId;

use super::ArtifactKind;

/// Which trusted owner can supply an artifact's bytes at export time.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArtifactCustody {
    /// The deterministic workspace renderer reproduces the accepted revision.
    Workspace,
    /// The browser retained bytes returned by an isolated tool.
    Browser,
}

/// One artifact that a deterministic renderer or isolated tool prepared.
///
/// The bytes stay outside the task journal. Custody is derived again from the
/// journalled command on replay, so an export can select its one legitimate
/// source without persisting content or adding a second wire identity.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArtifactRecord {
    artifact_id: ArtifactId,
    kind: ArtifactKind,
    workspace_revision: u64,
    custody: ArtifactCustody,
}

impl ArtifactRecord {
    /// Records a reproducible render of one exact workspace revision.
    pub const fn workspace(
        artifact_id: ArtifactId,
        kind: ArtifactKind,
        workspace_revision: u64,
    ) -> Self {
        Self {
            artifact_id,
            kind,
            workspace_revision,
            custody: ArtifactCustody::Workspace,
        }
    }

    /// Records bytes retained by the browser after isolated-tool validation.
    pub const fn browser(
        artifact_id: ArtifactId,
        kind: ArtifactKind,
        creation_revision: u64,
    ) -> Self {
        Self {
            artifact_id,
            kind,
            workspace_revision: creation_revision,
            custody: ArtifactCustody::Browser,
        }
    }

    /// Stable artifact identity.
    pub const fn artifact_id(&self) -> &ArtifactId {
        &self.artifact_id
    }

    /// Closed rendered format.
    pub const fn kind(&self) -> ArtifactKind {
        self.kind
    }

    /// Exact workspace revision whose accepted facts produced the bytes.
    pub const fn workspace_revision(&self) -> u64 {
        self.workspace_revision
    }

    /// Trusted owner that can supply the bytes for export.
    pub const fn custody(&self) -> ArtifactCustody {
        self.custody
    }
}
