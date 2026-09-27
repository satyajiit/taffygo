// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Workspace-ingestion scope tests.

use bip_types::identity::{SemanticNodeId, TabId};
use loop_kernel::context::MediaObservationKind;
use task_engine::action::{ActionIntent, BrowserIntent};

use super::*;

#[test]
fn whole_page_reads_and_queries_can_stage_the_exact_adopted_page_graph() {
    let tab = TabId::new("tab-1");
    let whole_page = ActionIntent::Browser(BrowserIntent::DomRead {
        tab: tab.clone(),
        target: None,
    });
    let query = ActionIntent::Browser(BrowserIntent::DomQuery {
        tab,
        within: None,
        role: None,
        text: None,
        limit: Some(5),
    });

    assert_eq!(
        workspace_ingestion_scope(&whole_page),
        Some(WorkspaceIngestionScope::Dom)
    );
    assert_eq!(
        workspace_ingestion_scope(&query),
        Some(WorkspaceIngestionScope::Dom)
    );
}

#[test]
fn targeted_reads_stay_excluded_while_media_uses_coordinate_preserving_scopes() {
    let tab = TabId::new("tab-1");
    let targeted = ActionIntent::Browser(BrowserIntent::DomRead {
        tab: tab.clone(),
        target: Some(SemanticNodeId::new("node-1")),
    });
    let media = ActionIntent::Browser(BrowserIntent::PdfInspect { tab });

    assert_eq!(workspace_ingestion_scope(&targeted), None);
    assert_eq!(
        workspace_ingestion_scope(&media),
        Some(WorkspaceIngestionScope::Media(MediaObservationKind::Pdf))
    );
}
