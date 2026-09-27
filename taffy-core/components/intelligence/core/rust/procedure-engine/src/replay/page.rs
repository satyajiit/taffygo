// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fresh graph bindings. No node identity or label enters a saved definition.

use bip_types::snapshot::{NodeState, SemanticRole};
use task_engine::action::{LinkHandleOriginKind, ObservedNodeHandle};
use task_engine::BrowserSessionId;
use task_engine::PageObservationEvidence;

use super::ReplayRefusal;
use crate::{classify_phrase, PhraseId};

/// One current semantic node, carrying its entire browser-observed identity.
#[derive(Clone, Debug)]
pub struct ReplayNode<'a> {
    pub handle: ObservedNodeHandle,
    pub role: SemanticRole,
    pub label: &'a str,
    pub states: &'a [NodeState],
}

/// A borrowed complete observation and task-owned download evidence.
#[derive(Clone, Copy, Debug)]
pub struct ReplayPage<'a> {
    pub evidence: &'a PageObservationEvidence,
    pub nodes: &'a [ReplayNode<'a>],
    pub browser_session_id: Option<&'a BrowserSessionId>,
    pub completed_downloads: usize,
    /// Owned created/in-progress rows from a complete current-session table.
    /// None means that table is unavailable or truncated.
    pub pending_downloads: Option<usize>,
}

impl ReplayPage<'_> {
    pub(super) fn target(
        self,
        role: SemanticRole,
        phrase: PhraseId,
    ) -> Result<ObservedNodeHandle, ReplayRefusal> {
        if !self.evidence.supports_complete_result() {
            return Err(ReplayRefusal::HandleBindingUnavailable);
        }
        let mut matches = self
            .nodes
            .iter()
            .filter(|node| node.role == role && classify_phrase(node.label) == Some(phrase));
        let node = matches
            .next()
            .ok_or(ReplayRefusal::HandleBindingUnavailable)?;
        if matches.next().is_some()
            || !node.states.contains(&NodeState::Visible)
            || !node.states.contains(&NodeState::Enabled)
            || node.handle.tab_id() != &self.evidence.tab_id
            || node.handle.frame_id() != &self.evidence.frame_id
            || node.handle.page_epoch() != &self.evidence.page_epoch
            || node.handle.graph_revision().0 != self.evidence.graph_revision
            || node.handle.expected_origin_kind() != LinkHandleOriginKind::Tuple
            || node.handle.expected_origin_value() != self.evidence.normalized_origin
        {
            return Err(ReplayRefusal::HandleBindingUnavailable);
        }
        Ok(node.handle.clone())
    }
}
