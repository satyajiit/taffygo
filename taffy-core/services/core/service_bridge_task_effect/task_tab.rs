// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict decoding of typed assistant-owned tab results.

use core_runtime::{wire, ActionResultCode, FrameId, GraphRevision, PageEpoch};

use super::PendingTaskEffect;
use crate::ffi;

fn fields_are_empty(terminal: &ffi::BridgeTaskTerminal) -> bool {
    terminal.task_tab_browser_session_id.is_empty()
        && terminal.task_tab_operation == 0
        && terminal.task_tab_postcondition == 0
        && terminal.task_tab_snapshots.is_empty()
        && !terminal.has_task_tab_target
        && terminal.task_tab_target_tab_id.is_empty()
        && terminal.task_tab_target_frame_id.is_empty()
        && terminal.task_tab_target_page_epoch.is_empty()
        && terminal.task_tab_target_graph_revision == 0
        && !terminal.task_tab_state_was_already_satisfied
}

fn target(
    terminal: &ffi::BridgeTaskTerminal,
    browser_session_id: core_runtime::BrowserSessionId,
) -> Result<core_runtime::TaskTabTarget, ()> {
    if !terminal.has_task_tab_target
        || terminal.task_tab_target_tab_id.is_empty()
        || terminal.task_tab_target_frame_id.is_empty()
        || terminal.task_tab_target_page_epoch.is_empty()
        || terminal.task_tab_target_graph_revision == 0
    {
        return Err(());
    }
    Ok(core_runtime::TaskTabTarget::new(
        browser_session_id,
        core_runtime::TabId::new(terminal.task_tab_target_tab_id.clone()),
        FrameId(terminal.task_tab_target_frame_id.clone()),
        PageEpoch(terminal.task_tab_target_page_epoch.clone()),
        GraphRevision(terminal.task_tab_target_graph_revision),
    ))
}

pub(super) fn decode(
    pending: &PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    code: ActionResultCode,
) -> Result<Option<core_runtime::TaskTabActionResult>, ()> {
    let operation = pending.action_operation;
    let is_task_tab = matches!(
        operation,
        Some(
            wire::TaskActionOperationKind::TabsList
                | wire::TaskActionOperationKind::TabsActivate
                | wire::TaskActionOperationKind::TabsClose
        )
    );
    if code != ActionResultCode::Verified {
        return (!terminal.has_task_tab_result && fields_are_empty(terminal))
            .then_some(None)
            .ok_or(());
    }
    if terminal.has_task_tab_result != is_task_tab {
        return Err(());
    }
    if !is_task_tab {
        return fields_are_empty(terminal).then_some(None).ok_or(());
    }
    let result_operation =
        wire::TaskActionOperationKind::from_wire(u32::from(terminal.task_tab_operation))
            .ok_or(())?;
    if Some(result_operation) != operation || terminal.task_tab_browser_session_id.is_empty() {
        return Err(());
    }
    let browser_session_id =
        core_runtime::BrowserSessionId::new(terminal.task_tab_browser_session_id.clone())
            .map_err(|_| ())?;
    let postcondition =
        wire::TaskTabPostcondition::from_wire(u32::from(terminal.task_tab_postcondition))
            .ok_or(())?;
    match (result_operation, postcondition) {
        (wire::TaskActionOperationKind::TabsList, wire::TaskTabPostcondition::Listed) => {
            if terminal.has_task_tab_target || terminal.task_tab_state_was_already_satisfied {
                return Err(());
            }
            let tabs = terminal
                .task_tab_snapshots
                .iter()
                .map(|snapshot| {
                    if snapshot.tab_id.is_empty()
                        || snapshot.frame_id.is_empty()
                        || snapshot.page_epoch.is_empty()
                        || snapshot.graph_revision == 0
                    {
                        return Err(());
                    }
                    Ok(core_runtime::TaskTabSnapshot::new(
                        core_runtime::TaskTabTarget::new(
                            browser_session_id.clone(),
                            core_runtime::TabId::new(snapshot.tab_id.clone()),
                            FrameId(snapshot.frame_id.clone()),
                            PageEpoch(snapshot.page_epoch.clone()),
                            GraphRevision(snapshot.graph_revision),
                        ),
                        snapshot.active,
                    ))
                })
                .collect::<Result<Vec<_>, ()>>()?;
            core_runtime::TaskTabActionResult::listed(browser_session_id, tabs)
                .map(Some)
                .map_err(|_| ())
        }
        (wire::TaskActionOperationKind::TabsActivate, wire::TaskTabPostcondition::Active) => {
            if !terminal.task_tab_snapshots.is_empty() {
                return Err(());
            }
            Ok(Some(core_runtime::TaskTabActionResult::activated(
                target(terminal, browser_session_id)?,
                terminal.task_tab_state_was_already_satisfied,
            )))
        }
        (wire::TaskActionOperationKind::TabsClose, wire::TaskTabPostcondition::Absent) => {
            if !terminal.task_tab_snapshots.is_empty() {
                return Err(());
            }
            Ok(Some(core_runtime::TaskTabActionResult::closed(
                target(terminal, browser_session_id)?,
                terminal.task_tab_state_was_already_satisfied,
            )))
        }
        _ => Err(()),
    }
}
