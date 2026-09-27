// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact transient page facts shared by procedure recording and replay.

use task_engine::action::ObservedNodeHandle;
use task_engine::{ConsentedSource, PageObservationEvidence};

use super::LivePage;
use crate::context::arena::ArenaNode;

pub(crate) struct ProcedurePage<'a> {
    pub evidence: &'a PageObservationEvidence,
    pub nodes: Vec<procedure_engine::ReplayNode<'a>>,
}

impl LivePage {
    /// A recorded entry address can match the current origin and an actual
    /// document root. Multiple sources or an incomplete graph cannot supply
    /// that matching condition.
    pub fn recording_origin_clause(&self) -> Option<(String, procedure_engine::MatchClause)> {
        let [page] = self.pages.as_slice() else {
            return None;
        };
        (page.evidence.supports_complete_result()
            && page
                .arena
                .nodes()
                .iter()
                .any(|node| node.role == bip_types::snapshot::SemanticRole::Document))
        .then(|| {
            (
                page.evidence.normalized_origin.clone(),
                procedure_engine::MatchClause::RolePresent(
                    bip_types::snapshot::SemanticRole::Document,
                ),
            )
        })
    }

    /// Borrows one complete consented page; no number is issued or reused.
    pub(crate) fn procedure_page(&self, source: &ConsentedSource) -> Option<ProcedurePage<'_>> {
        let page = self.pages.iter().find(|page| {
            page.source_id == source.source_id
                && page.evidence.tab_id == source.tab_id
                && page.evidence.normalized_origin == source.normalized_origin
                && page.evidence.supports_complete_result()
        })?;
        let nodes = page
            .arena
            .nodes()
            .iter()
            .filter(|node| {
                !node.name_withheld
                    && node.sensitivity == bip_types::snapshot::Sensitivity::NotSensitive
            })
            .filter_map(|node| {
                Some(procedure_engine::ReplayNode {
                    handle: ObservedNodeHandle::from_node_handle(
                        &page.identity.node_handle(&node.node_id),
                    )?,
                    role: node.role,
                    label: node.name.as_deref()?,
                    states: &node.states,
                })
            })
            .collect();
        Some(ProcedurePage {
            evidence: &page.evidence,
            nodes,
        })
    }

    /// The node behind a verified operation's complete observation identity.
    /// A same-named control from a different document, frame or graph revision
    /// cannot describe the operation that was just verified.
    pub fn observed_node(&self, target: &ObservedNodeHandle) -> Option<&ArenaNode> {
        let mut candidates = self.pages.iter().filter_map(|page| {
            let actual = ObservedNodeHandle::from_node_handle(
                &page.identity.node_handle(target.node_id().as_str()),
            )?;
            if &actual != target || !page.evidence.supports_complete_result() {
                return None;
            }
            let mut nodes = page
                .arena
                .nodes()
                .iter()
                .filter(|node| node.node_id == target.node_id().as_str());
            let node = nodes.next()?;
            nodes.next().is_none().then_some(node)
        });
        let node = candidates.next()?;
        candidates.next().is_none().then_some(node)
    }
}
